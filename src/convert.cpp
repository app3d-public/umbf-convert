#include "convert.hpp"
#include <acul/io/fs/file.hpp>
#include <acul/io/fs/path.hpp>
#include <acul/io/path.hpp>
#include <acul/log.hpp>
#include <aecl/image/import.hpp>
#include <aecl/scene/obj/import.hpp>
#include <inttypes.h>
#include <rapidjson/document.h>
#include <umbf/ext/image/utils.hpp>
#include <umbf/ext/material/material.hpp>
#include <umbf/ext/scene/scene.hpp>
#include <umbf/version.h>
#include "models/umbf.hpp"

namespace
{
    acul::string resolve_local_path(const acul::string &value, const acul::string &work_dir)
    {
        const acul::path path(value);
        if (value.empty() || path.is_absolute() || path.is_scheme_external()) return value;
        return work_dir.empty() ? value : (acul::path(work_dir) / path).str();
    }

    void create_file_structure(umbf::WriteDescriptor &file, u32 type_sign, bool compressed = false,
                               const acul::string &output = {})
    {
        if (!file.fd)
        {
            umbf::Header header;
            header.vendor_sign = UMBF_VENDOR_ID;
            header.vendor_version = UMBF_VERSION;
            header.spec_version = UMBF_VERSION;
            header.type_sign = type_sign;
            const auto result = output.empty() ? umbf::create_write_descriptor(header, file)
                                               : umbf::create_write_descriptor(output, header, file);
            if (!result.success()) throw acul::runtime_error("Failed to create UMBF write descriptor");
        }
        else if (!file.file || file.file->type_sign != type_sign)
            throw acul::runtime_error("UMBF write descriptor has an unexpected file type");
        const u32 segment_signature = !compressed                                    ? UMBF_SEGMENT_RAW
                                      : type_sign == umbf::sign_block::format::image ? UMBF_SEGMENT_COMPRESSED_IMAGE
                                                                                     : UMBF_SEGMENT_COMPRESSED;
        file.default_segment_signature = segment_signature;
    }

    bool serialize_resource(const aecl::Asset &resource, bool compressed, umbf::WriteDescriptor &file)
    {
        if (!umbf::create_write_descriptor(resource.header, file).success()) return false;
        file.default_segment_signature = !compressed ? UMBF_SEGMENT_RAW : UMBF_SEGMENT_COMPRESSED;
        umbf::SegmentWriter *object_segment = nullptr;
        if (resource.header.type_sign == umbf::sign_block::format::scene_object)
        {
            // Object description and content are consumed together; share one physical segment.
            object_segment = umbf::add_segment(file, file.default_segment_signature);
            if (!object_segment)
            {
                LOG_ERROR("Failed to create imported object segment");
                return false;
            }
        }

        for (const auto &block : resource.blocks)
        {
            if (!block)
            {
                LOG_ERROR("Failed to serialize an empty imported UMBF block");
                return false;
            }
            try
            {
                umbf::add_block(file, block.get(), object_segment);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Failed to serialize imported UMBF block 0x%08x: %s", block->signature(), e.what());
                return false;
            }
        }
        return true;
    }

    umbf::WriteDescriptor create_file_list(u32 type_sign)
    {
        umbf::WriteDescriptor list;
        create_file_structure(list, type_sign, false);
        return list;
    }

    amal::ivec2 find_min_square_atlas_size(const acul::vector<amal::irect> &rects, i32 padding)
    {
        if (rects.empty()) return {1, 1};

        i32 min_side = 1;
        for (u32 i = 0; i < rects.size(); ++i)
            min_side = amal::max(min_side, amal::max(rects[i].size.x, rects[i].size.y) + padding * 2);

        i32 max_side = min_side;
        while (true)
        {
            auto probe_rects = rects;
            const auto result =
                umbf::pack_max_rects({max_side, max_side}, 0, probe_rects, umbf::MaxRectsHeuristic::best_short_side_fit,
                                     umbf::MaxRectsTransformBits::none, padding);
            if (result.packed) break;

            if (max_side > (1 << 29)) throw acul::runtime_error("Failed to determine atlas size");
            max_side *= 2;
        }

        i32 best_side = max_side;
        while (min_side <= max_side)
        {
            const i32 mid_side = min_side + (max_side - min_side) / 2;
            auto probe_rects = rects;
            const auto result =
                umbf::pack_max_rects({mid_side, mid_side}, 0, probe_rects, umbf::MaxRectsHeuristic::best_short_side_fit,
                                     umbf::MaxRectsTransformBits::none, padding);
            if (result.packed)
            {
                best_side = mid_side;
                max_side = mid_side - 1;
            }
            else min_side = mid_side + 1;
        }

        return {best_side, best_side};
    }
} // namespace

