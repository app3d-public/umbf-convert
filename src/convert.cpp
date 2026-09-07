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
        umbf::SegmentWriter *resource_segment = nullptr;
        if (resource.header.type_sign == umbf::sign_block::format::none)
        {
            // Object description and content are consumed together; share one physical segment.
            resource_segment = umbf::add_segment(file, file.default_segment_signature);
            if (!resource_segment)
            {
                LOG_ERROR("failed to create imported object segment");
                return false;
            }
        }

        for (const auto &block : resource.blocks)
        {
            if (!block)
            {
                LOG_ERROR("failed to serialize an empty imported umbf block");
                return false;
            }
            try
            {
                auto *segment = resource_segment;
                if (block->signature() == umbf::sign_block::image)
                    segment = umbf::add_segment(file, compressed ? UMBF_SEGMENT_COMPRESSED_IMAGE : UMBF_SEGMENT_RAW);
                umbf::add_block(file, block.get(), segment);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("failed to serialize imported umbf block: signature=0x%08x error=%s", block->signature(),
                          e.what());
                return false;
            }
        }
        return true;
    }

    bool append_resource(aecl::Asset &destination, aecl::Asset &resource, umbf::BlockRange &range)
    {
        if (resource.blocks.size() > UINT32_MAX) return false;
        range.first_block = destination.blocks.size();
        range.block_count = static_cast<u32>(resource.blocks.size());
        destination.blocks.insert(destination.blocks.end(), std::make_move_iterator(resource.blocks.begin()),
                                  std::make_move_iterator(resource.blocks.end()));
        resource.blocks.clear();
        return true;
    }

    bool write_scene_asset(aecl::Asset &asset, umbf::WriteDescriptor &file, bool compressed)
    {
        acul::vector<u64> groups(asset.blocks.size(), UINT64_MAX);
        u64 next_group = 0u;
        const auto add_range = [&](const umbf::BlockRange &range) {
            if (range.first_block > groups.size() || range.block_count > groups.size() - range.first_block)
                return false;
            const u64 group = next_group++;
            for (u64 index = range.first_block; index < range.first_block + range.block_count; ++index)
            {
                if (groups[index] != UINT64_MAX) return false;
                groups[index] = group;
            }
            return true;
        };
        for (const auto &block : asset.blocks)
        {
            if (!block) continue;
            if (block->signature() == umbf::sign_block::scene_objects_list)
            {
                for (const auto &range : static_cast<const umbf::ObjectList *>(block.get())->objects)
                    if (!add_range(range)) return false;
            }
            else if (block->signature() == umbf::sign_block::materials_list)
            {
                for (const auto &entry : static_cast<const umbf::MaterialList *>(block.get())->materials)
                    if (!add_range(entry.blocks)) return false;
            }
            else if (block->signature() == umbf::sign_block::images_list)
            {
                for (const auto &entry : static_cast<const umbf::ImageList *>(block.get())->images)
                    if (!add_range(entry.blocks)) return false;
            }
        }

        umbf::SegmentWriter *segment = nullptr;
        u64 active_group = UINT64_MAX;
        for (size_t index = 0u; index < asset.blocks.size(); ++index)
        {
            auto &block = asset.blocks[index];
            if (!block) return false;
            if (groups[index] != active_group)
            {
                segment = nullptr;
                active_group = groups[index];
            }
            if (block->signature() == umbf::sign_block::image)
            {
                segment = umbf::add_segment(file, compressed ? UMBF_SEGMENT_COMPRESSED_IMAGE : UMBF_SEGMENT_RAW);
                if (!segment) return false;
                umbf::add_block(file, block.get(), segment);
                segment = nullptr;
                continue;
            }
            if (!segment) segment = umbf::add_segment(file, compressed ? UMBF_SEGMENT_COMPRESSED : UMBF_SEGMENT_RAW);
            if (!segment) return false;
            umbf::add_block(file, block.get(), segment);
        }
        return true;
    }

    bool append_scene_resources(aecl::Asset &scene, acul::vector<aecl::Asset> &objects,
                                acul::vector<aecl::Asset> &materials, acul::vector<aecl::Asset> &images)
    {
        auto object_list = acul::make_unique<umbf::ObjectList>();
        auto *object_list_ptr = object_list.get();
        scene.blocks.push_back(acul::unique_ptr<umbf::Block>(std::move(object_list)));
        object_list_ptr->objects.resize(objects.size());
        for (size_t index = 0u; index < objects.size(); ++index)
            if (!append_resource(scene, objects[index], object_list_ptr->objects[index])) return false;

        auto material_list = acul::make_unique<umbf::MaterialList>();
        auto *material_list_ptr = material_list.get();
        scene.blocks.push_back(acul::unique_ptr<umbf::Block>(std::move(material_list)));
        material_list_ptr->materials.resize(materials.size());
        for (size_t index = 0u; index < materials.size(); ++index)
        {
            auto &entry = material_list_ptr->materials[index];
            entry.header = materials[index].header;
            if (!append_resource(scene, materials[index], entry.blocks)) return false;
        }

        auto image_list = acul::make_unique<umbf::ImageList>();
        auto *image_list_ptr = image_list.get();
        scene.blocks.push_back(acul::unique_ptr<umbf::Block>(std::move(image_list)));
        image_list_ptr->images.resize(images.size());
        for (size_t index = 0u; index < images.size(); ++index)
        {
            auto &entry = image_list_ptr->images[index];
            entry.header = images[index].header;
            if (!append_resource(scene, images[index], entry.blocks)) return false;
        }
        return true;
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
            LOG_ERROR("failed to read raw file: %s", input.c_str());
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
            LOG_ERROR("failed to list directory: %s", input.c_str());
            return false;
        }
        if (files.empty())
        {
            LOG_ERROR("directory is empty: %s", input.c_str());
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
            LOG_ERROR("directory input for raw conversion requires -R");
            return false;
        }
        return convert_raw_directory(input, compressed, file, output);
    }

    create_file_structure(file, umbf::sign_block::format::raw, compressed, output);
    return convert_raw_file(input, file);
}

