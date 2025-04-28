#include <acul/io/file.hpp>
#include <acul/log.hpp>
#include <ecl/image/import.hpp>
#include <ecl/scene/obj/import.hpp>
#include <rapidjson/document.h>
#include <umbf/utils.hpp>
#include <umbf/version.h>
#include "models/umbf.hpp"

inline void createFileStructure(umbf::File &file, u16 type_sign, bool compressed)
{
    file.header.vendor_sign = UMBF_VENDOR_ID;
    file.header.vendor_version = UMBF_VERSION;
    file.header.spec_version = UMBF_VERSION;
    file.header.type_sign = type_sign;
    file.header.compressed = compressed;
}

bool convertRaw(const acul::string &input, bool compressed, umbf::File &file)
{
    createFileStructure(file, umbf::sign_block::format::raw, compressed);
    acul::vector<char> data;
    auto res = acul::io::file::read_binary(input, data);
    if (res != acul::io::file::op_state::success) return false;
    auto block = acul::make_shared<acul::meta::raw_block>();
    block->data = acul::alloc_n<char>(data.size());
    memcpy(block->data, data.data(), data.size());
    block->data_size = data.size();
    file.blocks.push_back(block);
    return true;
}

bool convertImage(const acul::string &input, bool compressed, umbf::File &file)
{
    auto importer = ecl::image::getImporterByPath(input);
    acul::vector<umbf::Image2D> images;
    umbf::Image2D *pImage = nullptr;
    bool ret = false;
    if (importer && importer->load(input, images) == acul::io::file::op_state::success)
    {
        pImage = &images.front();
        createFileStructure(file, umbf::sign_block::format::image, compressed);
        file.blocks.push_back(acul::make_shared<umbf::Image2D>(*pImage));
        ret = true;
    }
    acul::release(importer);
    return ret;
}

acul::shared_ptr<umbf::Image2D> modelToImage(const models::IPath &model)
{
    auto importer = ecl::image::getImporterByPath(model.path());
    acul::vector<umbf::Image2D> images;
    acul::shared_ptr<umbf::Image2D> pImage;
    if (importer && importer->load(model.path(), images) == acul::io::file::op_state::success)
        pImage = acul::make_shared<umbf::Image2D>(images.front());
    acul::release(importer);
    return pImage;
}

bool convertAtlas(const models::Atlas &atlas, bool compressed, umbf::File &file)
{
    createFileStructure(file, umbf::sign_block::format::image, compressed);
    auto image_block = acul::make_shared<umbf::Image2D>();
    image_block->width = atlas.width();
    image_block->height = atlas.height();
    image_block->bytesPerChannel = atlas.bytesPerChannel();
    image_block->channelCount = 4;
    image_block->channelNames = {"Red", "Green", "Blue", "Alpha"};
    image_block->imageFormat = atlas.imageFormat();

    auto atlas_block = acul::make_shared<umbf::Atlas>();
    atlas_block->padding = 1;
    atlas_block->discardStep = atlas.precision();
    acul::vector<acul::shared_ptr<umbf::Image2D>> atlas_dst_images;
    atlas_dst_images.reserve(atlas.images().size());
    for (const auto &image : atlas.images())
    {
        auto pImage = modelToImage(*image);
        if (!pImage)
        {
            logError("Failed to create image: %s", image->path().c_str());
            return false;
        }
        if (pImage->channelCount != image_block->channelCount ||
            pImage->bytesPerChannel != image_block->bytesPerChannel || pImage->imageFormat != image_block->imageFormat)
        {
            logInfo("Converting image to the atlas format: %s", image->path().c_str());
            void *converted = umbf::utils::convertImage(*pImage, image_block->imageFormat, image_block->channelCount);
            pImage->pixels = converted;
            pImage->bytesPerChannel = image_block->bytesPerChannel;
            pImage->channelCount = image_block->channelCount;
            pImage->imageFormat = image_block->imageFormat;
        }
        atlas_dst_images.push_back(pImage);
        atlas_block->packData.emplace_back(rectpack2D::rect_xywh(0, 0, pImage->width + 2 * atlas_block->padding,
                                                                 pImage->height + 2 * atlas_block->padding));
    }
    if (!umbf::packAtlas(std::max(image_block->width, image_block->height), atlas.precision(),
                         rectpack2D::flipping_option::DISABLED, atlas_block->packData))
    {
        logError("Failed to pack atlas");
        return false;
    }
    umbf::fillAtlasPixels(image_block, atlas_block, atlas_dst_images);

    file.blocks.push_back(image_block);
    file.blocks.push_back(atlas_block);

    return true;
}

