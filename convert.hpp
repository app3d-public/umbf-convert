#pragma once

#include <assets/scene.hpp>
#include <ecl/image/import.hpp>
#include "models/asset.hpp"

namespace assettool
{
    class ImageResource
    {
    public:
        explicit ImageResource(const std::filesystem::path &path);

        ~ImageResource() { delete _importer; }

        ImageResource(ImageResource &&other) noexcept
            : _importer(std::move(other._importer)), _image(std::move(other._image)), _valid(other._valid)
        {
            other._importer = nullptr;
            other._valid = false;
        }

        bool valid() const { return _valid; }

        assets::ImageInfo &image() { return _image; }

    private:
        ecl::image::ILoader *_importer;
        assets::ImageInfo _image;
        bool _valid{false};
    };

    std::shared_ptr<assets::Image> modelToImage2D(const std::shared_ptr<models::Image2D> &src,
                                                  DArray<ImageResource> &resources, const models::InfoHeader &info);

    std::shared_ptr<assets::Image> modelToImageAtlas(const std::shared_ptr<models::Atlas> &src,
                                                     DArray<ImageResource> &resources, const models::InfoHeader &info);

    std::shared_ptr<assets::Image> modelToImage(const std::shared_ptr<models::Image> &src,
                                                DArray<ImageResource> &resources);

    std::shared_ptr<assets::Asset> modelToImageAny(std::shared_ptr<models::AssetBase> &src,
                                                   DArray<ImageResource> &resources);

    std::shared_ptr<assets::Material> modelToMaterial(const std::shared_ptr<models::Material> &src,
                                                      DArray<ImageResource> &resources);

    std::shared_ptr<assets::Asset> modelToMaterialAny(std::shared_ptr<models::AssetBase> &src,
                                                      DArray<ImageResource> &resources, const std::string &name = "");

    std::shared_ptr<assets::Scene> modelToScene(models::Scene &sceneInfo, DArray<ImageResource> &images);

    void prepareNodeByModel(const models::FileNode &src, assets::FileNode &dst, DArray<ImageResource> &resources);
} // namespace assettool