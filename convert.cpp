#include "convert.hpp"
#include <assets/utils.hpp>
#include <core/log.hpp>
#include <ecl/scene/obj/import.hpp>
#include <memory>

namespace assettool
{
    ImageResource::ImageResource(const std::filesystem::path &path)
        : _importer(ecl::image::getImporterByPath(path)), _valid(false)
    {
        DArray<assets::ImageInfo> images;
        if (_importer->load(path, images) == io::file::ReadState::Success)
        {
            _image = images.front();
            _valid = true;
        }
    }

    std::shared_ptr<assets::Image> modelToImage2D(const std::shared_ptr<models::Image2D> &src,
                                                  DArray<ImageResource> &resources, const models::InfoHeader &info)
    {
        logInfo("Loading image: %s", src->path().string().c_str());
        ImageResource imageResource(src->path());
        if (!imageResource.valid())
        {
            logError("Failed to load image: %s", src->path().string().c_str());
            return nullptr;
        }
        logInfo("Converting image to asset");
        auto imageAsset = std::make_shared<assets::Image2D>(imageResource.image());
        auto asset = std::make_shared<assets::Image>(info, assets::ImageTypeFlagBits::image2D, imageAsset);
        resources.push_back(std::move(imageResource));
        return asset;
    }

    std::shared_ptr<assets::Image> modelToImageAtlas(const std::shared_ptr<models::Atlas> &src,
                                                     DArray<ImageResource> &resources, const models::InfoHeader &info)
    {
        assets::ImageInfo imageInfo{};
        imageInfo.width = src->width();
        imageInfo.height = src->height();
        imageInfo.bytesPerChannel = src->bytesPerChannel();
        imageInfo.channelCount = 4;
        imageInfo.channelNames = {"Red", "Green", "Blue", "Alpha"};
        imageInfo.imageFormat = src->imageFormat();
        DArray<std::shared_ptr<assets::Image2D>> textures;
        std::vector<assets::Atlas::Rect> rects;
        assert(imageInfo.pixels == nullptr);
        for (const auto &image : src->images())
        {
            models::InfoHeader info{};
            info.type = assets::Type::Image;
            info.compressed = false;
            auto texture2D = modelToImage2D(image, resources, info);
            if (texture2D)
            {
                auto texture = std::static_pointer_cast<assets::Image2D>(texture2D->stream());
                if (texture->channelCount != imageInfo.channelCount ||
                    texture->bytesPerChannel != imageInfo.bytesPerChannel ||
                    texture->imageFormat != imageInfo.imageFormat)
                {
                    logInfo("Converting image to the atlas format: %s", image->path().string().c_str());
                    void *converted =
                        assets::utils::convertImage(*texture, imageInfo.imageFormat, imageInfo.channelCount);
                    texture->pixels = converted;
                    texture->bytesPerChannel = imageInfo.bytesPerChannel;
                    texture->channelCount = imageInfo.channelCount;
                    texture->imageFormat = imageInfo.imageFormat;
                }
                textures.push_back(texture);
                rects.emplace_back(rectpack2D::rect_xywh(0, 0, texture->width, texture->height));
            }
        }
        if (!assets::packAtlas(std::max(imageInfo.width, imageInfo.height), src->precision(),
                               rectpack2D::flipping_option::DISABLED, rects))
        {
            logError("Failed to pack atlas");
            return nullptr;
        }
        auto atlas = std::make_shared<assets::Atlas>(imageInfo, src->precision(), textures, rects);
        auto asset = std::make_shared<assets::Image>(
            info, assets::ImageTypeFlagBits::image2D | assets::ImageTypeFlagBits::atlas, atlas);
        return asset;
    }

    std::shared_ptr<assets::Image> modelToImage(const std::shared_ptr<models::Image> &src,
                                                DArray<ImageResource> &resources)
    {
        if (src->type() & assets::ImageTypeFlagBits::atlas)
        {
            auto serializer = std::static_pointer_cast<models::Atlas>(src->serializer());
            return modelToImageAtlas(serializer, resources, src->assetInfo());
        }
        else if (src->type() & assets::ImageTypeFlagBits::image2D)
        {
            auto serializer = std::static_pointer_cast<models::Image2D>(src->serializer());
            return modelToImage2D(serializer, resources, src->assetInfo());
        }
        else
            return nullptr;
    }

    std::shared_ptr<assets::Asset> modelToImageAny(std::shared_ptr<models::AssetBase> &src,
                                                   DArray<ImageResource> &resources)
    {
        if (!src)
        {
            logError("Source asset cannot be a null pointer");
            return nullptr;
        }
        std::shared_ptr<assets::Asset> asset;
        switch (src->assetInfo().type)
        {
            case assets::Type::Image:
            {
                auto model = std::static_pointer_cast<models::Image>(src);
                asset = modelToImage(model, resources);
                break;
            }
            case assets::Type::Target:
            {
                auto model = std::static_pointer_cast<models::Target>(src);
                asset = std::make_shared<assets::Target>(src->assetInfo(), model->addr(), model->metaData());
                break;
            }
            default:
                logError("Invalid asset type");
                return nullptr;
        }
        if (!asset) logError("Failed to convert image from model");
        return asset;
    }

