#include <acul/io/fs/file.hpp>
#include <acul/io/fs/path.hpp>
#include <acul/io/path.hpp>
#include <acul/log.hpp>
#include <aecl/image/import.hpp>
#include <aecl/scene/obj/import.hpp>
#include <rapidjson/document.h>
#include <umbf/utils.hpp>
#include <umbf/version.h>
#include "models/umbf.hpp"

namespace
{
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
                umbf::utils::pack_max_rects({max_side, max_side}, 0, probe_rects,
                                            umbf::utils::MaxRectsHeuristic::best_short_side_fit,
                                            umbf::utils::MaxRectsTransformBits::none, padding);
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
                umbf::utils::pack_max_rects({mid_side, mid_side}, 0, probe_rects,
                                            umbf::utils::MaxRectsHeuristic::best_short_side_fit,
                                            umbf::utils::MaxRectsTransformBits::none, padding);
            if (result.packed)
            {
                best_side = mid_side;
                max_side = mid_side - 1;
            }
            else
                min_side = mid_side + 1;
        }

        return {best_side, best_side};
    }
} // namespace

inline void create_file_structure(umbf::File &file, u16 type_sign, u8 flags = 0)
{
    file.header.vendor_sign = UMBF_VENDOR_ID;
    file.header.vendor_version = UMBF_VERSION;
    file.header.spec_version = UMBF_VERSION;
    file.header.type_sign = type_sign;
    file.header.flags = flags;
}

namespace
{
    constexpr int default_compression_level = 5;

    bool read_raw_file(const acul::string &input, acul::vector<char> &data)
    {
        if (!acul::fs::read_binary(input, data))
        {
            LOG_ERROR("Failed to read raw file: %s", input.c_str());
            return false;
        }
        return true;
    }

    bool convert_raw_file(const acul::string &input, umbf::File &file)
    {
        acul::vector<char> data;
        if (!read_raw_file(input, data)) return false;
        auto block = acul::make_shared<umbf::RawBlock>();
        block->data = acul::alloc_n<char>(data.size());
        memcpy(block->data, data.data(), data.size());
        block->data_size = data.size();
        file.blocks.push_back(block);
        return true;
    }

    umbf::Library::Node *find_or_create_folder(umbf::Library::Node &parent, const acul::string &name)
    {
        auto it = std::find_if(parent.children.begin(), parent.children.end(),
                               [&name](const umbf::Library::Node &child) { return child.name == name; });
        if (it != parent.children.end()) return &(*it);

        umbf::Library::Node folder;
        folder.name = name;
        folder.is_folder = true;
        parent.children.push_back(std::move(folder));
        return &parent.children.back();
    }

    bool append_mapped_payload(const acul::string &input, bool compressed, umbf::File &asset, acul::vector<char> &payload)
    {
        acul::vector<char> data;
        if (!read_raw_file(input, data)) return false;

        acul::vector<char> stored = std::move(data);
        if (compressed)
        {
            acul::vector<char> compressed_data;
            auto cr = acul::fs::compress(stored.data(), stored.size(), compressed_data, default_compression_level);
            if (!cr.success())
            {
                LOG_ERROR("Failed to compress raw file: %s", input.c_str());
                return false;
            }
            stored = std::move(compressed_data);
        }

        create_file_structure(asset, umbf::sign_block::format::raw);
        auto mapping = acul::make_shared<umbf::Mapping>();
        mapping->offset = payload.size();
        mapping->size = stored.size();
        asset.blocks.push_back(mapping);
        payload.insert(payload.end(), stored.begin(), stored.end());
        return true;
    }

