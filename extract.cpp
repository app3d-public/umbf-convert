#include <acul/io/file.hpp>
#include <acul/io/path.hpp>
#include <acul/log.hpp>
#include <aecl/image/export.hpp>
#include <aecl/scene/obj/export.hpp>
#include <umbf/umbf.hpp>

bool extract_raw(const umbf::File *file, const acul::string &output)
{
    if (file->blocks.empty())
    {
        LOG_ERROR("Meta block list is empty");
        return false;
    }
    auto &block = file->blocks.front();
    if (block->signature() != umbf::sign_block::raw)
    {
        LOG_ERROR("Wrong block signature: %x. For Raw block expected raw_block.", block->signature());
        return false;
    }
    auto raw_block = acul::dynamic_pointer_cast<umbf::RawBlock>(block);
    if (!raw_block)
    {
        LOG_ERROR("Failed to cast block to RawBlock");
        return false;
    }
    return acul::io::file::write_binary(output, raw_block->data, raw_block->data_size);
}

bool save_image(const acul::string &output, const umbf::Image2D &image)
{
    assert(image.pixels);
    switch (aecl::image::get_type_by_extension(acul::io::get_extension(output)))
    {
        case aecl::image::Type::BMP:
        {
            aecl::image::bmp::Params bp(image);
            return aecl::image::bmp::save(output, bp);
        }
        case aecl::image::Type::GIF:
        {
            aecl::image::gif::Params gp({image});
            return aecl::image::gif::save(output, gp);
        }
        case aecl::image::Type::HDR:
        {
            aecl::image::hdr::Params hp(image);
            return aecl::image::hdr::save(output, hp);
        }
        case aecl::image::Type::HEIF:
        {
            aecl::image::heif::Params hp(image);
            return aecl::image::heif::save(output, hp);
        }
        case aecl::image::Type::JPEG:
        {
            aecl::image::jpeg::Params jp(image);
            return aecl::image::jpeg::save(output, jp);
        }
        case aecl::image::Type::OpenEXR:
        {
            aecl::image::openEXR::Params op({image});
            return aecl::image::openEXR::save(output, op, 2);
        }
        case aecl::image::Type::PNG:
        {
            aecl::image::png::Params pp(image);
            return aecl::image::png::save(output, pp, 1);
        }
        case aecl::image::Type::Targa:
        {
            aecl::image::targa::Params tp(image);
            return aecl::image::targa::save(output, tp);
        }
        case aecl::image::Type::TIFF:
        {
            aecl::image::tiff::Params tp({image});
            return aecl::image::tiff::save(output, tp, 1);
        }
        case aecl::image::Type::WebP:
        {
            aecl::image::webp::Params wp(image);
            return aecl::image::webp::save(output, wp);
        }
        case aecl::image::Type::UMBF:
            LOG_ERROR("Can't extract to self format");
            return false;
        default:
            LOG_ERROR("Unsupported dst format: %s", output.c_str());
            return false;
    }
}

bool extract_image(umbf::File *file, const acul::string &output)
{
    auto it = std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<umbf::Block> &block) {
        return block->signature() == umbf::sign_block::image;
    });
    if (it == file->blocks.end())
    {
        LOG_ERROR("Failed to find image meta");
        return false;
    }
    auto image = acul::static_pointer_cast<umbf::Image2D>(*it);
    return save_image(output, *image);
}

void add_texture_to_scene(const umbf::File::Header &header, umbf::File *file, acul::string &texture)
{
    if (header.vendor_sign == UMBF_VENDOR_ID && header.type_sign == umbf::sign_block::format::target)
    {
        auto it =
            std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<umbf::Block> &block) {
                return block->signature() == umbf::sign_block::target;
            });
        if (it == file->blocks.end())
        {
            LOG_ERROR("Failed to find target meta");
            texture = "undefined";
        }
        else
        {
            auto target = acul::static_pointer_cast<umbf::Target>(*it);
            acul::io::path url(target->url);
            if (url.scheme() == "file")
                texture = url.str();
            else
            {
                LOG_ERROR("Only file scheme supported. Recieved: %s", url.scheme().c_str());
                texture = "undefined";
            }
        }
    }
    else
    {
        texture = "undefined";
        LOG_WARN("Embedded texture not supported. Recieved type: %x", header.type_sign);
    }
}

