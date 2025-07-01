#include "umbf.hpp"
#include <acul/log.hpp>
#include <umbf/version.h>

namespace models
{
    bool UMBFRoot::deserialize_object(const rapidjson::Value &obj)
    {
        try
        {
            type_sign = get_format_field(obj, "type");
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Info header Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Image::deserialize_object(const rapidjson::Value &obj)
    {
        try
        {
            _signature = get_image_type(obj, "texture_type", false);
            if (_signature == 0) _signature = umbf::sign_block::Image2D;
            if (!_serializer)
            {
                switch (_signature)
                {
                    case umbf::sign_block::Image2D:
                        _serializer = acul::make_shared<IPath>(umbf::sign_block::format::Image);
                        break;
                    case umbf::sign_block::ImageAtlas:
                        _serializer = acul::make_shared<Atlas>();
                        break;
                    default:
                        throw acul::runtime_error("Unsupported texture type");
                }
            }
            else
                LOG_INFO("Texture already deserialized");
            return _serializer->deserialize_object(obj);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Image Deserialization error: %s", e.what());
            return false;
        }
    }

    bool IPath::deserialize_object(const rapidjson::Value &obj)
    {
        try
        {
            _path = get_field<acul::string>(obj, "path");
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("IPath deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Atlas::deserialize_object(const rapidjson::Value &obj)
    {
        try
        {
            _width = get_field<u64>(obj, "width");
            _height = get_field<u64>(obj, "height");
            _precision = get_field<int>(obj, "precision");
            _bytes_per_channel = get_field<int>(obj, "bytesPerChannel");
            _format = get_field<vk::Format>(obj, "format");
            for (const auto &image : get_field<rapidjson::Value::ConstArray>(obj, "images"))
            {
                acul::shared_ptr<IPath> texture = acul::make_shared<IPath>(umbf::sign_block::format::Image);
                if (!texture->deserialize_object(image)) throw acul::runtime_error("Failed to deserialize image");
                _images.push_back(texture);
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Image atlas Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Material::deserialize_object(const rapidjson::Value &obj)
    {
        try
        {
            for (const auto &texture : get_field<rapidjson::Value::ConstArray>(obj, "textures"))
            {
                u16 tex_type = get_format_field(texture, "type");
                acul::shared_ptr<UMBFRoot> asset;
                switch (tex_type)
                {
                    case umbf::sign_block::format::Image:
                    {
                        acul::shared_ptr<Image> textureAsset = acul::make_shared<Image>();
                        if (textureAsset->deserialize_object(texture)) asset = textureAsset;
                        break;
                    }
                    case umbf::sign_block::format::Target:
                    {
                        acul::shared_ptr<Target> textureAsset = acul::make_shared<Target>();
                        if (textureAsset->deserialize_object(texture)) asset = textureAsset;
                        break;
                    }
                    default:
                        LOG_ERROR("Unsupported texture type: %x", tex_type);
                        return false;
                }
                if (asset)
                    _textures.push_back(asset);
                else
                {
                    LOG_ERROR("Failed to deserialize texture");
                    return false;
                }
            }
            parse_node_info(obj["albedo"], _albedo_node);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Material Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    void Material::parse_node_info(const rapidjson::Value &nodeInfo, umbf::MaterialNode &node)
    {
        node.rgb = get_field<glm::vec3>(nodeInfo, "rgb");
        node.textured = get_field<bool>(nodeInfo, "textured");
        if (node.textured) node.texture_id = get_field<int>(nodeInfo, "texture_id");
    }

    bool Mesh::deserialize_object(const rapidjson::Value &obj)
    {
        try
        {
            _path = get_field<acul::string>(obj, "path");
            _mat_id = get_field<int>(obj, "mat_id", false);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Mesh Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Scene::deserialize_meshes(const rapidjson::Value &obj)
    {
        for (auto &mesh : get_field<rapidjson::Value::ConstArray>(obj, "meshes"))
        {
            auto mesh_asset = acul::make_shared<Mesh>();
            if (!mesh_asset->deserialize_object(mesh)) return false;
            _meshes.push_back(mesh_asset);
        }
        return true;
    }

    bool Scene::deserialize_textures(const rapidjson::Value &obj)
    {
        for (const auto &texture : get_field<rapidjson::Value::ConstArray>(obj, "textures"))
        {
            u16 tex_type = get_format_field(texture, "type");
            acul::shared_ptr<UMBFRoot> asset;
            switch (tex_type)
            {
                case umbf::sign_block::format::Image:
                {
                    acul::shared_ptr<Image> image_asset = acul::make_shared<Image>();
                    if (image_asset->deserialize_object(texture)) asset = image_asset;
                    break;
                }
                case umbf::sign_block::format::Target:
                {
                    acul::shared_ptr<Target> image_asset = acul::make_shared<Target>();
                    if (image_asset->deserialize_object(texture)) asset = image_asset;
                    break;
                }
                default:
                    LOG_ERROR("Unsupported image type: %d", tex_type);
                    return false;
            }
            if (asset)
                _textures.push_back(asset);
            else
            {
                LOG_ERROR("Failed to deserialize image");
                return false;
            }
        }
        return true;
    }

    bool Scene::deserialize_materials(const rapidjson::Value &obj)
    {
        for (const auto &material : get_field<rapidjson::Value::ConstArray>(obj, "materials"))
        {
            acul::string name = get_field<acul::string>(material, "name");
            u16 mat_type = get_format_field(material, "type");
            acul::shared_ptr<UMBFRoot> asset;
            switch (mat_type)
            {
                case umbf::sign_block::format::Material:
                {
                    acul::shared_ptr<Material> material_asset = acul::make_shared<Material>();
                    if (material_asset->deserialize_object(material)) asset = material_asset;
                    break;
                }
                case umbf::sign_block::format::Target:
                {
                    acul::shared_ptr<Target> material_asset = acul::make_shared<Target>();
                    if (material_asset->deserialize_object(material)) asset = material_asset;
                    break;
                }
                default:
                    LOG_ERROR("Unsupported material type: %d", mat_type);
                    return false;
            }
            if (asset)
                _materials.emplace_back(name, asset);
            else
            {
                LOG_ERROR("Failed to deserialize material: %s", name.c_str());
                return false;
            }
        }
        return true;
    }

    bool Scene::deserialize_object(const rapidjson::Value &obj)
    {
        try
        {
            if (!deserialize_meshes(obj)) return false;
            if (!deserialize_textures(obj)) return false;
            if (!deserialize_materials(obj)) return false;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Asset scene Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    bool Target::deserialize_object(const rapidjson::Value &obj)
    {
        try
        {
            _url = get_field<acul::string>(obj, "url");
            _header.vendor_sign = UMBF_VENDOR_ID;
            _header.vendor_version = UMBF_VERSION;
            _header.spec_version = UMBF_VERSION;
            _header.type_sign = get_format_field(obj, "target_type");
            _header.compressed = get_field<bool>(obj, "target_compress", false);
            _checksum = get_field<u64>(obj, "target_checksum", false);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Target Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }

    acul::shared_ptr<UMBFRoot> Library::parse_asset(const rapidjson::Value &obj, FileNode &node)
    {
        u16 asset_type = get_format_field(obj, "type");
        switch (asset_type)
        {
            case umbf::sign_block::format::Image:
            {
                acul::shared_ptr<Image> asset = acul::make_shared<Image>();
                if (!asset->deserialize_object(obj)) throw acul::runtime_error("Failed to deserialize image asset");
                return asset;
            }
            case umbf::sign_block::format::Material:
            {
                acul::shared_ptr<Material> asset = acul::make_shared<Material>();
                if (!asset->deserialize_object(obj)) throw acul::runtime_error("Failed to deserialize material asset");
                return asset;
            }
            case umbf::sign_block::format::Scene:
            {
                acul::shared_ptr<Scene> scene_asset = acul::make_shared<Scene>();
                if (!scene_asset->deserialize_object(obj))
                    throw acul::runtime_error("Failed to deserialize scene asset");
                return scene_asset;
            }
            case umbf::sign_block::format::Target:
            {
                acul::shared_ptr<Target> target_asset = acul::make_shared<Target>();
                if (!target_asset->deserialize_object(obj))
                    throw acul::runtime_error("Failed to deserialize target asset");
                return target_asset;
            }
            case umbf::sign_block::format::Library:
            {
                acul::shared_ptr<Library> library_asset = acul::make_shared<Library>();
                if (!library_asset->deserialize_object(obj))
                    throw acul::runtime_error("Failed to deserialize library asset");
                return library_asset;
            }
            case umbf::sign_block::format::Raw:
            {
                acul::shared_ptr<IPath> raw_asset = acul::make_shared<IPath>(umbf::sign_block::format::Raw);
                if (!raw_asset->deserialize_object(obj)) throw acul::runtime_error("Failed to deserialize raw asset");
                return raw_asset;
            }
            default:
                throw acul::runtime_error(acul::format("Unsupported asset type: %x", asset_type));
        }
        return nullptr;
    }

    bool Library::parse_file_tree(const rapidjson::Value &obj, FileNode &node)
    {
        try
        {
            node.name = get_field<acul::string>(obj, "name");
            node.is_folder = get_field<bool>(obj, "isFolder", false);
            if (!node.is_folder)
            {
                if (!obj.HasMember("asset") || !obj["asset"].IsObject())
                    throw acul::runtime_error("Missing 'asset' field");
                node.asset = parse_asset(obj["asset"], node);
                return true;
            }
            for (const auto &child : get_field<rapidjson::Value::ConstArray>(obj, "children"))
            {
                FileNode child_node;
                if (parse_file_tree(child, child_node))
                    node.children.push_back(child_node);
                else
                    throw acul::runtime_error("Failed to parse file node: " + child_node.name);
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Library Deserialization error: %s", e.what());
            return false;
        }
        return true;
    }
} // namespace models