#include <acul/log.hpp>
#include <inttypes.h>
#include <umbf/ext/image/image.hpp>
#include <umbf/ext/material/material.hpp>
#include <umbf/ext/scene/scene.hpp>
#include <umbf/umbf.hpp>

namespace
{
    template <typename T>
    acul::unique_ptr<T> find_block(const umbf::ReadDescriptor &file, u32 signature)
    {
        for (auto block = file.begin(); block != file.end(); ++block)
        {
            if (block->signature != signature) continue;
            auto value = umbf::get_block(block);
            if (!value || value->signature() != signature) return nullptr;
            return acul::unique_ptr<T>(static_cast<T *>(value.release()));
        }
        return nullptr;
    }

    template <typename T>
    acul::unique_ptr<T> find_block(const umbf::ReadDescriptor &file, const umbf::BlockRange &range, u32 signature)
    {
        auto block = umbf::BlockIterator(file, range.first_block);
        for (u32 index = 0u; index < range.block_count && block != file.end(); ++index, ++block)
        {
            if (block->signature != signature) continue;
            auto value = umbf::get_block(block);
            if (!value || value->signature() != signature) return nullptr;
            return acul::unique_ptr<T>(static_cast<T *>(value.release()));
        }
        return nullptr;
    }
} // namespace

bool print_raw(const umbf::ReadDescriptor &file)
{
    auto block = file.begin();
    while (block != file.end() && block->signature != umbf::sign_block::raw) ++block;
    if (block == file.end())
    {
        LOG_ERROR("raw payload not found");
        return false;
    }

    LOG_INFO("data size: %" PRIu64, block->size);
    return true;
}

void print_image_atlas(const umbf::Atlas *atlas)
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
        LOG_ERROR("failed to find image meta");
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

    if (auto atlas = find_block<umbf::Atlas>(file, umbf::sign_block::image_atlas)) print_image_atlas(atlas.get());

    return true;
}

bool print_scene(const umbf::ReadDescriptor &file)
{
    auto objects = find_block<umbf::ObjectList>(file, umbf::sign_block::scene_objects_list);
    auto textures = find_block<umbf::ImageList>(file, umbf::sign_block::images_list);
    auto materials = find_block<umbf::MaterialList>(file, umbf::sign_block::materials_list);
    if (!objects || !textures || !materials)
    {
        LOG_ERROR("failed to find scene meta");
        return false;
    }
    LOG_INFO("-------------scene meta--------------");
    LOG_INFO("objects size: %zu", objects->objects.size());
    u64 i = 0u;
    for (const auto &entry : objects->objects)
    {
        auto object = find_block<umbf::ObjectInfo>(file, entry, umbf::sign_block::object_info);
        if (!object) continue;
        LOG_INFO("-------------------------------------");
        LOG_INFO("object: #%" PRIu64, i++);
        LOG_INFO("id: %" PRIx64, object->id);
        LOG_INFO("name: %s", object->name.c_str());
        LOG_INFO("object blocks: %u", entry.block_count);
    }
    LOG_INFO("------------textures info------------");
    LOG_INFO("textures size: %zu", textures->images.size());
    i = 0u;
    for (const auto &entry : textures->images)
        LOG_INFO("#%" PRIu64 " | file: %08x", i++, entry.header.type_sign);
    LOG_INFO("-----------materials info------------");
    LOG_INFO("materials size: %zu", materials->materials.size());
    i = 0u;
    for (const auto &entry : materials->materials)
        LOG_INFO("#%" PRIu64 " | file: %08x", i++, entry.header.type_sign);
    return true;
}

bool print_target(const umbf::ReadDescriptor &file)
{
    if (file.file->vendor_sign != UMBF_VENDOR_ID || file.file->type_sign != umbf::sign_block::format::target)
    {
        LOG_ERROR("unsupported file type: %x", file.file->type_sign);
        return false;
    }

    auto target = find_block<umbf::Target>(file, umbf::sign_block::target);
    if (!target)
    {
        LOG_ERROR("failed to find target meta");
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
        LOG_ERROR("unsupported file type: %x", file.file->type_sign);
        return false;
    }
    auto material = find_block<umbf::Material>(file, umbf::sign_block::material);
    if (!material)
    {
        LOG_ERROR("failed to find material meta");
        return false;
    }
    LOG_INFO("------------material meta--------------");
    auto textures = find_block<umbf::ImageList>(file, umbf::sign_block::images_list);
    LOG_INFO("textures size: %zu", textures ? textures->images.size() : 0u);
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
        LOG_ERROR("unsupported file type: %x", file.file->type_sign);
        return false;
    }
    auto library = find_block<umbf::Library>(file, umbf::sign_block::library);
    if (!library)
    {
        LOG_ERROR("failed to find library meta");
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
        LOG_ERROR("failed to load file. Error code: 0x%016" PRIx64, static_cast<u64>(res));
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
        umbf::close_read_descriptor(file);
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
            LOG_ERROR("unsupported file type: %x", file.file->type_sign);
            break;
    }
    umbf::close_read_descriptor(file);
    return result;
}
