#include <acul/io/fs/file.hpp>
#include <acul/io/fs/path.hpp>
#include <acul/io/path.hpp>
#include <acul/log.hpp>
#include <aecl/image/export.hpp>
#include <aecl/scene/obj/export.hpp>
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

    aecl::Asset read_resource(const umbf::ReadDescriptor &source, const umbf::Header &header,
                              const umbf::BlockRange &range)
    {
        aecl::Asset destination;
        destination.header = header;
        auto block = umbf::BlockIterator(source, range.first_block);
        for (u32 index = 0u; index < range.block_count && block != source.end(); ++index, ++block)
            destination.blocks.push_back(umbf::get_block(block));
        return destination;
    }
} // namespace

bool extract_raw(const umbf::ReadDescriptor &file, const acul::string &output)
{
    auto block = file.begin();
    while (block != file.end() && block->signature != umbf::sign_block::raw) ++block;
    if (block == file.end())
    {
        LOG_ERROR("raw payload not found");
        return false;
    }
    return acul::fs::write_binary(output, block->data, block->size);
}

bool save_image(const acul::string &output, const umbf::Image2D &image)
{
    assert(image.pixels);
    switch (aecl::image::get_type_by_extension(acul::fs::get_extension(output)))
    {
        case aecl::image::Type::bmp:
        {
            aecl::image::bmp::Params bp(image);
            return aecl::image::bmp::save(output, bp);
        }
        case aecl::image::Type::gif:
        {
            aecl::image::gif::Params gp({image});
            return aecl::image::gif::save(output, gp);
        }
        case aecl::image::Type::hdr:
        {
            aecl::image::hdr::Params hp(image);
            return aecl::image::hdr::save(output, hp);
        }
        case aecl::image::Type::heif:
        {
            aecl::image::heif::Params hp(image);
            return aecl::image::heif::save(output, hp);
        }
        case aecl::image::Type::jpeg:
        {
            aecl::image::jpeg::Params jp(image);
            return aecl::image::jpeg::save(output, jp);
        }
        case aecl::image::Type::openexr:
        {
            aecl::image::openexr::Params op({image});
            return aecl::image::openexr::save(output, op, 2);
        }
        case aecl::image::Type::png:
        {
            aecl::image::png::Params pp(image);
            return aecl::image::png::save(output, pp, 1);
        }
        case aecl::image::Type::targa:
        {
            aecl::image::targa::Params tp(image);
            return aecl::image::targa::save(output, tp);
        }
        case aecl::image::Type::tiff:
        {
            aecl::image::tiff::Params tp({image});
            return aecl::image::tiff::save(output, tp, 1);
        }
        case aecl::image::Type::webp:
        {
            aecl::image::webp::Params wp(image);
            return aecl::image::webp::save(output, wp);
        }
        case aecl::image::Type::umbf:
            LOG_ERROR("can't extract to self format");
            return false;
        default:
            LOG_ERROR("unsupported dst format: %s", output.c_str());
            return false;
    }
}

bool extract_image(const umbf::ReadDescriptor &file, const acul::string &output)
{
    auto image = find_block<umbf::Image2D>(file, umbf::sign_block::image);
    if (!image)
    {
        LOG_ERROR("failed to find image meta");
        return false;
    }
    return save_image(output, *image);
}

