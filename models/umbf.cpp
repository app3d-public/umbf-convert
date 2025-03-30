#include "umbf.hpp"
#include <acul/log.hpp>
#include <umbf/version.h>

namespace models
{
    bool UMBFRoot::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            type_sign = getFormatField(obj, "type");
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
            if (_signature == 0) _signature = umbf::sign_block::meta::image2D;
            if (!_serializer)
            {
                switch (_signature)
                {
                    case umbf::sign_block::meta::image2D:
                        _serializer = acul::make_shared<IPath>(umbf::sign_block::format::image);
                        break;
                    case umbf::sign_block::meta::image_atlas:
                        _serializer = acul::make_shared<Atlas>();
                        break;
                    default:
                        throw acul::runtime_error("Unsupported texture type");
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

    bool IPath::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            _path = getField<acul::string>(obj, "path");
        }
        catch (const std::exception &e)
        {
            logError("IPath deserialization error: %s", e.what());
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
                acul::shared_ptr<IPath> texture = acul::make_shared<IPath>(umbf::sign_block::format::image);
                if (!texture->deserializeObject(image)) throw acul::runtime_error("Failed to deserialize image");
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
                u16 tex_type = getFormatField(texture, "type");
                acul::shared_ptr<UMBFRoot> asset;
                switch (tex_type)
                {
                    case umbf::sign_block::format::image:
                    {
                        acul::shared_ptr<Image> textureAsset = acul::make_shared<Image>();
                        if (textureAsset->deserializeObject(texture)) asset = textureAsset;
                        break;
                    }
                    case umbf::sign_block::format::target:
                    {
                        acul::shared_ptr<Target> textureAsset = acul::make_shared<Target>();
                        if (textureAsset->deserializeObject(texture)) asset = textureAsset;
                        break;
                    }
                    default:
                        logError("Unsupported texture type: %x", tex_type);
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

    void Material::parseNodeInfo(const rapidjson::Value &nodeInfo, umbf::MaterialNode &node)
    {
        node.rgb = getField<glm::vec3>(nodeInfo, "rgb");
        node.textured = getField<bool>(nodeInfo, "textured");
        if (node.textured) node.texture_id = getField<int>(nodeInfo, "texture_id");
    }

    bool Mesh::deserializeObject(const rapidjson::Value &obj)
    {
        try
        {
            _path = getField<acul::string>(obj, "path");
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
            auto meshAsset = acul::make_shared<Mesh>();
            if (!meshAsset->deserializeObject(mesh)) return false;
            _meshes.push_back(meshAsset);
        }
        return true;
    }

    bool Scene::deserializeTextures(const rapidjson::Value &obj)
    {
        for (const auto &texture : getField<rapidjson::Value::ConstArray>(obj, "textures"))
        {
            u16 tex_type = getFormatField(texture, "type");
            acul::shared_ptr<UMBFRoot> asset;
            switch (tex_type)
            {
                case umbf::sign_block::format::image:
                {
                    acul::shared_ptr<Image> imageAsset = acul::make_shared<Image>();
                    if (imageAsset->deserializeObject(texture)) asset = imageAsset;
                    break;
                }
                case umbf::sign_block::format::target:
                {
                    acul::shared_ptr<Target> imageAsset = acul::make_shared<Target>();
                    if (imageAsset->deserializeObject(texture)) asset = imageAsset;
                    break;
                }
                default:
                    logError("Unsupported image type: %d", tex_type);
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
            acul::string name = getField<acul::string>(material, "name");
            u16 mat_type = getFormatField(material, "type");
            acul::shared_ptr<UMBFRoot> asset;
            switch (mat_type)
            {
                case umbf::sign_block::format::material:
                {
                    acul::shared_ptr<Material> materialAsset = acul::make_shared<Material>();
                    if (materialAsset->deserializeObject(material)) asset = materialAsset;
                    break;
                }
                case umbf::sign_block::format::target:
                {
                    acul::shared_ptr<Target> materialAsset = acul::make_shared<Target>();
                    if (materialAsset->deserializeObject(material)) asset = materialAsset;
                    break;
                }
                default:
                    logError("Unsupported material type: %d", mat_type);
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
            _url = getField<acul::string>(obj, "url");
            _header.vendor_sign = UMBF_VENDOR_ID;
            _header.vendor_version = UMBF_VERSION;
            _header.spec_version = UMBF_VERSION;
            _header.type_sign = getFormatField(obj, "target_type");
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

    acul::shared_ptr<UMBFRoot> Library::parseAsset(const rapidjson::Value &obj, FileNode &node)
    {
        u16 assetType = getFormatField(obj, "type");
        switch (assetType)
        {
            case umbf::sign_block::format::image:
            {
                acul::shared_ptr<Image> asset = acul::make_shared<Image>();
                if (!asset->deserializeObject(obj)) throw acul::runtime_error("Failed to deserialize image asset");
                return asset;
            }
            case umbf::sign_block::format::material:
            {
                acul::shared_ptr<Material> asset = acul::make_shared<Material>();
                if (!asset->deserializeObject(obj)) throw acul::runtime_error("Failed to deserialize material asset");
                return asset;
            }
            case umbf::sign_block::format::scene:
            {
                acul::shared_ptr<Scene> sceneAsset = acul::make_shared<Scene>();
                if (!sceneAsset->deserializeObject(obj)) throw acul::runtime_error("Failed to deserialize scene asset");
                return sceneAsset;
            }
            case umbf::sign_block::format::target:
            {
                acul::shared_ptr<Target> targetAsset = acul::make_shared<Target>();
                if (!targetAsset->deserializeObject(obj))
                    throw acul::runtime_error("Failed to deserialize target asset");
                return targetAsset;
            }
            case umbf::sign_block::format::library:
            {
                acul::shared_ptr<Library> libraryAsset = acul::make_shared<Library>();
                if (!libraryAsset->deserializeObject(obj))
                    throw acul::runtime_error("Failed to deserialize library asset");
                return libraryAsset;
            }
            case umbf::sign_block::format::raw:
            {
                acul::shared_ptr<IPath> rawAsset = acul::make_shared<IPath>(umbf::sign_block::format::raw);
                if (!rawAsset->deserializeObject(obj)) throw acul::runtime_error("Failed to deserialize raw asset");
                return rawAsset;
            }
            default:
                throw acul::runtime_error(acul::format("Unsupported asset type: %x", assetType));
        }
        return nullptr;
    }

    bool Library::parseFileTree(const rapidjson::Value &obj, FileNode &node)
    {
        try
        {
            node.name = getField<acul::string>(obj, "name");
            node.isFolder = getField<bool>(obj, "isFolder", false);
            if (!node.isFolder)
            {
                if (!obj.HasMember("asset") || !obj["asset"].IsObject())
                    throw acul::runtime_error("Missing 'asset' field");
                node.asset = parseAsset(obj["asset"], node);
                return true;
            }
            for (const auto &child : getField<rapidjson::Value::ConstArray>(obj, "children"))
            {
                FileNode childNode;
                if (parseFileTree(child, childNode))
                    node.children.push_back(childNode);
                else
                    throw acul::runtime_error("Failed to parse file node: " + childNode.name);
            }
        }
        catch (const std::exception &e)
        {
            logError("Library Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }
} // namespace models