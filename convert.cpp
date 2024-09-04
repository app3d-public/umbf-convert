#include "convert.hpp"
#include <assets/utils.hpp>
#include <core/event.hpp>
#include <core/log.hpp>
#include <ecl/scene/obj/import.hpp>
#include <memory>
#include "models/asset.hpp"

namespace assettool
{
    ImageResource::ImageResource(const std::filesystem::path &path)
        : _importer(ecl::image::getImporterByPath(path)), _valid(false)
    {
        DArray<assets::Image2D> images;
        if (_importer && _importer->load(path, images) == io::file::ReadState::Success)
        {
            _image = images.front();
            _valid = true;
        }
    }

    std::shared_ptr<assets::Image2D> modelToImage2D(const std::shared_ptr<models::Image2D> &src,
                                                    DArray<ImageResource> &resources)
    {
        logInfo("Loading image: %s", src->path().string().c_str());
        ImageResource imageResource(src->path());
        if (!imageResource.valid())
        {
            logError("Failed to load image: %s", src->path().string().c_str());
            return nullptr;
        }
        logInfo("Converting image to asset");
        auto image = std::make_shared<assets::Image2D>(imageResource.image());
        resources.push_back(std::move(imageResource));
        return image;
    }

    std::shared_ptr<meta::Block> modelToImageAtlas(const std::shared_ptr<models::Atlas> &src,
                                                   DArray<ImageResource> &resources)
    {
        auto atlas = std::make_shared<assets::Atlas>();
        atlas->width = src->width();
        atlas->height = src->height();
        atlas->bytesPerChannel = src->bytesPerChannel();
        atlas->channelCount = 4;
        atlas->channelNames = {"Red", "Green", "Blue", "Alpha"};
        atlas->imageFormat = src->imageFormat();
        std::vector<assets::Atlas::Rect> rects;
        for (const auto &image : src->images())
        {
            auto texture2D = modelToImage2D(image, resources);
            if (!texture2D) continue;
            if (texture2D->channelCount != atlas->channelCount ||
                texture2D->bytesPerChannel != atlas->bytesPerChannel || texture2D->imageFormat != atlas->imageFormat)
            {
                logInfo("Converting image to the atlas format: %s", image->path().string().c_str());
                void *converted = assets::utils::convertImage(*texture2D, atlas->imageFormat, atlas->channelCount);
                texture2D->pixels = converted;
                texture2D->bytesPerChannel = atlas->bytesPerChannel;
                texture2D->channelCount = atlas->channelCount;
                texture2D->imageFormat = atlas->imageFormat;
            }
            atlas->images.push_back(texture2D);
            rects.emplace_back(rectpack2D::rect_xywh(0, 0, texture2D->width, texture2D->height));
        }
        if (!assets::packAtlas(std::max(atlas->width, atlas->height), src->precision(),
                               rectpack2D::flipping_option::DISABLED, rects))
        {
            logError("Failed to pack atlas");
            return nullptr;
        }
        atlas->discardStep = src->precision();
        atlas->packData = rects;
        return atlas;
    }

    std::shared_ptr<meta::Block> modelToImage(const std::shared_ptr<models::Image> &src,
                                              DArray<ImageResource> &resources)
    {
        if (src->signature() == assets::sign_block::image_atlas)
        {
            auto serializer = std::static_pointer_cast<models::Atlas>(src->serializer());
            return modelToImageAtlas(serializer, resources);
        }
        else if (src->signature() == assets::sign_block::image2D)
        {
            auto serializer = std::static_pointer_cast<models::Image2D>(src->serializer());
            return modelToImage2D(serializer, resources);
        }
        else
            return nullptr;
    }

    std::shared_ptr<assets::Target> modelToTarget(models::Target &src, DArray<ImageResource> &images)
    {
        auto target = std::make_shared<assets::Target>();
        target->addr = src.addr();
        target->header = src.header();
        target->checksum = src.checksum();
        return target;
    }

    std::shared_ptr<assets::Asset> modelToImageAny(std::shared_ptr<models::AssetBase> &src,
                                                   DArray<ImageResource> &resources)
    {
        if (!src)
        {
            logError("Source asset cannot be a null pointer");
            return nullptr;
        }
        std::shared_ptr<meta::Block> block;
        switch (src->assetInfo().type)
        {
            case assets::Type::Image:
            {
                block = modelToImage(std::static_pointer_cast<models::Image>(src), resources);
                break;
            }
            case assets::Type::Target:
            {
                block = modelToTarget(*std::static_pointer_cast<models::Target>(src), resources);
                break;
            }
            default:
                logError("Invalid asset type");
                return nullptr;
        }
        auto asset = std::make_shared<assets::Asset>();
        asset->header = src->assetInfo();
        if (!block) return nullptr;
        asset->blocks.push_back(block);
        return asset;
    }