namespace
{
    bool read_raw_file(const acul::string &input, acul::vector<char> &data)
    {
        if (!acul::fs::read_binary(input, data))
        {
            LOG_ERROR("Failed to read raw file: %s", input.c_str());
            return false;
        }
        return true;
    }

    bool convert_raw_file(const acul::string &input, umbf::WriteDescriptor &file)
    {
        acul::vector<char> data;
        if (!read_raw_file(input, data)) return false;
        umbf::add_raw_block(file, data.data(), data.size());
        return true;
    }

    umbf::Library::Node *find_or_create_folder(umbf::Library::Node &parent, const acul::string &name)
    {
        auto it = std::find_if(parent.children.begin(), parent.children.end(),
                               [&name](const umbf::Library::Node &child) { return child.name == name; });
        if (it != parent.children.end()) return &(*it);

        umbf::Library::Node folder;
        folder.name = name;
        folder.index = umbf::Library::Node::directory_index;
        parent.children.push_back(std::move(folder));
        return &parent.children.back();
    }

    bool build_raw_library_node(umbf::Library::Node &root, const acul::path &relative_path,
                                const acul::string &source_path, acul::vector<acul::vector<char>> &payloads)
    {
        umbf::Library::Node *current = &root;
        for (size_t i = 0; i < relative_path.size(); ++i)
        {
            const bool is_leaf = i + 1 == relative_path.size();
            const auto &part = *(relative_path.begin() + static_cast<ptrdiff_t>(i));
            if (!is_leaf)
            {
                current = find_or_create_folder(*current, part);
                continue;
            }

            umbf::Library::Node node;
            node.name = part;
            acul::vector<char> data;
            if (!read_raw_file(source_path, data)) return false;
            node.index = payloads.size() + 1u;
            payloads.push_back(std::move(data));
            current->children.push_back(std::move(node));
        }
        return true;
    }

    bool convert_raw_directory(const acul::string &input, bool compressed, umbf::WriteDescriptor &file,
                               const acul::string &output)
    {
        acul::vector<acul::string> files;
        auto lr = acul::fs::list_files(input, files, true);
        if (!lr.success())
        {
            LOG_ERROR("Failed to list directory: %s", input.c_str());
            return false;
        }
        if (files.empty())
        {
            LOG_ERROR("Directory is empty: %s", input.c_str());
            return false;
        }

        std::sort(files.begin(), files.end());

        const acul::path base_path(input);
        const acul::string base_str = base_path.str();
        auto library = acul::make_shared<umbf::Library>();
        library->file_tree.name = ".";
        library->file_tree.index = umbf::Library::Node::directory_index;

        acul::vector<acul::vector<char>> payloads;
        for (const auto &entry : files)
        {
            if (entry.size() <= base_str.size()) continue;
            size_t relative_offset = base_str.size();
            if (entry[relative_offset] == '/' || entry[relative_offset] == '\\') ++relative_offset;
            const acul::path relative_path(entry.substr(relative_offset));
            if (!build_raw_library_node(library->file_tree, relative_path, entry, payloads)) return false;
        }

        create_file_structure(file, umbf::sign_block::format::library, compressed, output);
        umbf::add_block(file, library.get());
        for (const auto &payload : payloads)
        {
            auto *segment = umbf::add_segment(file, compressed ? UMBF_SEGMENT_COMPRESSED : UMBF_SEGMENT_RAW);
            umbf::add_raw_block(file, payload.data(), payload.size(), segment);
        }
        return true;
    }
} // namespace

bool convert_raw(const acul::string &input, bool compressed, bool recursive, umbf::WriteDescriptor &file,
                 const acul::string &output)
{
    if (acul::fs::is_directory(input.c_str()))
    {
        if (!recursive)
        {
            LOG_ERROR("Directory input for raw conversion requires -R");
            return false;
        }
        return convert_raw_directory(input, compressed, file, output);
    }

    create_file_structure(file, umbf::sign_block::format::raw, compressed, output);
    return convert_raw_file(input, file);
}

