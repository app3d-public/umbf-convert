#include <acul/io/file.hpp>
#include <acul/io/path.hpp>
#include <acul/log.hpp>
#include <assets/asset.hpp>
#include <ecl/image/export.hpp>
#include <ecl/scene/obj/export.hpp>

bool extractRaw(const umbf::File *file, const acul::string &output)
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
    return acul::io::file::write_binary(output, raw_block->data, raw_block->dataSize);
}

bool saveImage(const acul::string &output, const umbf::Image2D &image)
{
    switch (ecl::image::getTypeByExt(acul::io::get_extension(output)))
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
        case ecl::image::Type::JPEG2000:
        {
            ecl::image::jpeg2000::Params jp(image);
            return ecl::image::jpeg2000::save(output, jp, 1);
        }
        case ecl::image::Type::JPEGXL:
        {
            ecl::image::jpegXL::Params jp(image);
            return ecl::image::jpegXL::save(output, jp, 1);
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
            return ecl::image::targa::save(output, tp, 1);
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
            logError("Can't extract to self format");
            return false;
        default:
            logError("Unsupported dst format: %s", output.c_str());
            return false;
    }
}

bool extractImage(umbf::File *file, const acul::string &output)
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
    return saveImage(output, *image);
}

void addTextureToScene(const umbf::File::Header &header, umbf::File *file, acul::string &texture)
{
    if (header.vendor_sign == UMBF_VENDOR_ID && header.type_sign == umbf::sign_block::format::target)
    {
        auto it = std::find_if(file->blocks.begin(), file->blocks.end(),
                               [](const acul::shared_ptr<acul::meta::block> &block) {
                                   return block->signature() == umbf::sign_block::meta::target;
                               });
        if (it == file->blocks.end())
        {
            logError("Failed to find target meta");
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
                logError("Only file scheme supported. Recieved: %s", url.scheme().c_str());
                texture = "undefined";
            }
        }
    }
    else
    {
        texture = "undefined";
        logWarn("Embedded texture not supported. Recieved type: %x", header.type_sign);
    }
}

bool extractScene(umbf::File *file, const acul::string &output)
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
    auto extension = acul::io::get_extension(output);
    ecl::scene::IExporter *exporter;
    if (extension == ".obj")
    {
        auto *obj = acul::alloc<ecl::scene::obj::Exporter>(output);
        obj->objFlags = ecl::scene::obj::ObjExportFlagBits::mgp_objects;
        exporter = obj;
    }
    else
        return false;

    exporter->meshFlags = ecl::scene::MeshExportFlagBits::export_normals | ecl::scene::MeshExportFlagBits::export_uv;
    exporter->materialFlags = ecl::scene::MaterialExportFlags::texture_origin;
    exporter->objects = scene->objects;
    exporter->materials = scene->materials;
    exporter->textures.resize(scene->textures.size());
    auto &textures = exporter->textures;
    for (size_t i = 0; i < scene->textures.size(); i++) addTextureToScene(scene->textures[i].header, file, textures[i]);

    bool success = exporter->save();
    acul::release(exporter);
    return success;
}

bool extractLibraryNode(umbf::Library::Node &node, const acul::io::path &parent)
{
    acul::io::path path = parent / node.name;
    acul::string str = path.str();
    if (node.isFolder)
    {
        logInfo("Creating directory: %s", str.c_str());
        if (acul::io::file::create_directory(str.c_str()) == acul::io::file::op_state::error) return false;
        for (auto &child : node.children)
            if (!extractLibraryNode(child, path)) return false;
    }
    else
    {
        logInfo("Extracting: %s", str.c_str());
        switch (node.asset.header.type_sign)
        {
            case umbf::sign_block::format::raw:
                if (!extractRaw(&node.asset, str)) return false;
                break;
            default:
                if (!node.asset.save(str))
                {
                    logError("Failed to save file: %s", str.c_str());
                    return false;
                }
        }
    }
    return true;
}

bool extractLibrary(umbf::File *file, const acul::string &output)
{
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
    return extractLibraryNode(library->fileTree, output);
}

bool extractFile(const acul::string &input, const acul::string &output)
{
    auto file = umbf::File::readFromDisk(input);
    if (!file)
    {
        logError("Failed to read file: %s", input.c_str());
        return false;
    }
    bool ret = false;
    switch (file->header.type_sign)
    {
        case umbf::sign_block::format::raw:
            ret = extractRaw(file.get(), output);
            break;
        case umbf::sign_block::format::image:
            ret = extractImage(file.get(), output);
            break;
        case umbf::sign_block::format::scene:
            ret = extractScene(file.get(), output);
            break;
        case umbf::sign_block::format::library:
            ret = extractLibrary(file.get(), output);
            break;
        default:
            logError("Unsupported file type: %x", file->header.type_sign);
            break;
    }

    return ret;
}