    bool build_raw_library_node(umbf::Library::Node &root, const acul::path &relative_path, const acul::string &source_path,
                                bool mapped, bool compressed, acul::vector<char> *payload)
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
            node.is_folder = false;
            const bool ok = mapped ? append_mapped_payload(source_path, compressed, node.asset, *payload)
                                   : convert_raw_file(source_path, node.asset);
            if (!ok) return false;
            current->children.push_back(std::move(node));
        }
        return true;
    }

    bool convert_raw_directory(const acul::string &input, bool compressed, bool mapped, umbf::File &file)
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
        library->file_tree.is_folder = true;

        acul::vector<char> payload;
        for (const auto &entry : files)
        {
            if (entry.size() <= base_str.size()) continue;
            size_t relative_offset = base_str.size();
            if (entry[relative_offset] == '/' || entry[relative_offset] == '\\') ++relative_offset;
            const acul::path relative_path(entry.substr(relative_offset));
            if (!build_raw_library_node(library->file_tree, relative_path, entry, mapped, compressed, &payload))
                return false;
        }

        const u8 flags = mapped ? static_cast<u8>(compressed ? UMBF_COMPRESSION_MAPPED_BIT : 0)
                                : static_cast<u8>(compressed ? UMBF_COMPRESSION_PAYLOAD_BIT : 0);
        create_file_structure(file, umbf::sign_block::format::library, flags);
        file.blocks.push_back(library);

        if (mapped)
        {
            auto raw = acul::make_shared<umbf::RawBlock>();
            raw->data_size = payload.size();
            raw->data = acul::alloc_n<char>(payload.size());
            memcpy(raw->data, payload.data(), payload.size());
            file.blocks.push_back(raw);
        }
        return true;
    }
} // namespace

bool convert_raw(const acul::string &input, bool compressed, bool recursive, bool mapped, umbf::File &file)
{
    if (acul::fs::is_directory(input.c_str()))
    {
        if (!recursive)
        {
            LOG_ERROR("Directory input for raw conversion requires -R");
            return false;
        }
        return convert_raw_directory(input, compressed, mapped, file);
    }

    if (mapped)
    {
        LOG_ERROR("--mapped is supported only for recursive raw directory conversion");
        return false;
    }

    create_file_structure(file, umbf::sign_block::format::raw,
                          compressed ? UMBF_COMPRESSION_PAYLOAD_BIT : 0);
    return convert_raw_file(input, file);
}

bool convert_image(const acul::string &input, bool compressed, umbf::File &file)
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
            create_file_structure(file, umbf::sign_block::format::image, compressed);
            file.blocks.push_back(acul::make_shared<umbf::Image2D>(*pImage));
            ret = true;
        }
        else LOG_ERROR("AECL error: %s", importer->error().c_str());
        acul::release(importer);
    }
    return ret;
}

acul::shared_ptr<umbf::Image2D> model_to_image(const models::IPath &model)
{
    auto importer = aecl::image::get_importer_by_path(model.path());
    acul::vector<umbf::Image2D> images;
    acul::shared_ptr<umbf::Image2D> dst_image;
    if (importer)
    {
        if (importer->load(model.path(), images)) dst_image = acul::make_shared<umbf::Image2D>(images.front());
        else LOG_ERROR("AECL error: %s", importer->error().c_str());
        acul::release(importer);
    }
    return dst_image;
}

bool convert_atlas(const models::Atlas &atlas, bool compressed, umbf::File &file)
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
        auto pImage = model_to_image(*image);
        if (!pImage)
        {
            LOG_ERROR("Failed to create image: %s", image->path().c_str());
            return false;
        }
        if (pImage->channels.size() != image_block->channels.size() || pImage->format != image_block->format)
        {
            LOG_INFO("Converting image to the atlas format: %s", image->path().c_str());
            void *converted = umbf::utils::convert_image(*pImage, image_block->format, image_block->channels.size());
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

    if (!umbf::utils::pack_max_rects(atlas_size, 0,
                                     atlas_block->pack_data, umbf::utils::MaxRectsHeuristic::best_short_side_fit,
                                     umbf::utils::MaxRectsTransformBits::none, atlas_block->padding)
             .packed)
    {
        LOG_ERROR("Failed to pack atlas");
        return false;
    }
    umbf::fill_atlas_pixels(image_block, atlas_block, atlas_dst_images);

    file.blocks.push_back(image_block);
    file.blocks.push_back(atlas_block);

    return true;
}

bool convert_image(const models::Image &image, bool compressed, umbf::File &file)
{
    if (image.signature() == umbf::sign_block::image)
    {
        auto serializer = acul::static_pointer_cast<models::IPath>(image.serializer());
        return convert_image(serializer->path(), compressed, file);
    }
    else if (image.signature() == umbf::sign_block::image_atlas)
    {
        auto serializer = acul::static_pointer_cast<models::Atlas>(image.serializer());
        return convert_atlas(*serializer, compressed, file);
    }
    LOG_ERROR("Unsupported image type: %x", image.signature());
    return false;
}