bool convertImage(const models::Image &image, bool compressed, umbf::File &file)
{
    if (image.signature() == umbf::sign_block::meta::image2D)
    {
        auto serializer = acul::static_pointer_cast<models::IPath>(image.serializer());
        return convertImage(serializer->path(), compressed, file);
    }
    else if (image.signature() == umbf::sign_block::meta::image_atlas)
    {
        auto serializer = acul::static_pointer_cast<models::Atlas>(image.serializer());
        return convertAtlas(*serializer, compressed, file);
    }
    logError("Unsupported image type: %x", image.signature());
    return false;
}

void convertTarget(const models::Target &target, bool compressed, umbf::File &file)
{
    createFileStructure(file, umbf::sign_block::format::target, compressed);
    auto block = acul::make_shared<umbf::Target>();
    block->url = target.url();
    block->header = target.header();
    block->checksum = target.checksum();
    file.blocks.push_back(block);
}

bool convertImage(const acul::shared_ptr<models::UMBFRoot> &model, bool compressed, umbf::File &file)
{
    switch (model->type_sign)
    {
        case umbf::sign_block::format::image:
        {
            auto image_model = acul::static_pointer_cast<models::Image>(model);
            return convertImage(*image_model, compressed, file);
        }
        case umbf::sign_block::format::target:
        {
            auto target_model = acul::static_pointer_cast<models::Target>(model);
            convertTarget(*target_model, compressed, file);
            return true;
        }
        default:
            logError("Unsupported texture type: %x", model->type_sign);
            return false;
    }
}

bool convertMaterial(const models::Material &material, bool compressed, umbf::File &file)
{
    createFileStructure(file, umbf::sign_block::format::material, compressed);
    auto block = acul::make_shared<umbf::Material>();
    block->albedo = material.albedo();
    for (auto &texture : material.textures())
    {
        umbf::File texture_file;
        if (convertImage(texture, compressed, texture_file))
            block->textures.push_back(texture_file);
        else
            return false;
    }
    file.blocks.push_back(block);
    return true;
}

acul::unique_ptr<ecl::scene::ILoader> importMesh(const acul::string &input)
{
    auto ext = acul::io::get_extension(input);
    if (ext == ".obj")
    {
        auto obj_loader = acul::make_unique<ecl::scene::obj::Importer>(input);
        ecl::scene::obj::Importer importer(input);
        if (!importer.load())
        {
            logError("Failed to load obj: %s", importer.path().c_str());
            return nullptr;
        }
        return std::move(obj_loader);
    }
    logError("Unsupported mesh format: %s", ext.c_str());
    return nullptr;
}

u32 convertScene(const acul::string &input, const acul::string &output, bool compressed)
{
    auto importer = importMesh(input);
    if (!importer) return 0;

    umbf::File file;
    createFileStructure(file, umbf::sign_block::format::scene, compressed);

    auto block = acul::make_shared<umbf::Scene>();
    block->objects = importer->objects();
    block->materials.reserve(importer->materials().size());
    for (auto &material : importer->materials()) block->materials.push_back(*material);
    auto &textures = importer->textures();
    block->textures.resize(textures.size());
    for (int i = 0; i < textures.size(); ++i)
    {
        createFileStructure(block->textures[i], umbf::sign_block::format::target, false);
        block->textures[i].blocks.push_back(textures[i]);
    }
    file.blocks.push_back(block);
    return file.save(output) ? file.checksum : 0;
}