namespace
{
    bool make_image_asset(const acul::string &input, aecl::Asset &asset)
    {
        auto importer = aecl::image::get_importer_by_path(input);
        if (!importer) return false;
        acul::vector<umbf::Image2D> images;
        const bool loaded = importer->load(input, images);
        if (!loaded) LOG_ERROR("aecl error: %s", importer->error().c_str());
        acul::release(importer);
        if (!loaded || images.empty()) return false;
        aecl::create_asset_structure(asset, umbf::sign_block::format::image);
        asset.blocks.push_back(acul::make_unique<umbf::Image2D>(std::move(images.front())));
        return true;
    }
} // namespace

bool convert_image(const acul::string &input, bool compressed, umbf::WriteDescriptor &file, const acul::string &)
{
    aecl::Asset asset;
    return make_image_asset(input, asset) && serialize_resource(asset, compressed, file);
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
        else LOG_ERROR("aecl error: %s", importer->error().c_str());
        acul::release(importer);
    }
    return dst_image;
}

bool make_atlas_asset(const models::Atlas &atlas, aecl::Asset &asset, const acul::string &work_dir)
{
    aecl::create_asset_structure(asset, umbf::sign_block::format::image);
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
            LOG_ERROR("failed to create image: %s", image->path().c_str());
            return false;
        }
        if (pImage->channels.size() != image_block->channels.size() || pImage->format != image_block->format)
        {
            LOG_INFO("converting image to the atlas format: %s", image->path().c_str());
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
        LOG_ERROR("failed to pack atlas");
        return false;
    }
    umbf::fill_atlas_pixels(image_block, atlas_block, atlas_dst_images);

    asset.blocks.push_back(acul::make_unique<umbf::Image2D>(std::move(*image_block)));
    asset.blocks.push_back(acul::make_unique<umbf::Atlas>(std::move(*atlas_block)));
    return true;
}

bool convert_atlas(const models::Atlas &atlas, bool compressed, umbf::WriteDescriptor &file,
                   const acul::string &work_dir)
{
    aecl::Asset asset;
    return make_atlas_asset(atlas, asset, work_dir) && serialize_resource(asset, compressed, file);
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
    LOG_ERROR("unsupported image type: %x", image.signature());
    return false;
}