    std::shared_ptr<assets::Material> modelToMaterial(const std::shared_ptr<models::Material> &src,
                                                      DArray<ImageResource> &resources)
    {
        auto material = std::make_shared<assets::Material>();
        for (auto &texture : src->textures())
            if (auto asset = modelToImageAny(texture, resources)) material->textures.push_back(*asset);
        material->albedo = src->albedo();
        return material;
    }

    std::shared_ptr<assets::Asset> modelToMaterialAny(std::shared_ptr<models::AssetBase> &src,
                                                      DArray<ImageResource> &resources, const std::string &name)
    {
        if (!src)
        {
            logError("Source asset cannot be a null pointer");
            return nullptr;
        }
        auto asset = std::make_shared<assets::Asset>();
        switch (src->assetInfo().type)
        {
            case assets::Type::Material:
            {
                auto model = std::static_pointer_cast<models::Material>(src);
                asset->header = model->assetInfo();
                if (auto material = modelToMaterial(model, resources))
                {
                    asset->blocks.push_back(material);
                    if (name.length() > 0)
                    {
                        auto matInfo = std::make_shared<assets::MaterialInfo>();
                        matInfo->name = name;
                        asset->blocks.push_back(matInfo);
                    }
                }
                else
                    return nullptr;
                break;
            }
            case assets::Type::Target:
            {
                auto model = std::static_pointer_cast<models::Target>(src);
                auto target = modelToTarget(*model, resources);
                if (!target) return nullptr;
                asset->blocks.push_back(target);
                asset->header = model->assetInfo();
                break;
            }
            default:
                logError("Invalid asset type");
                return nullptr;
        }
        return asset;
    }

    std::shared_ptr<assets::Scene> modelToScene(models::Scene &sceneInfo, DArray<ImageResource> &images)
    {
        auto scene = std::make_shared<assets::Scene>();
        for (int i = 0; i < sceneInfo.meshes().size(); i++)
        {
            auto &mesh = sceneInfo.meshes()[i];
            events::Manager e;
            switch (mesh->format())
            {
                case models::Mesh::Format::Obj:
                {
                    ecl::scene::obj::Importer importer(mesh->path());
                    if (importer.load(e) != io::file::ReadState::Success)
                    {
                        logError("Failed to load obj: %ls", mesh->path().c_str());
                        return nullptr;
                    }
                    for (auto &object : importer.objects()) scene->objects.push_back(object);
                    break;
                }
                default:
                    logError("Unsupported scene format");
                    return nullptr;
            }
        }

        DArray<ImageResource> imageResources{};
        for (auto &texture : sceneInfo.textures())
            if (auto asset = modelToImageAny(texture, imageResources)) scene->textures.push_back(*asset);

        for (auto &material : sceneInfo.materials())
            if (auto asset = modelToMaterialAny(material.asset, imageResources, material.name))
                scene->materials.push_back(*asset);

        return scene;
    }

    void prepareNodeByModel(const models::FileNode &src, assets::Library::Node &dst, DArray<ImageResource> &resources)
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
                std::shared_ptr<meta::Block> block;
                switch (src.asset->assetInfo().type)
                {
                    case assets::Type::Image:
                    {
                        block = modelToImage(std::static_pointer_cast<models::Image>(src.asset), resources);
                        break;
                    }
                    case assets::Type::Material:
                    {
                        block = modelToMaterial(std::static_pointer_cast<models::Material>(src.asset), resources);
                        break;
                    }
                    case assets::Type::Scene:
                    {
                        block = modelToScene(*std::static_pointer_cast<models::Scene>(src.asset), resources);
                        break;
                    }
                    case assets::Type::Target:
                    {
                        block = modelToTarget(*std::static_pointer_cast<models::Target>(src.asset), resources);
                        break;
                    }
                    default:
                        break;
                }
                if (!block) throw std::runtime_error("Failed to convert asset");
                dst.name = src.name;
                dst.asset.header = src.asset->assetInfo();
                dst.asset.blocks.push_back(block);
            }
        }
        else
        {
            dst.name = src.name;
            dst.isFolder = true;
            for (const auto &child : src.children)
            {
                assets::Library::Node node;
                prepareNodeByModel(child, node, resources);
                dst.children.push_back(node);
            }
        }
    }
} // namespace assettool