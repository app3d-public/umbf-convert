#include <acul/log.hpp>
#include <inttypes.h>
#include <umbf/umbf.hpp>

bool print_raw(umbf::File *file)
{
    if (file->blocks.empty())
    {
        LOG_ERROR("Meta block list is empty");
        return false;
    }
    auto &block = file->blocks.front();
    if (block->signature() != acul::meta::sign_block::Raw)
    {
        LOG_ERROR("Wrong block signature: %x. For Raw block expected raw_block.", block->signature());
        return false;
    }

    auto raw_block = acul::dynamic_pointer_cast<acul::meta::raw_block>(block);
    if (!raw_block)
    {
        LOG_ERROR("Failed to cast block to RawBlock");
        return false;
    }
    LOG_INFO("Data size: %zu", raw_block->data_size);
    return true;
}

void print_image_atlas(const acul::shared_ptr<umbf::Atlas> &atlas)
{
    LOG_INFO("-------------atlas meta--------------");
    LOG_INFO("discarding step: %d", atlas->discard_step);
    LOG_INFO("rects size: %zu", atlas->pack_data.size());
    LOG_INFO("padding: %d", atlas->padding);
}

bool print_image(umbf::File *file)
{
    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::Image2D;
        });
    if (it == file->blocks.end())
    {
        LOG_ERROR("Failed to find image meta");
        return false;
    }

    auto image = acul::static_pointer_cast<umbf::Image2D>(*it);
    LOG_INFO("-------------image meta--------------");
    LOG_INFO("width: %d", image->width);
    LOG_INFO("height: %d", image->height);
    acul::stringstream ss;
    for (int i = 0; i < image->channel_count; ++i)
    {
        ss << image->channel_names[i];
        if (i < image->channel_count - 1) ss << ", ";
    }
    LOG_INFO("channels: (%d) %s", image->channel_count, ss.str().c_str());
    LOG_INFO("bytes per channel: %d bit", image->bytes_per_channel * 8);
    LOG_INFO("image format: %s", vk::to_string(image->format).c_str());
    LOG_INFO("size: %" PRIu64, image->size());
    acul::release(image->pixels);

    auto atlas_it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::ImageAtlas;
        });
    if (atlas_it != file->blocks.end()) print_image_atlas(acul::static_pointer_cast<umbf::Atlas>(*atlas_it));

    return true;
}

bool print_scene(umbf::File *file)
{
    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::Scene;
        });
    if (it == file->blocks.end())
    {
        LOG_ERROR("Failed to find scene meta");
        return false;
    }
    auto scene = acul::static_pointer_cast<umbf::Scene>(*it);
    LOG_INFO("-------------scene meta--------------");
    auto &objects = scene->objects;
    LOG_INFO("Objects size: %zu", objects.size());
    for (auto &object : objects)
    {
        LOG_INFO("-------------------------------------");
        LOG_INFO("id: %" PRIx64, object.id);
        LOG_INFO("name: %s", object.name.c_str());
        if (object.meta.begin() == object.meta.end())
            LOG_INFO("neta: no");
        else
            for (auto &block : object.meta) LOG_INFO("Meta block signature: 0x%08x", block->signature());
    }
    LOG_INFO("------------textures info------------");
    LOG_INFO("textures size: %zu", scene->textures.size());
    int count = 0;
    for (auto &texture : scene->textures)
    {
        switch (texture.header.type_sign)
        {
            case umbf::sign_block::format::None:
                LOG_WARN("#%d | type: none", count);
                break;
            case umbf::sign_block::format::Target:
                LOG_INFO("#%d | type: target", count);
                break;
            case umbf::sign_block::format::Image:
                LOG_INFO("#%d | type: image", count);
                break;
            default:
                LOG_ERROR("#%d | incompatible type: %hx", count, texture.header.type_sign);
                break;
        }
        ++count;
    }
    LOG_INFO("-----------materials info------------");
    LOG_INFO("materials size: %zu", scene->materials.size());
    count = 0;
    for (auto &asset : scene->materials)
    {
        switch (asset.header.type_sign)
        {
            case umbf::sign_block::format::None:
                LOG_WARN("#%d | type: none", count++);
                break;
            case umbf::sign_block::format::Target:
                LOG_INFO("#%d | type: target", count++);
                break;
            case umbf::sign_block::format::Material:
            {
                LOG_INFO("#%d | type: Material", count++);
                break;
            }
            default:
                LOG_ERROR("#%d | incompatible type: %hx", count++, asset.header.type_sign);
                break;
        }
        auto info_it = std::find_if(asset.blocks.begin(), asset.blocks.end(), [](auto &block) {
            return block->signature() == umbf::sign_block::meta::MaterialInfo;
        });
        if (info_it != asset.blocks.end())
        {
            auto mat_info = acul::static_pointer_cast<umbf::MaterialInfo>(*info_it);
            LOG_INFO("   | id:   %" PRIx64, mat_info->id);
            LOG_INFO("   | name: %s", mat_info->name.c_str());
        }
    }
    return true;
}