void convert_target(const models::Target &target, bool compressed, umbf::WriteDescriptor &file,
                    const acul::string &work_dir)
{
    aecl::Asset asset;
    aecl::create_asset_structure(asset, umbf::sign_block::format::target);
    auto block = acul::make_unique<umbf::Target>();
    block->url = resolve_local_path(target.url(), work_dir);
    block->header = target.header();
    block->checksum = target.checksum();
    block->tag = target.tag();
    asset.blocks.push_back(std::move(block));
    if (!serialize_resource(asset, compressed, file)) throw acul::runtime_error("Failed to serialize target resource");
}

bool make_image_asset(const acul::shared_ptr<models::UMBFRoot> &model, aecl::Asset &asset, const acul::string &work_dir)
{
    switch (model->type_sign)
    {
        case umbf::sign_block::format::image:
        {
            auto image_model = acul::static_pointer_cast<models::Image>(model);
            if (image_model->signature() == umbf::sign_block::image)
            {
                auto serializer = acul::static_pointer_cast<models::IPath>(image_model->serializer());
                return make_image_asset(resolve_local_path(serializer->path(), work_dir), asset);
            }
            if (image_model->signature() == umbf::sign_block::image_atlas)
            {
                auto serializer = acul::static_pointer_cast<models::Atlas>(image_model->serializer());
                return make_atlas_asset(*serializer, asset, work_dir);
            }
            return false;
        }
        case umbf::sign_block::format::target:
        {
            auto target = acul::static_pointer_cast<models::Target>(model);
            aecl::create_asset_structure(asset, umbf::sign_block::format::target);
            auto block = acul::make_unique<umbf::Target>();
            block->url = resolve_local_path(target->url(), work_dir);
            block->header = target->header();
            block->checksum = target->checksum();
            block->tag = target->tag();
            asset.blocks.push_back(std::move(block));
            return true;
        }
        default:
            return false;
    }
}

bool convert_image(const acul::shared_ptr<models::UMBFRoot> &model, bool compressed, umbf::WriteDescriptor &file,
                   const acul::string &work_dir)
{
    aecl::Asset asset;
    if (!make_image_asset(model, asset, work_dir))
    {
        LOG_ERROR("unsupported texture type: %x", model->type_sign);
        return false;
    }
    return serialize_resource(asset, compressed, file);
}

bool convert_material(const models::Material &material, bool compressed, umbf::WriteDescriptor &file,
                      const acul::string &work_dir)
{
    aecl::Asset asset;
    aecl::create_asset_structure(asset, umbf::sign_block::format::material);
    auto block = acul::make_unique<umbf::Material>();
    block->albedo = material.albedo();
    asset.blocks.push_back(std::move(block));
    acul::vector<aecl::Asset> textures;
    textures.reserve(material.textures().size());
    for (auto &texture : material.textures())
    {
        textures.emplace_back();
        if (!make_image_asset(texture, textures.back(), work_dir)) return false;
    }
    auto images = acul::make_unique<umbf::ImageList>();
    auto *images_ptr = images.get();
    asset.blocks.push_back(std::move(images));
    images_ptr->images.resize(textures.size());
    for (size_t index = 0u; index < textures.size(); ++index)
    {
        images_ptr->images[index].header = textures[index].header;
        if (!append_resource(asset, textures[index], images_ptr->images[index].blocks)) return false;
    }
    if (!umbf::create_write_descriptor(asset.header, file).success()) return false;
    return write_scene_asset(asset, file, compressed);
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
            LOG_ERROR("failed to load obj: path='%s' error='%s' result=0x%" PRIx64, obj_loader->path().c_str(),
                      obj_loader->error().c_str(), static_cast<u64>(result));
            return nullptr;
        }
        return obj_loader;
    }
    LOG_ERROR("unsupported mesh format: %s", ext.c_str());
    return nullptr;
}

u32 convert_scene(const acul::string &input, const acul::string &output, bool compressed)
{
    auto importer = import_mesh(input);
    if (!importer) return 0;

    umbf::WriteDescriptor file;
    create_file_structure(file, umbf::sign_block::format::scene, false, output);
    auto &objects = importer->objects();
    auto &images = importer->images();
    auto &materials = importer->materials();
    aecl::Asset scene;
    aecl::create_asset_structure(scene, umbf::sign_block::format::scene);
    if (!append_scene_resources(scene, objects, materials, images) || !write_scene_asset(scene, file, compressed))
        return 0;
    return umbf::save_file(file, output) ? file.file->checksum : 0;
}