bool convert_image(const acul::string &input, bool compressed, umbf::WriteDescriptor &file,
                   const acul::string &output)
{
    auto importer = aecl::image::get_importer_by_path(input);
    acul::vector<umbf::Image2D> images;
    umbf::Image2D *pImage = nullptr;
    bool ret = false;
    if (importer)
    {
        if (importer->load(input, images))
        {
            pImage = &images.front();
            create_file_structure(file, umbf::sign_block::format::image, compressed, output);
            umbf::add_block(file, pImage);
            ret = true;
        }
        else LOG_ERROR("AECL error: %s", importer->error().c_str());
        acul::release(importer);
    }
    return ret;
}

acul::shared_ptr<umbf::Image2D> model_to_image(const models::IPath &model, const acul::string &work_dir)
{
    const auto path = resolve_local_path(model.path(), work_dir);
    auto importer = aecl::image::get_importer_by_path(path);
    acul::vector<umbf::Image2D> images;
    acul::shared_ptr<umbf::Image2D> dst_image;
    if (importer)
    {
        if (importer->load(path, images)) dst_image = acul::make_shared<umbf::Image2D>(images.front());
        else LOG_ERROR("AECL error: %s", importer->error().c_str());
        acul::release(importer);
    }
    return dst_image;
}

bool convert_atlas(const models::Atlas &atlas, bool compressed, umbf::WriteDescriptor &file,
                   const acul::string &work_dir)
{
    create_file_structure(file, umbf::sign_block::format::image, compressed);
    auto image_block = acul::make_shared<umbf::Image2D>();
    image_block->format.bytes_per_channel = atlas.bytes_per_channel();
    image_block->format.type = atlas.type();
    image_block->channels = {"R", "G", "B", "A"};

    auto atlas_block = acul::make_shared<umbf::Atlas>();
    atlas_block->padding = 1;
    acul::vector<acul::shared_ptr<umbf::Image2D>> atlas_dst_images;
    atlas_dst_images.reserve(atlas.images().size());
    for (const auto &image : atlas.images())
    {
        auto pImage = model_to_image(*image, work_dir);
        if (!pImage)
        {
            LOG_ERROR("Failed to create image: %s", image->path().c_str());
            return false;
        }
        if (pImage->channels.size() != image_block->channels.size() || pImage->format != image_block->format)
        {
            LOG_INFO("Converting image to the atlas format: %s", image->path().c_str());
            void *converted = umbf::convert_image(*pImage, image_block->format, image_block->channels.size());
            pImage->pixels = converted;
            pImage->format = image_block->format;
        }
        atlas_dst_images.push_back(pImage);
        atlas_block->pack_data.push_back(
            {{-1, -1}, {static_cast<i32>(pImage->width), static_cast<i32>(pImage->height)}});
    }

    const amal::ivec2 atlas_size = find_min_square_atlas_size(atlas_block->pack_data, atlas_block->padding);
    image_block->width = atlas_size.x;
    image_block->height = atlas_size.y;

    if (!umbf::pack_max_rects(atlas_size, 0, atlas_block->pack_data, umbf::MaxRectsHeuristic::best_short_side_fit,
                              umbf::MaxRectsTransformBits::none, atlas_block->padding)
             .packed)
    {
        LOG_ERROR("Failed to pack atlas");
        return false;
    }
    umbf::fill_atlas_pixels(image_block, atlas_block, atlas_dst_images);

    umbf::add_block(file, image_block.get());
    umbf::add_block(file, atlas_block.get());
    return true;
}

bool convert_image(const models::Image &image, bool compressed, umbf::WriteDescriptor &file,
                   const acul::string &work_dir)
{
    if (image.signature() == umbf::sign_block::image)
    {
        auto serializer = acul::static_pointer_cast<models::IPath>(image.serializer());
        return convert_image(resolve_local_path(serializer->path(), work_dir), compressed, file, {});
    }
    else if (image.signature() == umbf::sign_block::image_atlas)
    {
        auto serializer = acul::static_pointer_cast<models::Atlas>(image.serializer());
        return convert_atlas(*serializer, compressed, file, work_dir);
    }
    LOG_ERROR("Unsupported image type: %x", image.signature());
    return false;
}

