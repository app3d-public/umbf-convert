#include <acul/log.hpp>
#include <assets/asset.hpp>

bool printRaw(umbf::File *file)
{
    if (file->blocks.empty())
    {
        logError("Meta block list is empty");
        return false;
    }
    auto &block = file->blocks.front();
    if (block->signature() != acul::meta::sign_block::raw_block)
    {
        logError("Wrong block signature: %x. For Raw block expected raw_block.", block->signature());
        return false;
    }

    auto raw_block = acul::dynamic_pointer_cast<acul::meta::raw_block>(block);
    if (!raw_block)
    {
        logError("Failed to cast block to RawBlock");
        return false;
    }
    logInfo("Data size: %zu", raw_block->dataSize);
    return true;
}

void printImageAtlas(const acul::shared_ptr<umbf::Atlas> &atlas)
{
    logInfo("-------------atlas meta--------------");
    logInfo("discarding step: %d", atlas->discardStep);
    logInfo("rects size: %zu", atlas->packData.size());
    logInfo("padding: %d", atlas->padding);
}

bool printImage2D(umbf::File *file)
{
    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::image2D;
        });
    if (it == file->blocks.end())
    {
        logError("Failed to find image meta");
        return false;
    }

    auto image = acul::static_pointer_cast<umbf::Image2D>(*it);
    logInfo("-------------image meta--------------");
    logInfo("width: %d", image->width);
    logInfo("height: %d", image->height);
    acul::stringstream ss;
    for (size_t i = 0; i < image->channelCount; ++i)
    {
        ss << image->channelNames[i];
        if (i < image->channelCount - 1) ss << ", ";
    }
    logInfo("channels: (%d) %s", image->channelCount, ss.str().c_str());
    logInfo("bytes per channel: %d bit", image->bytesPerChannel * 8);
    logInfo("image format: %s", vk::to_string(image->imageFormat).c_str());
    logInfo("size: %llu", image->imageSize());
    acul::release(image->pixels);

    auto atlas_it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::image_atlas;
        });
    if (atlas_it != file->blocks.end()) printImageAtlas(acul::static_pointer_cast<umbf::Atlas>(*atlas_it));

    return true;
}

bool printScene(umbf::File *file)
{
    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::scene;
        });
    if (it == file->blocks.end())
    {
        logError("Failed to find scene meta");
        return false;
    }
    auto scene = acul::static_pointer_cast<umbf::Scene>(*it);
    logInfo("-------------scene meta--------------");
    auto &objects = scene->objects;
    logInfo("Objects size: %zu", objects.size());
    for (auto &object : objects)
    {
        logInfo("-------------------------------------");
        logInfo("id: %llx", object.id);
        logInfo("name: %s", object.name.c_str());
        if (object.meta.begin() == object.meta.end())
            logInfo("neta: no");
        else
            for (auto &block : object.meta) logInfo("Meta block signature: 0x%08x", block->signature());
    }
    logInfo("------------textures info------------");
    logInfo("textures size: %zu", scene->textures.size());
    int count = 0;
    for (auto &texture : scene->textures)
    {
        switch (texture.header.type_sign)
        {
            case umbf::sign_block::format::none:
                logWarn("#%d | type: none", count);
                break;
            case umbf::sign_block::format::target:
                logInfo("#%d | type: target", count);
                break;
            case umbf::sign_block::format::image:
                logInfo("#%d | type: image", count);
                break;
            default:
                logError("#%d | incompatible type: %hx", count, texture.header.type_sign);
                break;
        }
        ++count;
    }
    logInfo("-----------materials info------------");
    logInfo("materials size: %zu", scene->materials.size());
    count = 0;
    for (auto &asset : scene->materials)
    {
        switch (asset.header.type_sign)
        {
            case umbf::sign_block::format::none:
                logWarn("#%d | type: none", count++);
                break;
            case umbf::sign_block::format::target:
                logInfo("#%d | type: target", count++);
                break;
            case umbf::sign_block::format::material:
            {
                logInfo("#%d | type: Material", count++);
                break;
            }
            default:
                logError("#%d | incompatible type: %hx", count++, asset.header.type_sign);
                break;
        }
        auto info_it = std::find_if(asset.blocks.begin(), asset.blocks.end(), [](auto &block) {
            return block->signature() == umbf::sign_block::meta::material_info;
        });
        if (info_it != asset.blocks.end())
        {
            auto mat_info = acul::static_pointer_cast<umbf::MaterialInfo>(*info_it);
            logInfo("   | id:   %llx", mat_info->id);
            logInfo("   | name: %s", mat_info->name.c_str());
        }
    }
    return true;
}