bool convert_scene(models::Scene &scene, bool compressed, umbf::WriteDescriptor &file, const acul::string &work_dir)
{
    aecl::Asset scene_asset;
    aecl::create_asset_structure(scene_asset, umbf::sign_block::format::scene);
    acul::vector<aecl::Asset> objects;
    acul::vector<aecl::Asset> images;
    acul::vector<aecl::Asset> materials;
    acul::vector<acul::vector<u64>> materials_ids(scene.materials().size());
    for (auto &mesh : scene.meshes())
    {
        auto importer = import_mesh(resolve_local_path(mesh->path(), work_dir));
        if (!importer) return false;
        auto &objects_im = importer->objects();
        for (u64 index = 0u; index < objects_im.size(); ++index)
        {
            auto &object = objects_im[index];
            if (object.blocks.empty())
            {
                LOG_ERROR("failed to obtain imported scene object: #%" PRIu64, index);
                return false;
            }
            const auto *descriptor =
                static_cast<const umbf::ObjectInfo *>(umbf::find_block(object.blocks, umbf::sign_block::object_info));
            if (!descriptor)
            {
                LOG_ERROR("imported scene object has no descriptor block");
                return false;
            }
            if (mesh->mat_id() >= 0 && static_cast<size_t>(mesh->mat_id()) < materials_ids.size())
                materials_ids[static_cast<size_t>(mesh->mat_id())].push_back(descriptor->id);
            objects.push_back(std::move(object));
        }
    }

    u32 image_count = 0u;
    for (auto &texture : scene.textures())
    {
        images.emplace_back();
        if (!make_image_asset(texture, images.back(), work_dir)) return false;
        ++image_count;
    }

    materials.reserve(scene.materials().size());
    for (size_t i = 0; i < scene.materials().size(); ++i)
    {
        auto &material = scene.materials()[i];
        materials.emplace_back();
        auto &material_asset = materials.back();
        if (material.asset->type_sign == umbf::sign_block::format::material)
        {
            auto material_model = acul::static_pointer_cast<models::Material>(material.asset);
            aecl::create_asset_structure(material_asset, umbf::sign_block::format::material);
            auto data = acul::make_unique<umbf::Material>();
            data->albedo = material_model->albedo();
            if (data->albedo.textured) data->albedo.texture_id += static_cast<i16>(image_count);
            for (auto &texture : material_model->textures())
            {
                images.emplace_back();
                if (!make_image_asset(texture, images.back(), work_dir)) return false;
                ++image_count;
            }
            material_asset.blocks.push_back(std::move(data));
        }
        else if (material.asset->type_sign == umbf::sign_block::format::target)
        {
            const auto target_model = acul::static_pointer_cast<models::Target>(material.asset);
            aecl::create_asset_structure(material_asset, umbf::sign_block::format::target);
            auto target = acul::make_unique<umbf::Target>();
            target->url = resolve_local_path(target_model->url(), work_dir);
            target->header = target_model->header();
            target->checksum = target_model->checksum();
            target->tag = target_model->tag();
            material_asset.blocks.push_back(std::move(target));
        }
        else
        {
            LOG_ERROR("unsupported material type: %x", material.asset->type_sign);
            return false;
        }
        auto info = acul::make_unique<umbf::MaterialBinding>();
        info->name = material.name;
        info->id = acul::id_gen()();
        for (auto &id : materials_ids[i]) info->assignments.push_back(id);
        material_asset.blocks.push_back(std::move(info));
    }

    if (!append_scene_resources(scene_asset, objects, materials, images)) return false;
    if (!umbf::create_write_descriptor(scene_asset.header, file).success()) return false;
    return write_scene_asset(scene_asset, file, compressed);
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

u32 convert_json(const acul::string &input, const acul::string &output, bool compressed, const acul::string &work_dir)
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
                LOG_ERROR("failed to deserialize image: %s", input.c_str());
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
                LOG_ERROR("failed to deserialize material: %s", input.c_str());
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
                LOG_ERROR("failed to deserialize scene: %s", input.c_str());
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
                LOG_ERROR("failed to deserialize target: %s", input.c_str());
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
                LOG_ERROR("failed to deserialize library: %s", input.c_str());
                return 0;
            }
            return convert_library(library, output, compressed, work_dir);
        }
        default:
            LOG_ERROR("unsupported type: %x", root.type_sign);
            return 0;
    }
}