void convert_target(const models::Target &target, bool compressed, umbf::WriteDescriptor &file,
                    const acul::string &work_dir)
{
    create_file_structure(file, umbf::sign_block::format::target, compressed);
    auto block = acul::make_shared<umbf::Target>();
    block->url = resolve_local_path(target.url(), work_dir);
    block->header = target.header();
    block->checksum = target.checksum();
    block->tag = target.tag();
    umbf::add_block(file, block.get());
}

bool convert_image(const acul::shared_ptr<models::UMBFRoot> &model, bool compressed, umbf::WriteDescriptor &file,
                   const acul::string &work_dir)
{
    switch (model->type_sign)
    {
        case umbf::sign_block::format::image:
        {
            auto image_model = acul::static_pointer_cast<models::Image>(model);
            return convert_image(*image_model, compressed, file, work_dir);
        }
        case umbf::sign_block::format::target:
        {
            auto target_model = acul::static_pointer_cast<models::Target>(model);
            convert_target(*target_model, compressed, file, work_dir);
            return true;
        }
        default:
            LOG_ERROR("Unsupported texture type: %x", model->type_sign);
            return false;
    }
}

bool convert_material(const models::Material &material, bool compressed, umbf::WriteDescriptor &file,
                      const acul::string &work_dir)
{
    create_file_structure(file, umbf::sign_block::format::material, false);
    auto block = acul::make_shared<umbf::Material>();
    block->albedo = material.albedo();
    auto textures = create_file_list(umbf::sign_block::format::images_list);
    for (auto &texture : material.textures())
    {
        umbf::WriteDescriptor texture_file;
        if (convert_image(texture, compressed, texture_file, work_dir)) umbf::add_file(textures, texture_file);
        else return false;
    }
    umbf::add_block(file, block.get());
    umbf::add_file(file, textures);
    return true;
}

acul::unique_ptr<aecl::scene::ILoader> import_mesh(const acul::string &input)
{
    auto ext = acul::fs::get_extension(input);
    if (ext == ".obj")
    {
        auto obj_loader = acul::make_unique<aecl::scene::obj::Importer>(input);
        const auto result = obj_loader->load();
        if (!result.success())
        {
            LOG_ERROR("Failed to load obj '%s': %s (0x%016" PRIx64 ")", obj_loader->path().c_str(),
                      obj_loader->error().c_str(), static_cast<u64>(result));
            return nullptr;
        }
        return obj_loader;
    }
    LOG_ERROR("Unsupported mesh format: %s", ext.c_str());
    return nullptr;
}

u32 convert_scene(const acul::string &input, const acul::string &output, bool compressed)
{
    auto importer = import_mesh(input);
    if (!importer) return 0;

    umbf::WriteDescriptor file;
    create_file_structure(file, umbf::sign_block::format::scene, false, output);

    auto &object_im = importer->objects();
    auto objects_fl = create_file_list(umbf::sign_block::format::scene_objects_list);
    for (u64 index = 0u; index < object_im.size(); ++index)
    {
        const auto &resource = object_im[index];
        if (object_im[index].blocks.empty())
        {
            LOG_ERROR("Invalid scene object #%" PRIu64, index);
            return 0;
        }
        umbf::WriteDescriptor serialized;
        if (!serialize_resource(resource, compressed, serialized)) return 0;
        umbf::add_file(objects_fl, serialized);
    }

    auto &images_im = importer->images();
    auto images_fl = create_file_list(umbf::sign_block::format::images_list);
    for (u64 index = 0u; index < images_im.size(); ++index)
    {
        const auto &resource = images_im[index];
        if (images_im[index].blocks.empty())
        {
            LOG_ERROR("Invalid image #%" PRIu64, index);
            return 0;
        }
        umbf::WriteDescriptor serialized;
        if (!serialize_resource(resource, compressed, serialized)) return 0;
        umbf::add_file(images_fl, serialized);
    }

    auto &materials_im = importer->materials();
    auto materials_fl = create_file_list(umbf::sign_block::format::materials_list);
    for (u64 index = 0u; index < materials_im.size(); ++index)
    {
        const auto &resource = materials_im[index];
        if (materials_im[index].blocks.empty())
        {
            LOG_ERROR("Invalid material #%" PRIu64, index);
            return 0;
        }
        umbf::WriteDescriptor serialized;
        if (!serialize_resource(resource, compressed, serialized)) return 0;
        umbf::add_file(materials_fl, serialized);
    }
    umbf::add_file(file, objects_fl);
    umbf::add_file(file, images_fl);
    umbf::add_file(file, materials_fl);
    return umbf::save_file(file, output) ? file.file->checksum : 0;
}