bool printTarget(umbf::File *file)
{
    if (file->header.vendor_sign != UMBF_VENDOR_ID || file->header.type_sign != umbf::sign_block::format::target)
    {
        logError("Unsupported file type: %x", file->header.type_sign);
        return false;
    }

    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::target;
        });
    if (it == file->blocks.end())
    {
        logError("Failed to find target meta");
        return false;
    }

    auto target = acul::static_pointer_cast<umbf::Target>(*it);
    logInfo("------------target meta--------------");
    logInfo("url: %s", target->url.c_str());
    logInfo("vendor_id: %x", target->header.vendor_sign);
    logInfo("version: %x", target->header.vendor_version);
    logInfo("spec version: %x", target->header.spec_version);
    logInfo("checksum: %u", target->checksum);
    logInfo("type: %x", target->header.type_sign);
    logInfo("compressed: %s", target->header.compressed ? "true" : "false");
    return true;
}

bool printMaterial(umbf::File *file)
{
    if (file->header.vendor_sign != UMBF_VENDOR_ID || file->header.type_sign != umbf::sign_block::format::material)
    {
        logError("Unsupported file type: %x", file->header.type_sign);
        return false;
    }
    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::material;
        });
    if (it == file->blocks.end())
    {
        logError("Failed to find material meta");
        return false;
    }
    auto material = acul::static_pointer_cast<umbf::Material>(*it);
    logInfo("------------material meta--------------");
    logInfo("textures size: %zu", material->textures.size());
    for (int i = 0; i < material->textures.size(); ++i)
    {
        auto &texture = material->textures[i];
        if (texture.header.type_sign == umbf::sign_block::format::image)
            logInfo("    %d | embedded image", i);
        else if (texture.header.type_sign == umbf::sign_block::format::target)
        {
            auto tit = std::find_if(texture.blocks.begin(), texture.blocks.end(),
                                    [](auto &block) { return block->signature() == umbf::sign_block::meta::target; });
            if (tit == texture.blocks.end())
            {
                logError("Failed to find target meta");
                return false;
            }
            auto target = acul::static_pointer_cast<umbf::Target>(*tit);
            logInfo("    %d | %s", i, target->url.c_str());
        }
        else
            logWarn("    %d | unknown type (%x)", i, texture.header.type_sign);
    }
    logInfo("albedo:");
    logInfo("   rgb: %f %f %f", material->albedo.rgb.x, material->albedo.rgb.y, material->albedo.rgb.z);
    logInfo("   textured: %s", material->albedo.textured ? "true" : "false");
    if (material->albedo.textured) logInfo("   texture id: %d", material->albedo.texture_id);
    return true;
}

void printFileHierarchy(const umbf::Library::Node &node, int depth = 0, const acul::string &prefix = "")
{
    if (depth == 0)
        logInfo("| %s", node.name.c_str());
    else
        logInfo("%s%s %s", prefix.c_str(), (const char *)(node.isFolder ? "|----" : "|____"), node.name.c_str());

    acul::string newPrefix = prefix + (depth > 0 ? "|     " : "");

    for (size_t i = 0; i < node.children.size(); ++i)
        if (i == node.children.size() - 1)
            printFileHierarchy(node.children[i], depth + 1, prefix + (depth > 0 ? "      " : ""));
        else
            printFileHierarchy(node.children[i], depth + 1, newPrefix);
}

bool printLibrary(umbf::File *file)
{
    if (file->header.vendor_sign != UMBF_VENDOR_ID || file->header.type_sign != umbf::sign_block::format::library)
    {
        logError("Unsupported file type: %x", file->header.type_sign);
        return false;
    }
    auto it =
        std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<acul::meta::block> &block) {
            return block->signature() == umbf::sign_block::meta::library;
        });
    if (it == file->blocks.end())
    {
        logError("Failed to find library meta");
        return false;
    }
    auto library = acul::static_pointer_cast<umbf::Library>(*it);
    logInfo("------------library meta--------------");
    printFileHierarchy(library->fileTree);
    return true;
}

bool show_file(const acul::string &path)
{
    auto file = umbf::File::readFromDisk(path);
    if (!file)
    {
        logError("Failed to load file: %s", path.c_str());
        return false;
    }
    logInfo("vendor sign: %x", file->header.vendor_sign);
    logInfo("vendor version: %x", file->header.vendor_version);
    logInfo("spec version: %x", file->header.spec_version);
    logInfo("type sign: %x", file->header.type_sign);
    logInfo("compressed: %s", file->header.compressed ? "true" : "false");
    logInfo("checksum: %u", file->checksum);

    switch (file->header.type_sign)
    {
        case umbf::sign_block::format::image:
            return printImage2D(file.get());
        case umbf::sign_block::format::target:
            return printTarget(file.get());
        case umbf::sign_block::format::library:
            return printLibrary(file.get());
        case umbf::sign_block::format::scene:
            return printScene(file.get());
        case umbf::sign_block::format::material:
            return printMaterial(file.get());
        case umbf::sign_block::format::raw:
            return printRaw(file.get());
        default:
            logError("Unsupported file type: %x", file->header.type_sign);
            return false;
    }
}