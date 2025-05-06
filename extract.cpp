#include <acul/io/file.hpp>
#include <acul/io/path.hpp>
#include <acul/log.hpp>
#include <ecl/image/export.hpp>
#include <ecl/scene/obj/export.hpp>
#include <umbf/umbf.hpp>

bool extract_raw(const umbf::File *file, const acul::string &output)
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
    return acul::io::file::write_binary(output, raw_block->data, raw_block->data_size);
}

bool save_image(const acul::string &output, const umbf::Image2D &image)
{
    assert(image.pixels);
    switch (ecl::image::get_type_by_extension(acul::io::get_extension(output)))
    {
        case ecl::image::Type::BMP:
        {
            ecl::image::bmp::Params bp(image);
            return ecl::image::bmp::save(output, bp);
        }
        case ecl::image::Type::GIF:
        {
            ecl::image::gif::Params gp({image});
            return ecl::image::gif::save(output, gp);
        }
        case ecl::image::Type::HDR:
        {
            ecl::image::hdr::Params hp(image);
            return ecl::image::hdr::save(output, hp);
        }
        case ecl::image::Type::HEIF:
        {
            ecl::image::heif::Params hp(image);
            return ecl::image::heif::save(output, hp);
        }
        case ecl::image::Type::JPEG:
        {
            ecl::image::jpeg::Params jp(image);
            return ecl::image::jpeg::save(output, jp);
        }
        case ecl::image::Type::OpenEXR:
        {
            ecl::image::openEXR::Params op({image});
            return ecl::image::openEXR::save(output, op, 2);
        }
        case ecl::image::Type::PNG:
        {
            ecl::image::png::Params pp(image);
            return ecl::image::png::save(output, pp, 1);
        }
        case ecl::image::Type::Targa:
        {
            ecl::image::targa::Params tp(image);
            return ecl::image::targa::save(output, tp);
        }
        case ecl::image::Type::TIFF:
        {
            ecl::image::tiff::Params tp({image});
            return ecl::image::tiff::save(output, tp, 1);
        }
        case ecl::image::Type::WebP:
        {
            ecl::image::webp::Params wp(image);
            return ecl::image::webp::save(output, wp);
        }
        case ecl::image::Type::UMBF:
            LOG_ERROR("Can't extract to self format");
            return false;
        default:
            LOG_ERROR("Unsupported dst format: %s", output.c_str());
            return false;
    }
}

bool extract_image(umbf::File *file, const acul::string &output)
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
    return save_image(output, *image);
}

void add_texture_to_scene(const umbf::File::Header &header, umbf::File *file, acul::string &texture)
{
    if (header.vendor_sign == UMBF_VENDOR_ID && header.type_sign == umbf::sign_block::format::Target)
    {
        auto it = std::find_if(file->blocks.begin(), file->blocks.end(),
                               [](const acul::shared_ptr<acul::meta::block> &block) {
                                   return block->signature() == umbf::sign_block::meta::Target;
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
    auto extension = acul::io::get_extension(output);
    ecl::scene::IExporter *exporter;
    if (extension == ".obj")
    {
        auto *obj = acul::alloc<ecl::scene::obj::Exporter>(output);
        obj->obj_flags = ecl::scene::obj::ObjExportFlagBits::ObjectPolicyObjects;
        exporter = obj;
    }
    else
        return false;

    exporter->mesh_flags = ecl::scene::MeshExportFlagBits::ExportNormals | ecl::scene::MeshExportFlagBits::ExportUV;
    exporter->material_flags = ecl::scene::MaterialExportFlags::TextureOrigin;
    exporter->objects = scene->objects;
    exporter->materials = scene->materials;
    exporter->textures.resize(scene->textures.size());
    auto &textures = exporter->textures;
    for (size_t i = 0; i < scene->textures.size(); i++) add_texture_to_scene(scene->textures[i].header, file, textures[i]);

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
        if (acul::io::file::create_directory(str.c_str()) == acul::io::file::op_state::Error) return false;
        for (auto &child : node.children)
            if (!extract_library_node(child, path)) return false;
    }
    else
    {
        LOG_INFO("Extracting: %s", str.c_str());
        switch (node.asset.header.type_sign)
        {
            case umbf::sign_block::format::Raw:
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
    return extract_library_node(library->fileTree, output);
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
        case umbf::sign_block::format::Raw:
            ret = extract_raw(file.get(), output);
            break;
        case umbf::sign_block::format::Image:
            ret = extract_image(file.get(), output);
            break;
        case umbf::sign_block::format::Scene:
            ret = extract_scene(file.get(), output);
            break;
        case umbf::sign_block::format::Library:
            ret = extract_library(file.get(), output);
            break;
        default:
            LOG_ERROR("Unsupported file type: %x", file->header.type_sign);
            break;
    }
    if (!ret) LOG_ERROR("Failed to extract file: %s", input.c_str());
    return ret;
}