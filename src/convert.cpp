#include <acul/io/file.hpp>
#include <acul/log.hpp>
#include <aecl/image/import.hpp>
#include <aecl/scene/obj/import.hpp>
#include <rapidjson/document.h>
#include <umbf/utils.hpp>
#include <umbf/version.h>
#include "models/umbf.hpp"

inline void create_file_structure(umbf::File &file, u16 type_sign, bool compressed)
{
    file.header.vendor_sign = UMBF_VENDOR_ID;
    file.header.vendor_version = UMBF_VERSION;
    file.header.spec_version = UMBF_VERSION;
    file.header.type_sign = type_sign;
    file.header.compressed = compressed;
}

bool convert_raw(const acul::string &input, bool compressed, umbf::File &file)
{
    create_file_structure(file, umbf::sign_block::format::raw, compressed);
    acul::vector<char> data;
    auto res = acul::io::file::read_binary(input, data);
    if (res != acul::io::file::op_state::success) return false;
    auto block = acul::make_shared<umbf::RawBlock>();
    block->data = acul::alloc_n<char>(data.size());
    memcpy(block->data, data.data(), data.size());
    block->data_size = data.size();
    file.blocks.push_back(block);
    return true;
}

bool convert_image(const acul::string &input, bool compressed, umbf::File &file)
{
    auto importer = aecl::image::get_importer_by_path(input);
    acul::vector<umbf::Image2D> images;
    umbf::Image2D *pImage = nullptr;
    bool ret = false;
    if (importer && importer->load(input, images) == acul::io::file::op_state::success)
    {
        pImage = &images.front();
        create_file_structure(file, umbf::sign_block::format::image, compressed);
        file.blocks.push_back(acul::make_shared<umbf::Image2D>(*pImage));
        ret = true;
    }
    acul::release(importer);
    return ret;
}

acul::shared_ptr<umbf::Image2D> model_to_image(const models::IPath &model)
{
    auto importer = aecl::image::get_importer_by_path(model.path());
    acul::vector<umbf::Image2D> images;
    acul::shared_ptr<umbf::Image2D> pImage;
    if (importer && importer->load(model.path(), images) == acul::io::file::op_state::success)
        pImage = acul::make_shared<umbf::Image2D>(images.front());
    acul::release(importer);
    return pImage;
}

bool convert_atlas(const models::Atlas &atlas, bool compressed, umbf::File &file)
{
    create_file_structure(file, umbf::sign_block::format::image, compressed);
    auto image_block = acul::make_shared<umbf::Image2D>();
    image_block->width = atlas.width();
    image_block->height = atlas.height();
    image_block->format.bytes_per_channel = atlas.bytes_per_channel();
    image_block->format.type = atlas.type();
    image_block->channels = {"red", "green", "blue", "alpha"};

    auto atlas_block = acul::make_shared<umbf::Atlas>();
    atlas_block->padding = 1;
    atlas_block->discard_step = atlas.precision();
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
        atlas_block->pack_data.emplace_back(rectpack2D::rect_xywh(0, 0, pImage->width + 2 * atlas_block->padding,
                                                                  pImage->height + 2 * atlas_block->padding));
    }
    if (!umbf::pack_atlas(std::max(image_block->width, image_block->height), atlas.precision(),
                          rectpack2D::flipping_option::DISABLED, atlas_block->pack_data))
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
        if (convert_image(texture, compressed, texture_file))
            block->textures.push_back(texture_file);
        else
            return false;
    }
    file.blocks.push_back(block);
    return true;
}

acul::unique_ptr<aecl::scene::ILoader> import_mesh(const acul::string &input)
{
    auto ext = acul::io::get_extension(input);
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
        if (convert_image(texture, compressed, texture_file))
            scene_block->textures.push_back(texture_file);
        else
            return false;
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
                    if (!convert_raw(acul::static_pointer_cast<models::IPath>(src.asset)->path(), false, dst.asset))
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