bool convert_scene(models::Scene &scene, bool compressed, umbf::WriteDescriptor &file,
                   const acul::string &work_dir)
{
    create_file_structure(file, umbf::sign_block::format::scene, false);
    auto objects_fl = create_file_list(umbf::sign_block::format::scene_objects_list);
    acul::vector<acul::vector<u64>> materials_ids(scene.materials().size());
    for (auto &mesh : scene.meshes())
    {
        auto importer = import_mesh(resolve_local_path(mesh->path(), work_dir));
        if (!importer) return false;
        auto &objects_im = importer->objects();
        for (u64 index = 0u; index < objects_im.size(); ++index)
        {
            const auto &object = objects_im[index];
            if (objects_im[index].blocks.empty())
            {
                LOG_ERROR("Failed to obtain imported scene object #%" PRIu64, index);
                return false;
            }
            acul::shared_ptr<umbf::ObjectInfo> descriptor;
            for (const auto &block : object.blocks)
            {
                if (block && block->signature() == umbf::sign_block::object_info)
                {
                    descriptor = acul::static_pointer_cast<umbf::ObjectInfo>(block);
                    break;
                }
            }
            if (!descriptor)
            {
                LOG_ERROR("Imported scene object has no descriptor block");
                return false;
            }
            umbf::WriteDescriptor serialized;
            if (!serialize_resource(object, compressed, serialized)) return false;
            umbf::add_file(objects_fl, serialized);
            if (mesh->mat_id() >= 0 && static_cast<size_t>(mesh->mat_id()) < materials_ids.size())
                materials_ids[static_cast<size_t>(mesh->mat_id())].push_back(descriptor->id);
        }
    }

    auto images = create_file_list(umbf::sign_block::format::images_list);
    u32 image_count = 0u;
    for (auto &texture : scene.textures())
    {
        umbf::WriteDescriptor texture_file;
        if (convert_image(texture, compressed, texture_file, work_dir))
        {
            umbf::add_file(images, texture_file);
            ++image_count;
        }
        else return false;
    }

    auto materials = create_file_list(umbf::sign_block::format::materials_list);
    for (size_t i = 0; i < scene.materials().size(); ++i)
    {
        auto &material = scene.materials()[i];
        umbf::WriteDescriptor material_file;
        if (material.asset->type_sign == umbf::sign_block::format::material)
        {
            auto material_model = acul::static_pointer_cast<models::Material>(material.asset);
            umbf::Material data;
            data.albedo = material_model->albedo();
            if (data.albedo.textured) data.albedo.texture_id += static_cast<i16>(image_count);
            for (auto &texture : material_model->textures())
            {
                umbf::WriteDescriptor texture_file;
                if (!convert_image(texture, compressed, texture_file, work_dir)) return false;
                umbf::add_file(images, texture_file);
                ++image_count;
            }
            create_file_structure(material_file, umbf::sign_block::format::material, false);
            umbf::add_block(material_file, &data);
        }
        else if (material.asset->type_sign == umbf::sign_block::format::target)
        {
            const auto target_model = acul::static_pointer_cast<models::Target>(material.asset);
            convert_target(*target_model, false, material_file, work_dir);
        }
        else
        {
            LOG_ERROR("Unsupported material type: %x", material.asset->type_sign);
            return false;
        }
        umbf::MaterialBinding info;
        info.name = material.name;
        info.id = acul::id_gen()();
        for (auto &id : materials_ids[i]) info.assignments.push_back(id);
        umbf::add_block(material_file, &info);
        umbf::add_file(materials, material_file);
    }

    umbf::add_file(file, objects_fl);
    umbf::add_file(file, images);
    umbf::add_file(file, materials);
    return true;
}

