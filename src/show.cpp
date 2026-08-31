#include <acul/log.hpp>
#include <inttypes.h>
#include <umbf/ext/image/image.hpp>
#include <umbf/ext/material/material.hpp>
#include <umbf/ext/scene/scene.hpp>
#include <umbf/umbf.hpp>

namespace
{
    template <typename T>
    acul::shared_ptr<T> find_block(const umbf::ReadDescriptor &file, u32 signature)
    {
        for (auto block = file.begin(); block != file.end(); ++block)
        {
            if (block->signature != signature) continue;
            auto value = umbf::get_block(block);
            if (!value || value->signature() != signature) return nullptr;
            return acul::static_pointer_cast<T>(value);
        }
        return nullptr;
    }

    bool find_nested_file(const umbf::ReadDescriptor &parent, u32 type_sign, umbf::ReadDescriptor &result)
    {
        for (auto block = parent.begin(); block != parent.end(); ++block)
            if (block->signature == type_sign)
                return umbf::create_read_descriptor(parent, block, result, type_sign).success();
        return false;
    }

    u64 block_count(const umbf::ReadDescriptor &file)
    {
        u64 result = 0u;
        for (auto block = file.begin(); block != file.end(); ++block) ++result;
        return result;
    }
} // namespace

bool print_raw(const umbf::ReadDescriptor &file)
{
    auto block = file.begin();
    while (block != file.end() && block->signature != umbf::sign_block::raw) ++block;
    if (block == file.end())
    {
        LOG_ERROR("Raw payload not found");
        return false;
    }

    LOG_INFO("Data size: %" PRIu64, block->size);
    return true;
}

void print_image_atlas(const acul::shared_ptr<umbf::Atlas> &atlas)
{
    LOG_INFO("-------------atlas meta--------------");
    LOG_INFO("rects size: %zu", atlas->pack_data.size());
    LOG_INFO("padding: %d", atlas->padding);
}

bool print_image(const umbf::ReadDescriptor &file)
{
    auto image = find_block<umbf::Image2D>(file, umbf::sign_block::image);
    if (!image)
    {
        LOG_ERROR("Failed to find image meta");
        return false;
    }

    LOG_INFO("-------------image meta--------------");
    LOG_INFO("width: %d", image->width);
    LOG_INFO("height: %d", image->height);
    acul::stringstream ss;
    for (int i = 0; i < image->channels.size(); ++i)
    {
        ss << image->channels[i];
        if (i < image->channels.size() - 1) ss << ", ";
    }
    LOG_INFO("channels: (%zu) %s", image->channels.size(), ss.str().c_str());
    LOG_INFO("image format: %s", acul::to_string(image->format).c_str());
    LOG_INFO("size: %" PRIu64, image->size());
    acul::release(image->pixels);

    if (auto atlas = find_block<umbf::Atlas>(file, umbf::sign_block::image_atlas)) print_image_atlas(atlas);

    return true;
}

bool print_scene(const umbf::ReadDescriptor &file)
{
    umbf::ReadDescriptor objects, textures, materials;
    if (!find_nested_file(file, umbf::sign_block::format::scene_objects_list, objects) ||
        !find_nested_file(file, umbf::sign_block::format::images_list, textures) ||
        !find_nested_file(file, umbf::sign_block::format::materials_list, materials))
    {
        LOG_ERROR("Failed to find scene meta");
        umbf::close_map_descriptor(objects);
        umbf::close_map_descriptor(textures);
        umbf::close_map_descriptor(materials);
        return false;
    }
    LOG_INFO("-------------scene meta--------------");
    LOG_INFO("Objects size: %" PRIu64, block_count(objects));
    u64 i = 0u;
    for (auto entry = objects.begin(); entry != objects.end(); ++entry, ++i)
    {
        umbf::ReadDescriptor object_file;
        if (entry->signature != umbf::sign_block::format::scene_object ||
            !umbf::create_read_descriptor(objects, entry, object_file, umbf::sign_block::format::scene_object).success())
            continue;
        auto object = find_block<umbf::ObjectInfo>(object_file, umbf::sign_block::object_info);
        if (!object)
        {
            umbf::close_map_descriptor(object_file);
            continue;
        }
        LOG_INFO("-------------------------------------");
        LOG_INFO("id: %" PRIx64, object->id);
        LOG_INFO("name: %s", object->name.c_str());
        LOG_INFO("object segments: %zu", object_file.file->sg_table.size());
        umbf::close_map_descriptor(object_file);
    }
    LOG_INFO("------------textures info------------");
    LOG_INFO("textures size: %" PRIu64, block_count(textures));
    i = 0u;
    for (auto entry = textures.begin(); entry != textures.end(); ++entry, ++i)
    {
        umbf::ReadDescriptor texture;
        if (umbf::create_read_descriptor(textures, entry, texture).success())
        {
            LOG_INFO("#%" PRIu64 " | file: %08x", i, texture.file->type_sign);
            umbf::close_map_descriptor(texture);
        }
    }
    LOG_INFO("-----------materials info------------");
    LOG_INFO("materials size: %" PRIu64, block_count(materials));
    i = 0u;
    for (auto entry = materials.begin(); entry != materials.end(); ++entry, ++i)
    {
        umbf::ReadDescriptor material;
        if (umbf::create_read_descriptor(materials, entry, material).success())
        {
            LOG_INFO("#%" PRIu64 " | file: %08x", i, material.file->type_sign);
            umbf::close_map_descriptor(material);
        }
    }
    umbf::close_map_descriptor(objects);
    umbf::close_map_descriptor(textures);
    umbf::close_map_descriptor(materials);
    return true;
}