bool convertScene(models::Scene &scene, bool compressed, umbf::File &file)
{
    createFileStructure(file, umbf::sign_block::format::scene, compressed);
    auto scene_block = acul::make_shared<umbf::Scene>();
    scene_block->objects.reserve(scene.meshes().size());
    acul::vector<acul::vector<u64>> materials_ids(scene.materials().size());
    for (auto &mesh : scene.meshes())
    {
        auto importer = importMesh(mesh->path());
        if (!importer) return false;
        for (auto &object : importer->objects())
        {
            scene_block->objects.push_back(object);
            if (mesh->matID() != -1) materials_ids[mesh->matID()].push_back(object.id);
        }
    }
    file.blocks.push_back(scene_block);
    for (auto &texture : scene.textures())
    {
        umbf::File texture_file;
        if (convertImage(texture, compressed, texture_file))
            scene_block->textures.push_back(texture_file);
        else
            return false;
    }

    for (int i = 0; i < scene.materials().size(); ++i)
    {
        auto &material = scene.materials()[i];
        umbf::File material_file;
        if (material.asset->type_sign == umbf::sign_block::format::material)
        {
            auto material_model = acul::static_pointer_cast<models::Material>(material.asset);
            if (!convertMaterial(*material_model, compressed, material_file)) return false;
        }
        else if (material.asset->type_sign == umbf::sign_block::format::target)
        {
            auto target_model = acul::static_pointer_cast<models::Target>(material.asset);
            convertTarget(*target_model, compressed, material_file);
        }
        else
        {
            logError("Unsupported material type: %x", material.asset->type_sign);
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

void prepareLibraryNode(const models::FileNode &src, umbf::Library::Node &dst)
{
    if (src.children.empty())
    {
        if (src.isFolder)
        {
            dst.name = src.name;
            dst.isFolder = true;
        }
        else
        {
            switch (src.asset->type_sign)
            {
                case umbf::sign_block::format::image:
                    if (!convertImage(src.asset, false, dst.asset))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                case umbf::sign_block::format::material:
                    if (!convertMaterial(*acul::static_pointer_cast<models::Material>(src.asset), false, dst.asset))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                case umbf::sign_block::format::scene:
                    if (!convertScene(*acul::static_pointer_cast<models::Scene>(src.asset), false, dst.asset))
                        throw acul::runtime_error("Failed to create asset file");
                    break;
                case umbf::sign_block::format::target:
                    convertTarget(*acul::static_pointer_cast<models::Target>(src.asset), false, dst.asset);
                    break;
                case umbf::sign_block::format::raw:
                    if (!convertRaw(acul::static_pointer_cast<models::IPath>(src.asset)->path(), false, dst.asset))
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
        dst.isFolder = src.isFolder;
        for (const auto &child : src.children)
        {
            umbf::Library::Node node;
            prepareLibraryNode(child, node);
            dst.children.push_back(node);
        }
    }
}

u32 convertLibrary(const models::Library &library, const acul::string &output, bool compressed)
{
    umbf::File file;
    createFileStructure(file, umbf::sign_block::format::library, compressed);
    auto block = acul::make_shared<umbf::Library>();
    prepareLibraryNode(library.fileTree(), block->fileTree);
    file.blocks.push_back(block);
    return file.save(output) ? file.checksum : 0;
}

u32 convertJson(const acul::string &input, const acul::string &output, bool compressed)
{
    rapidjson::Document json;
    models::UMBFRoot root;
    if (!root.deserializeFromFile(input, json))
    {
        logError("Failed to load file: %s", input.c_str());
        return 0;
    }
    switch (root.type_sign)
    {
        case umbf::sign_block::format::image:
        {
            models::Image image;
            if (!image.deserializeObject(json))
            {
                logError("Failed to deserialize image: %s", input.c_str());
                return 0;
            }
            umbf::File file;
            if (!convertImage(image, compressed, file)) return 0;
            return file.save(output) ? file.checksum : 0;
        }
        case umbf::sign_block::format::material:
        {
            models::Material material;
            if (!material.deserializeObject(json))
            {
                logError("Failed to deserialize material: %s", input.c_str());
                return 0;
            }
            umbf::File file;
            if (!convertMaterial(material, compressed, file)) return 0;
            return file.save(output) ? file.checksum : 0;
        }
        case umbf::sign_block::format::scene:
        {
            models::Scene scene;
            if (!scene.deserializeObject(json))
            {
                logError("Failed to deserialize scene: %s", input.c_str());
                return 0;
            }
            umbf::File file;
            if (!convertScene(scene, compressed, file)) return 0;
            return file.save(output) ? file.checksum : 0;
        }
        case umbf::sign_block::format::target:
        {
            models::Target target;
            if (!target.deserializeObject(json))
            {
                logError("Failed to deserialize target: %s", input.c_str());
                return 0;
            }
            umbf::File file;
            convertTarget(target, compressed, file);
            return file.save(output) ? file.checksum : 0;
        }
        case umbf::sign_block::format::library:
        {
            models::Library library;
            if (!library.deserializeObject(json))
            {
                logError("Failed to deserialize library: %s", input.c_str());
                return 0;
            }
            return convertLibrary(library, output, compressed);
        }
        default:
            logError("Unsupported type: %x", root.type_sign);
            return 0;
    }
}