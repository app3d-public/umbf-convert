#pragma once

#include <ecl/image/import.hpp>
#include "models/asset.hpp"

namespace assettool
{
    class ImageResource
    {
    public:
        explicit ImageResource(const std::filesystem::path &path);

        ~ImageResource() { astl::release(_importer); }

        ImageResource(ImageResource &&other) noexcept
            : _importer(std::move(other._importer)), _image(std::move(other._image)), _valid(other._valid)
        {
            other._importer = nullptr;
            other._valid = false;
        }

        bool valid() const { return _valid; }

        assets::Image2D &image() { return _image; }

    private:
        ecl::image::ILoader *_importer;
        assets::Image2D _image;
        bool _valid{false};
    };

    astl::shared_ptr<assets::Image2D> modelToImage2D(const astl::shared_ptr<models::Image2D> &src,
                                                     astl::vector<ImageResource> &resources);

    astl::shared_ptr<meta::Block> modelToImageAtlas(const astl::shared_ptr<models::Atlas> &src,
                                                    astl::vector<ImageResource> &resources);

    astl::shared_ptr<meta::Block> modelToImage(const astl::shared_ptr<models::Image> &src,
                                               astl::vector<ImageResource> &resources);

    astl::shared_ptr<assets::Asset> modelToImageAny(astl::shared_ptr<models::AssetBase> &src,
                                                    astl::vector<ImageResource> &resources);

    astl::shared_ptr<assets::Material> modelToMaterial(const astl::shared_ptr<models::Material> &src,
                                                       astl::vector<ImageResource> &resources);

    astl::shared_ptr<assets::Asset> modelToMaterialAny(astl::shared_ptr<models::AssetBase> &src,
                                                       astl::vector<ImageResource> &resources,
                                                       const std::string &name = "");

    astl::shared_ptr<assets::Scene> modelToScene(models::Scene &sceneInfo, astl::vector<ImageResource> &images);

    astl::shared_ptr<assets::Target> modelToTarget(models::Target &targetInfo, astl::vector<ImageResource> &images);

    void prepareNodeByModel(const models::FileNode &src, assets::Library::Node &dst,
                            astl::vector<ImageResource> &resources);
} // namespace assettool