void convert_target(const models::Target &target, bool compressed, umbf::File &file)
{
    create_file_structure(file, umbf::sign_block::format::target, compressed);
    auto block = acul::make_shared<umbf::Target>();
    block->url = target.url();
    block->header = target.header();
    block->checksum = target.checksum();
    file.blocks.push_back(block);
}

bool convert_image(const acul::shared_ptr<models::UMBFRoot> &model, bool compressed, umbf::File &file)
{
    switch (model->type_sign)
    {
        case umbf::sign_block::format::image:
        {
            auto image_model = acul::static_pointer_cast<models::Image>(model);
            return convert_image(*image_model, compressed, file);
        }
        case umbf::sign_block::format::target:
        {
            auto target_model = acul::static_pointer_cast<models::Target>(model);
            convert_target(*target_model, compressed, file);
            return true;
        }
        default:
            LOG_ERROR("Unsupported texture type: %x", model->type_sign);
            return false;
    }
}

bool convert_material(const models::Material &material, bool compressed, umbf::File &file)
{
    create_file_structure(file, umbf::sign_block::format::material, compressed);
    auto block = acul::make_shared<umbf::Material>();
    block->albedo = material.albedo();
    for (auto &texture : material.textures())
    {
        umbf::File texture_file;
        if (convert_image(texture, compressed, texture_file)) block->textures.push_back(texture_file);
        else return false;
    }
    file.blocks.push_back(block);
    return true;
}

acul::unique_ptr<aecl::scene::ILoader> import_mesh(const acul::string &input)
{
    auto ext = acul::fs::get_extension(input);
    if (ext == ".obj")
    {
        auto obj_loader = acul::make_unique<aecl::scene::obj::Importer>(input);
        aecl::scene::obj::Importer importer(input);
        if (!importer.load())
        {
            LOG_ERROR("Failed to load obj: %s", importer.path().c_str());
            return nullptr;
        }
        return std::move(obj_loader);
    }
    LOG_ERROR("Unsupported mesh format: %s", ext.c_str());
    return nullptr;
}

u32 convert_scene(const acul::string &input, const acul::string &output, bool compressed)
{
    auto importer = import_mesh(input);
    if (!importer) return 0;

    umbf::File file;
    create_file_structure(file, umbf::sign_block::format::scene, compressed);

    auto block = acul::make_shared<umbf::Scene>();
    block->objects = importer->objects();
    block->materials.reserve(importer->materials().size());
    for (auto &material : importer->materials()) block->materials.push_back(*material);
    auto &textures = importer->textures();
    block->textures.resize(textures.size());
    for (size_t i = 0; i < textures.size(); ++i)
    {
        create_file_structure(block->textures[i], umbf::sign_block::format::target, false);
        block->textures[i].blocks.push_back(textures[i]);
    }
    file.blocks.push_back(block);
    return file.save(output) ? file.checksum : 0;
}

bool convert_scene(models::Scene &scene, bool compressed, umbf::File &file)
{
    create_file_structure(file, umbf::sign_block::format::scene, compressed);
    auto scene_block = acul::make_shared<umbf::Scene>();
    scene_block->objects.reserve(scene.meshes().size());
    acul::vector<acul::vector<u64>> materials_ids(scene.materials().size());
    for (auto &mesh : scene.meshes())
    {
        auto importer = import_mesh(mesh->path());
        if (!importer) return false;
        for (auto &object : importer->objects())
        {
            scene_block->objects.push_back(object);
            if (mesh->mat_id() != -1) materials_ids[mesh->mat_id()].push_back(object.id);
        }
    }
    file.blocks.push_back(scene_block);
    for (auto &texture : scene.textures())
    {
        umbf::File texture_file;
        if (convert_image(texture, compressed, texture_file)) scene_block->textures.push_back(texture_file);
        else return false;
    }

    for (size_t i = 0; i < scene.materials().size(); ++i)
    {
        auto &material = scene.materials()[i];
        umbf::File material_file;
        if (material.asset->type_sign == umbf::sign_block::format::material)
        {
            auto material_model = acul::static_pointer_cast<models::Material>(material.asset);
            if (!convert_material(*material_model, compressed, material_file)) return false;
        }
        else if (material.asset->type_sign == umbf::sign_block::format::target)
        {
            auto target_model = acul::static_pointer_cast<models::Target>(material.asset);
            convert_target(*target_model, compressed, material_file);
        }
        else
        {
            LOG_ERROR("Unsupported material type: %x", material.asset->type_sign);
            return false;
        }
        auto mat_info = acul::make_shared<umbf::MaterialInfo>();
        mat_info->name = material.name;
        mat_info->id = acul::id_gen()();
        for (auto &id : materials_ids[i]) mat_info->assignments.push_back(id);
        material_file.blocks.push_back(mat_info);
        scene_block->materials.push_back(material_file);
    }

    return true;
}

