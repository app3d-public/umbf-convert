#ifndef ASSETTOOL_VALIDATE_H
#define ASSETTOOL_VALIDATE_H

#include <assets/asset.hpp>

namespace assettool
{

    void printMetaHeader(const std::shared_ptr<assets::Asset> &asset, assets::Type dstType);

    void printImage2D(const std::shared_ptr<assets::Image2D> &image);

    void printAtlas(const std::shared_ptr<assets::Atlas> &atlas);

    void printMaterialInfo(const std::shared_ptr<assets::Asset> &asset);

    void printSceneInfo(const std::shared_ptr<assets::Asset> &asset);

    void printTargetInfo(const std::shared_ptr<assets::Asset> &asset);

    void printLibraryInfo(const std::shared_ptr<assets::Asset> &asset);
} // namespace assettool

#endif