bool extract_scene(umbf::File *file, const acul::string &output)
{
    auto it = std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<umbf::Block> &block) {
        return block->signature() == umbf::sign_block::scene;
    });
    if (it == file->blocks.end())
    {
        LOG_ERROR("Failed to find scene meta");
        return false;
    }
    auto scene = acul::static_pointer_cast<umbf::Scene>(*it);
    auto extension = acul::io::get_extension(output);
    aecl::scene::IExporter *exporter;
    if (extension == ".obj")
    {
        auto *obj = acul::alloc<aecl::scene::obj::Exporter>(output);
        obj->obj_flags = aecl::scene::obj::ObjExportFlagBits::ObjectPolicyObjects;
        exporter = obj;
    }
    else
        return false;

    exporter->mesh_flags = aecl::scene::MeshExportFlagBits::ExportNormals | aecl::scene::MeshExportFlagBits::ExportUV;
    exporter->material_flags = aecl::scene::MaterialExportFlags::TextureOrigin;
    exporter->objects = scene->objects;
    exporter->materials = scene->materials;
    exporter->textures.resize(scene->textures.size());
    auto &textures = exporter->textures;
    for (size_t i = 0; i < scene->textures.size(); i++)
        add_texture_to_scene(scene->textures[i].header, file, textures[i]);

    bool success = exporter->save();
    acul::release(exporter);
    return success;
}

bool extract_library_node(umbf::Library::Node &node, const acul::io::path &parent)
{
    acul::io::path path = parent / node.name;
    acul::string str = path.str();
    if (node.is_folder)
    {
        LOG_INFO("Creating directory: %s", str.c_str());
        if (acul::io::file::create_directory(str.c_str()) == acul::io::file::op_state::error) return false;
        for (auto &child : node.children)
            if (!extract_library_node(child, path)) return false;
    }
    else
    {
        LOG_INFO("Extracting: %s", str.c_str());
        switch (node.asset.header.type_sign)
        {
            case umbf::sign_block::format::raw:
                if (!extract_raw(&node.asset, str)) return false;
                break;
            default:
                if (!node.asset.save(str))
                {
                    LOG_ERROR("Failed to save file: %s", str.c_str());
                    return false;
                }
        }
    }
    return true;
}

bool extract_library(umbf::File *file, const acul::string &output)
{
    auto it = std::find_if(file->blocks.begin(), file->blocks.end(), [](const acul::shared_ptr<umbf::Block> &block) {
        return block->signature() == umbf::sign_block::library;
    });
    if (it == file->blocks.end())
    {
        LOG_ERROR("Failed to find library meta");
        return false;
    }
    auto library = acul::static_pointer_cast<umbf::Library>(*it);
    return extract_library_node(library->file_tree, output);
}

bool extract_file(const acul::string &input, const acul::string &output)
{
    auto file = umbf::File::read_from_disk(input);
    if (!file)
    {
        LOG_ERROR("Failed to read file: %s", input.c_str());
        return false;
    }
    bool ret = false;
    switch (file->header.type_sign)
    {
        case umbf::sign_block::format::raw:
            ret = extract_raw(file.get(), output);
            break;
        case umbf::sign_block::format::image:
            ret = extract_image(file.get(), output);
            break;
        case umbf::sign_block::format::scene:
            ret = extract_scene(file.get(), output);
            break;
        case umbf::sign_block::format::library:
            ret = extract_library(file.get(), output);
            break;
        default:
            LOG_ERROR("Unsupported file type: %x", file->header.type_sign);
            break;
    }
    if (!ret) LOG_ERROR("Failed to extract file: %s", input.c_str());
    return ret;
}