    std::shared_ptr<assets::Material> modelToMaterial(const std::shared_ptr<models::Material> &src,
                                                      DArray<ImageResource> &resources)
    {
        DArray<std::shared_ptr<assets::Asset>> textures;
        for (auto &texture : src->textures())
            if (auto asset = modelToImageAny(texture, resources)) textures.push_back(asset);
        assets::MaterialInfo material;
        material.albedo = src->albedo();
        return std::make_shared<assets::Material>(src->assetInfo(), textures, material);
    }

    std::shared_ptr<assets::Asset> modelToMaterialAny(std::shared_ptr<models::AssetBase> &src,
                                                      DArray<ImageResource> &resources, const std::string &name)
    {
        if (!src)
        {
            logError("Source asset cannot be a null pointer");
            return nullptr;
        }
        std::shared_ptr<assets::Asset> asset;
        switch (src->assetInfo().type)
        {
            case assets::Type::Material:
            {
                auto model = std::static_pointer_cast<models::Material>(src);
                asset = modelToMaterial(model, resources);
                if (asset && name.length() > 0)
                {
                    auto meta = std::make_shared<assets::meta::MaterialBlock>();
                    meta->name = name;
                    asset->meta.push_front(meta);
                }
                break;
            }
            case assets::Type::Target:
            {
                auto model = std::static_pointer_cast<models::Target>(src);
                asset = std::make_shared<assets::Target>(src->assetInfo(), model->addr(), model->metaData());
                break;
            }
            default:
                logError("Invalid asset type");
                return nullptr;
        }
        if (!asset) logError("Failed to convert material from model");
        return asset;
    }

    std::shared_ptr<assets::Scene> modelToScene(models::Scene &sceneInfo, DArray<ImageResource> &images)
    {
        DArray<std::shared_ptr<assets::Object>> objects;
        for (int i = 0; i < sceneInfo.meshes().size(); i++)
        {
            auto &mesh = sceneInfo.meshes()[i];
            switch (mesh->format())
            {
                case models::Mesh::Format::Obj:
                {
                    ecl::scene::obj::Importer importer(mesh->path());
                    if (importer.load() != io::file::ReadState::Success)
                    {
                        logError("Failed to load obj: %ls", mesh->path().c_str());
                        return nullptr;
                    }
                    objects.emplace_back(std::make_shared<assets::Object>());
                    objects.back()->meta = importer.meshes().back();
                    break;
                }
                default:
                    logError("Unsupported scene format");
                    return nullptr;
            }
        }

        DArray<ImageResource> imageResources{};
        DArray<std::shared_ptr<assets::Asset>> textures;
        for (auto &texture : sceneInfo.textures())
            if (auto asset = modelToImageAny(texture, imageResources)) textures.push_back(asset);

        DArray<std::shared_ptr<assets::Asset>> materials;
        for (auto &material : sceneInfo.materials())
            if (auto asset = modelToMaterialAny(material.asset, imageResources, material.name))
                materials.push_back(asset);
        auto asset = std::make_shared<assets::Scene>(sceneInfo.assetInfo(), objects, textures, materials);
        if (!asset) return nullptr;

        // Meta
        std::string info = "Genereated by App3D Asset Tool";
        std::string author = "App3D Dev Team";
        u32 version = vk::makeApiVersion(0, 1, 0, 0);
        auto meta = std::make_shared<assets::meta::SceneInfo>();
        meta->info = info;
        meta->author = author;
        meta->version = version;
        asset->meta.push_front(meta);
        return asset;
    }

    void prepareNodeByModel(const models::FileNode &src, assets::FileNode &dst, DArray<ImageResource> &resources)
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
                switch (src.asset->assetInfo().type)
                {
                    case assets::Type::Image:
                    {
                        auto model = std::static_pointer_cast<models::Image>(src.asset);
                        dst.name = src.name;
                        dst.asset = modelToImage(model, resources);
                        if (!dst.asset) throw std::runtime_error("Failed to convert model to image");
                        break;
                    }
                    case assets::Type::Material:
                    {
                        auto model = std::static_pointer_cast<models::Material>(src.asset);
                        dst.name = src.name;
                        dst.asset = modelToMaterial(model, resources);
                        if (!dst.asset) throw std::runtime_error("Failed to convert model to material");
                        break;
                    }
                    case assets::Type::Scene:
                    {
                        auto model = std::static_pointer_cast<models::Scene>(src.asset);
                        dst.name = src.name;
                        dst.asset = modelToScene(*model, resources);
                        if (!dst.asset) throw std::runtime_error("Failed to convert model to scene");
                        break;
                    }
                    case assets::Type::Target:
                    {
                        auto model = std::static_pointer_cast<models::Target>(src.asset);
                        dst.name = src.name;
                        dst.asset =
                            std::make_shared<assets::Target>(model->assetInfo(), model->addr(), model->metaData());
                        break;
                    }
                    default:
                        break;
                }
            }
        }
        else
        {
            dst.name = src.name;
            dst.isFolder = true;
            for (const auto &child : src.children)
            {
                assets::FileNode node;
                prepareNodeByModel(child, node, resources);
                dst.children.push_back(node);
            }
        }
    }
} // namespace assettool