bool extract_scene(const umbf::ReadDescriptor &file, const acul::string &output)
{
    auto objects = find_block<umbf::ObjectList>(file, umbf::sign_block::scene_objects_list);
    auto images = find_block<umbf::ImageList>(file, umbf::sign_block::images_list);
    auto materials = find_block<umbf::MaterialList>(file, umbf::sign_block::materials_list);
    if (!objects || !images || !materials)
    {
        LOG_ERROR("failed to find scene meta");
        return false;
    }
    auto extension = acul::fs::get_extension(output);
    aecl::scene::IExporter *exporter;
    if (extension == ".obj")
    {
        auto *obj = acul::alloc<aecl::scene::obj::Exporter>(output);
        obj->obj_flags = aecl::scene::obj::ObjExportFlagBits::object_policy_objects;
        exporter = obj;
    }
    else
    {
        return false;
    }

    exporter->mesh_flags = aecl::scene::MeshExportFlagBits::export_normals | aecl::scene::MeshExportFlagBits::export_uv;
    exporter->material_flags = aecl::scene::MaterialExportFlags::texture_origin;
    const umbf::Header object_header{file.file->vendor_sign, file.file->vendor_version, file.file->spec_version,
                                     umbf::sign_block::format::none};
    for (const auto &entry : objects->objects)
    {
        auto data = read_resource(file, object_header, entry);
        if (data.blocks.empty()) continue;
        exporter->objects.push_back(std::move(data));
    }

    exporter->textures.reserve(images->images.size());
    for (const auto &entry : images->images)
        exporter->textures.push_back(read_resource(file, entry.header, entry.blocks));

    for (const auto &entry : materials->materials)
        exporter->materials.push_back(read_resource(file, entry.header, entry.blocks));

    bool success = exporter->save();
    if (!success) LOG_ERROR("failed to export scene: %s", exporter->error().c_str());
    acul::release(exporter);
    return success;
}

bool extract_library_node(umbf::Library::Node &node, const acul::path &parent, const umbf::ReadDescriptor &owner)
{
    acul::path path = parent / node.name;
    acul::string str = path.str();
    if (node.is_directory())
    {
        LOG_INFO("creating directory: %s", str.c_str());
        auto cdr = acul::fs::create_directory(str.c_str());
        if (cdr.state != ACUL_OP_SUCCESS)
        {
            LOG_ERROR("failed to create directory. Error code: 0x%" PRIx64, static_cast<u64>(cdr));
            return false;
        }
        for (auto &child : node.children)
            if (!extract_library_node(child, path, owner)) return false;
    }
    else
    {
        LOG_INFO("extracting: %s", str.c_str());
        umbf::BlockIterator block(owner, node.index);
        if (!block) return false;
        switch (block->signature)
        {
            case umbf::sign_block::raw:
                if (!acul::fs::write_binary(str, block->data, block->size)) return false;
                break;
            default:
            {
                if (!acul::fs::write_binary(str, block->data, block->size))
                {
                    LOG_ERROR("failed to save file: %s", str.c_str());
                    return false;
                }
            }
        }
    }
    return true;
}

bool extract_library(const umbf::ReadDescriptor &file, const acul::string &output)
{
    auto library = find_block<umbf::Library>(file, umbf::sign_block::library);
    if (!library)
    {
        LOG_ERROR("failed to find library meta");
        return false;
    }
    if (library->file_tree.name.empty() || library->file_tree.name == ".")
    {
        for (auto &child : library->file_tree.children)
            if (!extract_library_node(child, output, file)) return false;
        return true;
    }
    return extract_library_node(library->file_tree, output, file);
}

bool extract_file(const acul::string &input, const acul::string &output)
{
    umbf::ReadDescriptor file;
    auto res = umbf::create_read_descriptor(input, file);
    if (!res.success())
    {
        LOG_ERROR("failed to load file. Error code: 0x%" PRIx64, static_cast<u64>(res));
        return false;
    }
    bool ret = false;
    switch (file.file->type_sign)
    {
        case umbf::sign_block::format::raw:
            ret = extract_raw(file, output);
            break;
        case umbf::sign_block::format::image:
            ret = extract_image(file, output);
            break;
        case umbf::sign_block::format::scene:
            ret = extract_scene(file, output);
            break;
        case umbf::sign_block::format::library:
            ret = extract_library(file, output);
            break;
        default:
            LOG_ERROR("unsupported file type: %x", file.file->type_sign);
            break;
    }
    umbf::close_read_descriptor(file);
    if (!ret) LOG_ERROR("failed to extract file: %s", input.c_str());
    return ret;
}
