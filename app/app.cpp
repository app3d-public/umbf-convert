#include "app.hpp"
#include <core/log.hpp>
#include "../check.hpp"
#include "../models/asset.hpp"

namespace assettool
{
    void App::run()
    {
        astl::shared_ptr<assets::Asset> asset;
        switch (_mode)
        {
            case SaveMode::Image:
                asset = getAssetByImage();
                break;
            case SaveMode::Scene:
                asset = getAssetByScene();
                break;
            case SaveMode::Json:
                asset = getAssetByJson();
                break;
            default:
                if (!checkAsset(_input)) _statusCode = EXIT_FAILURE;
                return;
        }
        if (!asset || !asset->save(_output))
        {
            logError("Failed to save asset: %s", _output.string().c_str());
            _statusCode = EXIT_FAILURE;
        }
        else
        {
            logInfo("Asset saved to disk: %s. Checksum: %u", _output.string().c_str(), asset->checksum);
            if (_check && !checkAsset(_output)) _statusCode = EXIT_FAILURE;
        };
    }

    bool App::checkAsset(const std::filesystem::path &path)
    {
        logInfo("Opening asset: %s", path.string().c_str());
        auto asset = assets::Asset::readFromFile(path);
        if (!asset)
        {
            logError("Failed to load asset: %s", path.string().c_str());
            return false;
        }
        switch (asset->header.type)
        {
            case assets::Type::Image:
                switch (asset->blocks.front()->signature())
                {
                    case assets::sign_block::image2D:
                    case assets::sign_block::image_atlas:
                        printMetaHeader(asset, assets::Type::Image);
                        printImage2D(astl::static_pointer_cast<assets::Image2D>(asset->blocks.front()));
                        if (asset->blocks.front()->signature() == assets::sign_block::image_atlas)
                            printAtlas(astl::static_pointer_cast<assets::Atlas>(asset->blocks.front()));
                        break;
                    default:
                        break;
                };
                break;
            case assets::Type::Material:
                printMaterialInfo(asset);
                break;
            case assets::Type::Scene:
                printSceneInfo(asset);
                break;
            case assets::Type::Target:
                printTargetInfo(asset);
                break;
            case assets::Type::Library:
                printLibraryInfo(asset);
                break;
            default:
                logError("Unsupported asset type: %s", assets::toString(asset->header.type).c_str());
                return false;
        }
        return true;
    }

    astl::shared_ptr<assets::Asset> App::getAssetByImage()
    {
        auto asset = astl::make_shared<assets::Asset>();
        asset->header.type = assets::Type::Image;
        asset->header.compressed = true;
        auto imageModel = astl::make_shared<models::Image2D>();
        imageModel->path(_input);
        auto image2D = modelToImage2D(imageModel, _images);
        if (!image2D) return nullptr;
        asset->blocks.push_back(image2D);
        return asset;
    }

    astl::shared_ptr<assets::Asset> App::getAssetByScene()
    {
        models::InfoHeader assetInfo{};
        assetInfo.type = assets::Type::Scene;
        assetInfo.compressed = true;
        astl::shared_ptr<models::Scene> sceneInfo = astl::make_shared<models::Scene>(assetInfo);
        astl::hashmap<std::string, models::Mesh::Format> formats = {{"obj", models::Mesh::Format::Obj}};
        auto ext = _input.extension().string();
        auto it = formats.find(ext.substr(1));
        if (it == formats.end())
        {
            logError("Unsupported scene format: %ls", _input.extension().c_str());
            return nullptr;
        }
        sceneInfo->meshes().push_back(astl::make_shared<models::Mesh>());
        auto &mesh = sceneInfo->meshes().back();
        mesh->path(_input);
        mesh->format(it->second);
        auto asset = astl::make_shared<assets::Asset>();
        asset->header = assetInfo;
        auto scene = modelToScene(*sceneInfo, _images);
        if (!scene) return nullptr;
        asset->blocks.push_back(scene);
        return asset;
    }

    astl::shared_ptr<assets::Asset> App::getAssetByJson()
    {
        models::InfoHeader assetInfo;
        rapidjson::Document json;
        if (!assetInfo.deserializeFromFile(_input, json))
        {
            logError("Failed to load asset: %ls", _input.c_str());
            return nullptr;
        }
        astl::shared_ptr<meta::Block> block;
        switch (assetInfo.type)
        {
            case assets::Type::Image:
            {
                auto model = astl::make_shared<models::Image>(assetInfo);
                if (!model->deserializeObject(json))
                {
                    logError("Failed to deserialize image configuration: %ls", _input.c_str());
                    return nullptr;
                }
                block = modelToImage(model, _images);
                break;
            }
            case assets::Type::Material:
            {
                astl::shared_ptr<models::Material> model = astl::make_shared<models::Material>(assetInfo);
                if (!model->deserializeObject(json))
                {
                    logError("Failed to deserialize material configuration: %ls", _input.c_str());
                    return nullptr;
                }
                block = modelToMaterial(model, _images);
                break;
            }
            case assets::Type::Scene:
            {
                std::shared_ptr<models::Scene> model = std::make_shared<models::Scene>(assetInfo);
                if (!model->deserializeObject(json))
                {
                    logError("Failed to deserialize scene configuration: %ls", _input.c_str());
                    return nullptr;
                }
                auto scene = modelToScene(*model, _images);
                block = scene;
                break;
            }
            case assets::Type::Target:
            {
                models::Target model(assetInfo);
                if (!model.deserializeObject(json))
                {
                    logError("Failed to deserialize target configuration: %ls", _input.c_str());
                    return nullptr;
                }
                block = modelToTarget(model, _images);
                break;
            }
            case assets::Type::Library:
            {
                models::Library model(assetInfo);
                if (!model.deserializeObject(json))
                {
                    logError("Failed to load library: %ls", _input.c_str());
                    return nullptr;
                }
                auto library = astl::make_shared<assets::Library>();
                prepareNodeByModel(model.fileTree(), library->fileTree, _images);
                block = library;
                break;
            }
            default:
                logError("Unsupported asset type");
                return nullptr;
        }
        if (!block) return nullptr;
        auto asset = astl::make_shared<assets::Asset>();
        asset->header = assetInfo;
        asset->blocks.push_back(block);
        return asset;
    }
} // namespace assettool