void prepare_library_node(const models::FileNode &src, umbf::Library::Node &dst,
                          acul::vector<umbf::WriteDescriptor> &assets, const acul::string &work_dir)
{
    dst.name = src.name;
    if (src.children.empty())
    {
        if (src.is_folder) dst.index = umbf::Library::Node::directory_index;
        else
        {
            umbf::WriteDescriptor asset;
            switch (src.asset->type_sign)
            {
                case umbf::sign_block::format::image:
                    if (!convert_image(src.asset, false, asset, work_dir))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                case umbf::sign_block::format::material:
                    if (!convert_material(*acul::static_pointer_cast<models::Material>(src.asset), false, asset,
                                          work_dir))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                case umbf::sign_block::format::scene:
                    if (!convert_scene(*acul::static_pointer_cast<models::Scene>(src.asset), false, asset, work_dir))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                case umbf::sign_block::format::target:
                    convert_target(*acul::static_pointer_cast<models::Target>(src.asset), false, asset, work_dir);
                    break;
                case umbf::sign_block::format::raw:
                    create_file_structure(asset, umbf::sign_block::format::raw);
                    if (!convert_raw_file(
                            resolve_local_path(acul::static_pointer_cast<models::IPath>(src.asset)->path(), work_dir),
                            asset))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                default:
                    throw acul::runtime_error(acul::format("Unsupported asset type: %x", src.asset->type_sign));
                    break;
            }
            dst.index = assets.size() + 1u;
            assets.push_back(std::move(asset));
        }
    }
    else
    {
        dst.index = umbf::Library::Node::directory_index;
        for (const auto &child : src.children)
        {
            umbf::Library::Node node;
            prepare_library_node(child, node, assets, work_dir);
            dst.children.push_back(node);
        }
    }
}

u32 convert_library(const models::Library &library, const acul::string &output, bool compressed,
                    const acul::string &work_dir)
{
    umbf::WriteDescriptor file;
    create_file_structure(file, umbf::sign_block::format::library, false, output);
    auto block = acul::make_shared<umbf::Library>();
    acul::vector<umbf::WriteDescriptor> assets;
    prepare_library_node(library.file_tree(), block->file_tree, assets, work_dir);
    umbf::add_block(file, block.get());
    for (auto &asset : assets) umbf::add_file(file, asset);
    return umbf::save_file(file, output) ? file.file->checksum : 0;
}

u32 convert_json(const acul::string &input, const acul::string &output, bool compressed,
                 const acul::string &work_dir)
{
    rapidjson::Document json;
    models::UMBFRoot root;
    if (!root.deserialize_from_file(input, json))
    {
        LOG_ERROR("%s", root.error().c_str());
        return 0;
    }
    switch (root.type_sign)
    {
        case umbf::sign_block::format::image:
        {
            models::Image image;
            if (!image.deserialize_object(json))
            {
                LOG_ERROR("Failed to deserialize image: %s", input.c_str());
                return 0;
            }
            umbf::WriteDescriptor file;
            create_file_structure(file, umbf::sign_block::format::image, compressed, output);
            if (!convert_image(image, compressed, file, work_dir)) return 0;
            return umbf::save_file(file, output) ? file.file->checksum : 0;
        }
        case umbf::sign_block::format::material:
        {
            models::Material material;
            if (!material.deserialize_object(json))
            {
                LOG_ERROR("Failed to deserialize material: %s", input.c_str());
                return 0;
            }
            umbf::WriteDescriptor file;
            create_file_structure(file, umbf::sign_block::format::material, false, output);
            if (!convert_material(material, compressed, file, work_dir)) return 0;
            return umbf::save_file(file, output) ? file.file->checksum : 0;
        }
        case umbf::sign_block::format::scene:
        {
            models::Scene scene;
            if (!scene.deserialize_object(json))
            {
                LOG_ERROR("Failed to deserialize scene: %s", input.c_str());
                return 0;
            }
            umbf::WriteDescriptor file;
            create_file_structure(file, umbf::sign_block::format::scene, false, output);
            if (!convert_scene(scene, compressed, file, work_dir)) return 0;
            return umbf::save_file(file, output) ? file.file->checksum : 0;
        }
        case umbf::sign_block::format::target:
        {
            models::Target target;
            if (!target.deserialize_object(json))
            {
                LOG_ERROR("Failed to deserialize target: %s", input.c_str());
                return 0;
            }
            umbf::WriteDescriptor file;
            create_file_structure(file, umbf::sign_block::format::target, compressed, output);
            convert_target(target, compressed, file, work_dir);
            return umbf::save_file(file, output) ? file.file->checksum : 0;
        }
        case umbf::sign_block::format::library:
        {
            models::Library library;
            if (!library.deserialize_object(json))
            {
                LOG_ERROR("Failed to deserialize library: %s", input.c_str());
                return 0;
            }
            return convert_library(library, output, compressed, work_dir);
        }
        default:
            LOG_ERROR("Unsupported type: %x", root.type_sign);
            return 0;
    }
}
