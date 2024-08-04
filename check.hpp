#ifndef ASSETTOOL_VALIDATE_H
#define ASSETTOOL_VALIDATE_H

#include <assets/image.hpp>
#include <assets/library.hpp>
#include <assets/material.hpp>
#include <assets/scene.hpp>

namespace assettool
{

    void printMetaHeader(const std::shared_ptr<assets::Asset> &asset, assets::Type dstType);

    void printImageInfo(const std::shared_ptr<assets::Image> &image);

    void printMaterialInfo(const std::shared_ptr<assets::Material> &material);

    void printSceneInfo(const std::shared_ptr<assets::Scene> &scene);

    void printTargetInfo(const std::shared_ptr<assets::Target> &target);

    void printLibraryInfo(const std::shared_ptr<assets::Library> &library);
} // namespace assettool

#endif