bool print_target(umbf::File *file)
{
    if (file->header.vendor_sign != UMBF_VENDOR_ID || file->header.type_sign != umbf::sign_block::format::Target)
    {
        LOG_ERROR("Unsupported file type: %x", file->header.type_sign);
        return false;
    }

    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::Target;
        });
    if (it == file->blocks.end())
    {
        LOG_ERROR("Failed to find target meta");
        return false;
    }

    auto target = acul::static_pointer_cast<umbf::Target>(*it);
    LOG_INFO("------------target meta--------------");
    LOG_INFO("url: %s", target->url.c_str());
    LOG_INFO("vendor_id: %x", target->header.vendor_sign);
    LOG_INFO("version: %x", target->header.vendor_version);
    LOG_INFO("spec version: %x", target->header.spec_version);
    LOG_INFO("checksum: %u", target->checksum);
    LOG_INFO("type: %x", target->header.type_sign);
    LOG_INFO("compressed: %s", target->header.compressed ? "true" : "false");
    return true;
}

bool print_material(umbf::File *file)
{
    if (file->header.vendor_sign != UMBF_VENDOR_ID || file->header.type_sign != umbf::sign_block::format::Material)
    {
        LOG_ERROR("Unsupported file type: %x", file->header.type_sign);
        return false;
    }
    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::Material;
        });
    if (it == file->blocks.end())
    {
        LOG_ERROR("Failed to find material meta");
        return false;
    }
    auto material = acul::static_pointer_cast<umbf::Material>(*it);
    LOG_INFO("------------material meta--------------");
    LOG_INFO("textures size: %zu", material->textures.size());
    for (size_t i = 0; i < material->textures.size(); ++i)
    {
        auto &texture = material->textures[i];
        if (texture.header.type_sign == umbf::sign_block::format::Image)
            LOG_INFO("    %zu | embedded image", i);
        else if (texture.header.type_sign == umbf::sign_block::format::Target)
        {
            auto tit = std::find_if(texture.blocks.begin(), texture.blocks.end(),
                                    [](auto &block) { return block->signature() == umbf::sign_block::meta::Target; });
            if (tit == texture.blocks.end())
            {
                LOG_ERROR("Failed to find target meta");
                return false;
            }
            auto target = acul::static_pointer_cast<umbf::Target>(*tit);
            LOG_INFO("    %zu | %s", i, target->url.c_str());
        }
        else
            LOG_WARN("    %zu | unknown type (%x)", i, texture.header.type_sign);
    }
    LOG_INFO("albedo:");
    LOG_INFO("   rgb: %f %f %f", material->albedo.rgb.x, material->albedo.rgb.y, material->albedo.rgb.z);
    LOG_INFO("   textured: %s", material->albedo.textured ? "true" : "false");
    if (material->albedo.textured) LOG_INFO("   texture id: %d", material->albedo.texture_id);
    return true;
}

void print_file_hierarchy(const umbf::Library::Node &node, int depth = 0, const acul::string &prefix = "")
{
    if (depth == 0)
        LOG_INFO("| %s", node.name.c_str());
    else
        LOG_INFO("%s%s %s", prefix.c_str(), (const char *)(node.is_folder ? "|----" : "|____"), node.name.c_str());

    acul::string newPrefix = prefix + (depth > 0 ? "|     " : "");

    for (size_t i = 0; i < node.children.size(); ++i)
        print_file_hierarchy(node.children[i], depth + 1,
                             i == node.children.size() - 1 ? prefix + (depth > 0 ? "      " : "") : newPrefix);
}

bool print_library(umbf::File *file)
{
    if (file->header.vendor_sign != UMBF_VENDOR_ID || file->header.type_sign != umbf::sign_block::format::Library)
    {
        LOG_ERROR("Unsupported file type: %x", file->header.type_sign);
        return false;
    }
    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::Library;
        });
    if (it == file->blocks.end())
    {
        LOG_ERROR("Failed to find library meta");
        return false;
    }
    auto library = acul::static_pointer_cast<umbf::Library>(*it);
    LOG_INFO("------------library meta--------------");
    print_file_hierarchy(library->file_tree);
    return true;
}

bool show_file(const acul::string &path)
{
    auto file = umbf::File::read_from_disk(path);
    if (!file)
    {
        LOG_ERROR("Failed to load file: %s", path.c_str());
        return false;
    }
    LOG_INFO("vendor sign: %x", file->header.vendor_sign);
    LOG_INFO("vendor version: %x", file->header.vendor_version);
    LOG_INFO("spec version: %x", file->header.spec_version);
    LOG_INFO("type sign: %x", file->header.type_sign);
    LOG_INFO("compressed: %s", file->header.compressed ? "true" : "false");
    LOG_INFO("checksum: %u", file->checksum);
    if (file->header.vendor_sign != UMBF_VENDOR_ID) return true;

    switch (file->header.type_sign)
    {
        case umbf::sign_block::format::Image:
            return print_image(file.get());
        case umbf::sign_block::format::Target:
            return print_target(file.get());
        case umbf::sign_block::format::Library:
            return print_library(file.get());
        case umbf::sign_block::format::Scene:
            return print_scene(file.get());
        case umbf::sign_block::format::Material:
            return print_material(file.get());
        case umbf::sign_block::format::Raw:
            return print_raw(file.get());
        default:
            LOG_ERROR("Unsupported file type: %x", file->header.type_sign);
            return false;
    }
}