void prepare_library_node(const models::FileNode &src, umbf::Library::Node &dst)
{
    if (src.children.empty())
    {
        if (src.is_folder)
        {
            dst.name = src.name;
            dst.is_folder = true;
        }
        else
        {
            switch (src.asset->type_sign)
            {
                case umbf::sign_block::format::image:
                    if (!convert_image(src.asset, false, dst.asset))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                case umbf::sign_block::format::material:
                    if (!convert_material(*acul::static_pointer_cast<models::Material>(src.asset), false, dst.asset))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                case umbf::sign_block::format::scene:
                    if (!convert_scene(*acul::static_pointer_cast<models::Scene>(src.asset), false, dst.asset))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                case umbf::sign_block::format::target:
                    convert_target(*acul::static_pointer_cast<models::Target>(src.asset), false, dst.asset);
                    break;
                case umbf::sign_block::format::raw:
                    create_file_structure(dst.asset, umbf::sign_block::format::raw);
                    if (!convert_raw_file(acul::static_pointer_cast<models::IPath>(src.asset)->path(), dst.asset))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                default:
                    throw acul::runtime_error(acul::format("Unsupported asset type: %x", src.asset->type_sign));
                    break;
            }
            dst.name = src.name;
        }
    }
    else
    {
        dst.name = src.name;
        dst.is_folder = src.is_folder;
        for (const auto &child : src.children)
        {
            umbf::Library::Node node;
            prepare_library_node(child, node);
            dst.children.push_back(node);
        }
    }
}

u32 convert_library(const models::Library &library, const acul::string &output, bool compressed)
{
    umbf::File file;
    create_file_structure(file, umbf::sign_block::format::library, compressed);
    auto block = acul::make_shared<umbf::Library>();
    prepare_library_node(library.file_tree(), block->file_tree);
    file.blocks.push_back(block);
    return file.save(output) ? file.checksum : 0;
}

u32 convert_json(const acul::string &input, const acul::string &output, bool compressed)
{
    rapidjson::Document json;
    models::UMBFRoot root;
    if (!root.deserialize_from_file(input, json))
    {
        LOG_ERROR("Failed to load file: %s", input.c_str());
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
            umbf::File file;
            if (!convert_image(image, compressed, file)) return 0;
            return file.save(output) ? file.checksum : 0;
        }
        case umbf::sign_block::format::material:
        {
            models::Material material;
            if (!material.deserialize_object(json))
            {
                LOG_ERROR("Failed to deserialize material: %s", input.c_str());
                return 0;
            }
            umbf::File file;
            if (!convert_material(material, compressed, file)) return 0;
            return file.save(output) ? file.checksum : 0;
        }
        case umbf::sign_block::format::scene:
        {
            models::Scene scene;
            if (!scene.deserialize_object(json))
            {
                LOG_ERROR("Failed to deserialize scene: %s", input.c_str());
                return 0;
            }
            umbf::File file;
            if (!convert_scene(scene, compressed, file)) return 0;
            return file.save(output) ? file.checksum : 0;
        }
        case umbf::sign_block::format::target:
        {
            models::Target target;
            if (!target.deserialize_object(json))
            {
                LOG_ERROR("Failed to deserialize target: %s", input.c_str());
                return 0;
            }
            umbf::File file;
            convert_target(target, compressed, file);
            return file.save(output) ? file.checksum : 0;
        }
        case umbf::sign_block::format::library:
        {
            models::Library library;
            if (!library.deserialize_object(json))
            {
                LOG_ERROR("Failed to deserialize library: %s", input.c_str());
                return 0;
            }
            return convert_library(library, output, compressed);
        }
        default:
            LOG_ERROR("Unsupported type: %x", root.type_sign);
            return 0;
    }
}