bool print_target(const umbf::ReadDescriptor &file)
{
    if (file.file->vendor_sign != UMBF_VENDOR_ID || file.file->type_sign != umbf::sign_block::format::target)
    {
        LOG_ERROR("Unsupported file type: %x", file.file->type_sign);
        return false;
    }

    auto target = find_block<umbf::Target>(file, umbf::sign_block::target);
    if (!target)
    {
        LOG_ERROR("Failed to find target meta");
        return false;
    }

    LOG_INFO("------------target meta--------------");
    LOG_INFO("url: %s", target->url.c_str());
    LOG_INFO("tag: %u", target->tag);
    LOG_INFO("vendor_id: %x", target->header.vendor_sign);
    LOG_INFO("version: %x", target->header.vendor_version);
    LOG_INFO("spec version: %x", target->header.spec_version);
    LOG_INFO("checksum: %u", target->checksum);
    LOG_INFO("type: %x", target->header.type_sign);
    return true;
}

bool print_material(const umbf::ReadDescriptor &file)
{
    if (file.file->vendor_sign != UMBF_VENDOR_ID || file.file->type_sign != umbf::sign_block::format::material)
    {
        LOG_ERROR("Unsupported file type: %x", file.file->type_sign);
        return false;
    }
    auto material = find_block<umbf::Material>(file, umbf::sign_block::material);
    if (!material)
    {
        LOG_ERROR("Failed to find material meta");
        return false;
    }
    LOG_INFO("------------material meta--------------");
    umbf::ReadDescriptor textures;
    const bool has_textures = find_nested_file(file, umbf::sign_block::format::images_list, textures);
    LOG_INFO("textures size: %" PRIu64, has_textures ? block_count(textures) : 0u);
    if (has_textures) umbf::close_map_descriptor(textures);
    LOG_INFO("albedo:");
    LOG_INFO("   rgb: %f %f %f", material->albedo.rgb.x, material->albedo.rgb.y, material->albedo.rgb.z);
    LOG_INFO("   textured: %s", material->albedo.textured ? "true" : "false");
    if (material->albedo.textured) LOG_INFO("   texture id: %d", material->albedo.texture_id);
    return true;
}

void print_file_hierarchy(const umbf::Library::Node &node, int depth = 0, const acul::string &prefix = "")
{
    if (depth == 0 && !node.name.empty() && node.name != ".") LOG_INFO("| %s", node.name.c_str());
    else
        LOG_INFO("%s%s %s", prefix.c_str(), (const char *)(node.is_directory() ? "|----" : "|____"), node.name.c_str());

    acul::string newPrefix = prefix + (depth > 0 ? "|     " : "");

    for (size_t i = 0; i < node.children.size(); ++i)
        print_file_hierarchy(node.children[i], depth + 1,
                             i == node.children.size() - 1 ? prefix + (depth > 0 ? "      " : "") : newPrefix);
}

bool print_library(const umbf::ReadDescriptor &file)
{
    if (file.file->vendor_sign != UMBF_VENDOR_ID || file.file->type_sign != umbf::sign_block::format::library)
    {
        LOG_ERROR("Unsupported file type: %x", file.file->type_sign);
        return false;
    }
    auto library = find_block<umbf::Library>(file, umbf::sign_block::library);
    if (!library)
    {
        LOG_ERROR("Failed to find library meta");
        return false;
    }
    LOG_INFO("------------library meta--------------");
    if (library->file_tree.name.empty() || library->file_tree.name == ".")
    {
        for (const auto &child : library->file_tree.children) print_file_hierarchy(child);
    }
    else print_file_hierarchy(library->file_tree);
    return true;
}

bool show_file(const acul::string &path)
{
    umbf::ReadDescriptor file;
    acul::op_result res = umbf::create_read_descriptor(path, file);
    if (!res.success())
    {
        LOG_ERROR("Failed to load file. Error code: 0x%016" PRIx64, static_cast<u64>(res));
        return false;
    }
    LOG_INFO("vendor sign: %x", file.file->vendor_sign);
    LOG_INFO("vendor version: %x", file.file->vendor_version);
    LOG_INFO("spec version: %x", file.file->spec_version);
    LOG_INFO("type sign: %x", file.file->type_sign);
    LOG_INFO("segments: %zu", file.file->sg_table.size());
    LOG_INFO("checksum: %u", file.file->checksum);
    if (file.file->vendor_sign != UMBF_VENDOR_ID)
    {
        umbf::close_map_descriptor(file);
        return true;
    }

    bool result = false;
    switch (file.file->type_sign)
    {
        case umbf::sign_block::format::image:
            result = print_image(file);
            break;
        case umbf::sign_block::format::target:
            result = print_target(file);
            break;
        case umbf::sign_block::format::library:
            result = print_library(file);
            break;
        case umbf::sign_block::format::scene:
            result = print_scene(file);
            break;
        case umbf::sign_block::format::material:
            result = print_material(file);
            break;
        case umbf::sign_block::format::raw:
            result = print_raw(file);
            break;
        default:
            LOG_ERROR("Unsupported file type: %x", file.file->type_sign);
            break;
    }
    umbf::close_map_descriptor(file);
    return result;
}
