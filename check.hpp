#ifndef ASSETTOOL_VALIDATE_H
#define ASSETTOOL_VALIDATE_H

#include <assets/asset.hpp>

namespace assettool
{

    void printMetaHeader(const astl::shared_ptr<assets::Asset> &asset, assets::Type dstType);

    void printImage2D(const astl::shared_ptr<assets::Image2D> &image);

    void printAtlas(const astl::shared_ptr<assets::Atlas> &atlas);

    void printMaterialInfo(const astl::shared_ptr<assets::Asset> &asset);

    void printSceneInfo(const astl::shared_ptr<assets::Asset> &asset);

    void printTargetInfo(const astl::shared_ptr<assets::Asset> &asset);

    void printLibraryInfo(const astl::shared_ptr<assets::Asset> &asset);
} // namespace assettool

#endif