#include "asset.hpp"
#include <core/log.hpp>

namespace models
{
    bool InfoHeader::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            type = getField<assets::Type>(obj, "type");
            compressed = getField<bool>(obj, "compress", false);
        }
        catch (const std::exception &e)
        {
            logError("Info header Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Image::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            _signature = getImageType(obj, "texture_type", false);
            if (_signature == 0) _signature = assets::sign_block::image2D;
            if (!_serializer)
            {
                switch (_signature)
                {
                    case assets::sign_block::image2D:
                        _serializer = astl::make_shared<Image2D>();
                        break;
                    case assets::sign_block::image_atlas:
                        _serializer = astl::make_shared<Atlas>();
                        break;
                    default:
                        throw std::runtime_error("Unsupported texture type");
                }
            }
            else
                logInfo("Texture already deserialized");
            return _serializer->deserializeObject(obj);
        }
        catch (const std::exception &e)
        {
            logError("Image Deserialization error: %s", e.what());
            return false;
        }
    }

    bool Image2D::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            _path = getField<std::string>(obj, "path");
        }
        catch (const std::exception &e)
        {
            logError("Texture2D Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Atlas::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            _width = getField<u64>(obj, "width");
            _height = getField<u64>(obj, "height");
            _precision = getField<int>(obj, "precision");
            _bytesPerChannel = getField<int>(obj, "bytesPerChannel");
            _imageFormat = getField<vk::Format>(obj, "format");
            for (const auto &image : getField<rapidjson::Value::ConstArray>(obj, "images"))
            {
                InfoHeader imageInfo{};
                imageInfo.deserializeObject(image);
                astl::shared_ptr<Image2D> texture = astl::make_shared<Image2D>();
                if (!texture->deserializeObject(image)) throw std::runtime_error("Failed to deserialize image");
                _images.push_back(texture);
            }
        }
        catch (const std::exception &e)
        {
            logError("Image atlas Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Material::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            for (const auto &texture : getField<rapidjson::Value::ConstArray>(obj, "textures"))
            {
                InfoHeader textureInfo{};
                textureInfo.deserializeObject(texture);
                astl::shared_ptr<AssetBase> asset;
                switch (textureInfo.type)
                {
                    case assets::Type::Image:
                    {
                        astl::shared_ptr<Image> textureAsset = astl::make_shared<Image>(textureInfo);
                        if (textureAsset->deserializeObject(texture)) asset = textureAsset;
                        break;
                    }
                    case assets::Type::Target:
                    {
                        astl::shared_ptr<Target> textureAsset = astl::make_shared<Target>(textureInfo);
                        if (textureAsset->deserializeObject(texture)) asset = textureAsset;
                        break;
                    }
                    default:
                        logError("Unsupported texture type: %s", toString(textureInfo.type).c_str());
                        return false;
                }
                if (asset)
                    _textures.push_back(asset);
                else
                {
                    logError("Failed to deserialize texture");
                    return false;
                }
            }
            parseNodeInfo(obj["albedo"], _albedoNode);
        }
        catch (const std::exception &e)
        {
            logError("Material Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    void Material::parseNodeInfo(const rapidjson::Value &nodeInfo, assets::MaterialNode &node)
    {
        node.rgb = getField<glm::vec3>(nodeInfo, "rgb");
        node.textured = getField<bool>(nodeInfo, "textured");
        if (node.textured) node.textureID = getField<int>(nodeInfo, "textureID");
    }

    bool Mesh::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            _path = getField<std::string>(obj, "path");
            astl::hashmap<std::string, Format> formatMap = {{"obj", Format::Obj}};
            auto it = formatMap.find(getField<std::string>(obj, "format"));
            if (it != formatMap.end())
                _format = it->second;
            else
                throw std::runtime_error("Unsupported scene format");
            _matID = getField<int>(obj, "matID", false);
        }
        catch (const std::exception &e)
        {
            logError("Mesh Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Scene::deserializeMeshes(const rapidjson::Value &obj)
    {
        for (auto &mesh : getField<rapidjson::Value::ConstArray>(obj, "meshes"))
        {
            auto meshAsset = astl::make_shared<Mesh>();
            if (!meshAsset->deserializeObject(mesh)) return false;
            _meshes.push_back(meshAsset);
        }
        return true;
    }

    bool Scene::deserializeTextures(const rapidjson::Value &obj)
    {
        for (const auto &texture : getField<rapidjson::Value::ConstArray>(obj, "textures"))
        {
            InfoHeader textureInfo{};
            textureInfo.deserializeObject(texture);
            astl::shared_ptr<AssetBase> asset;
            switch (textureInfo.type)
            {
                case assets::Type::Image:
                {
                    astl::shared_ptr<Image> imageAsset = astl::make_shared<Image>(textureInfo);
                    if (imageAsset->deserializeObject(texture)) asset = imageAsset;
                    break;
                }
                case assets::Type::Target:
                {
                    astl::shared_ptr<Target> imageAsset = astl::make_shared<Target>(textureInfo);
                    if (imageAsset->deserializeObject(texture)) asset = imageAsset;
                    break;
                }
                default:
                    logError("Unsupported image type: %d", (int)textureInfo.type);
                    return false;
            }
            if (asset)
                _textures.push_back(asset);
            else
            {
                logError("Failed to deserialize image");
                return false;
            }
        }
        return true;
    }

    bool Scene::deserializeMaterials(const rapidjson::Value &obj)
    {
        for (const auto &material : getField<rapidjson::Value::ConstArray>(obj, "materials"))
        {
            std::string name = getField<std::string>(material, "name");
            InfoHeader materialInfo{};
            materialInfo.deserializeObject(material);
            astl::shared_ptr<AssetBase> asset;
            switch (materialInfo.type)
            {
                case assets::Type::Material:
                {
                    astl::shared_ptr<Material> materialAsset = astl::make_shared<Material>(materialInfo);
                    if (materialAsset->deserializeObject(material)) asset = materialAsset;
                    break;
                }
                case assets::Type::Target:
                {
                    astl::shared_ptr<Target> materialAsset = astl::make_shared<Target>(materialInfo);
                    if (materialAsset->deserializeObject(material)) asset = materialAsset;
                    break;
                }
                default:
                    logError("Unsupported material type: %d", (int)materialInfo.type);
                    return false;
            }
            if (asset)
                _materials.emplace_back(name, asset);
            else
            {
                logError("Failed to deserialize material: %s", name.c_str());
                return false;
            }
        }
        return true;
    }

    bool Scene::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            if (!deserializeMeshes(obj)) return false;
            if (!deserializeTextures(obj)) return false;
            if (!deserializeMaterials(obj)) return false;
        }
        catch (const std::exception &e)
        {
            logError("Asset scene Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Target::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            _addr.proto = getField<assets::Target::Addr::Proto>(obj, "proto");
            _addr.url = getField<std::string>(obj, "url");
            _header.type = getField<assets::Type>(obj, "target_type");
            _header.compressed = getField<bool>(obj, "target_compress", false);
            _checksum = getField<u64>(obj, "target_checksum", false);
        }
        catch (const std::exception &e)
        {
            logError("Target Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    astl::shared_ptr<AssetBase> Library::parseAsset(const rapidjson::Value &obj, FileNode &node)
    {
        InfoHeader assetInfo{};
        if (!assetInfo.deserializeObject(obj)) throw std::runtime_error("Failed to deserialize asset");
        switch (assetInfo.type)
        {
            case assets::Type::Image:
            {
                astl::shared_ptr<Image> asset = astl::make_shared<Image>(assetInfo);
                if (!asset->deserializeObject(obj)) throw std::runtime_error("Failed to deserialize image asset");
                return asset;
            }
            case assets::Type::Material:
            {
                astl::shared_ptr<Material> asset = astl::make_shared<Material>(assetInfo);
                if (!asset->deserializeObject(obj)) throw std::runtime_error("Failed to deserialize material asset");
                return asset;
            }
            case assets::Type::Scene:
            {
                astl::shared_ptr<Scene> sceneAsset = astl::make_shared<Scene>(assetInfo);
                if (!sceneAsset->deserializeObject(obj)) throw std::runtime_error("Failed to deserialize scene asset");
                return sceneAsset;
            }
            case assets::Type::Target:
            {
                astl::shared_ptr<Target> targetAsset = astl::make_shared<Target>(assetInfo);
                if (!targetAsset->deserializeObject(obj))
                    throw std::runtime_error("Failed to deserialize target asset");
                return targetAsset;
            }
            case assets::Type::Library:
            {
                astl::shared_ptr<Library> libraryAsset = astl::make_shared<Library>(assetInfo);
                if (!libraryAsset->deserializeObject(obj))
                    throw std::runtime_error("Failed to deserialize library asset");
                return libraryAsset;
            }
            default:
                throw std::runtime_error("Unsupported asset type: " + assets::toString(assetInfo.type));
        }
        return nullptr;
    }

    bool Library::parseFileTree(const rapidjson::Value &obj, FileNode &node)
    {
        try
        {
            node.name = getField<std::string>(obj, "name");
            node.isFolder = getField<bool>(obj, "isFolder", false);
            if (!node.isFolder)
            {
                if (!obj.HasMember("asset") || !obj["asset"].IsObject())
                    throw std::runtime_error("Missing 'asset' field");
                node.asset = parseAsset(obj["asset"], node);
                return true;
            }
            for (const auto &child : getField<rapidjson::Value::ConstArray>(obj, "children"))
            {
                FileNode childNode;
                if (parseFileTree(child, childNode))
                    node.children.push_back(childNode);
                else
                    throw std::runtime_error("Failed to parse file node: " + childNode.name);
            }
        }
        catch (const std::exception &e)
        {
            logError("Library Deserialization error: %s", e.what());
            return false;
        }

        return true;
    }

    bool Library::deserializeObject(const rapidjson::Value &obj) { return parseFileTree(obj, _fileTree); }

} // namespace models