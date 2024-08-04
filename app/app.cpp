#include "app.hpp"
#include <assets/image.hpp>
#include <assets/material.hpp>
#include <assets/scene.hpp>
#include <core/log.hpp>
#include "../check.hpp"

namespace assettool
{
    void App::run()
    {
        std::shared_ptr<assets::Asset> asset;
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
            logInfo("Asset saved to disk: %s. Checksum: %u", _output.string().c_str(), asset->checksum());
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
        switch (asset->info().type)
        {
            case assets::Type::Image:
                printImageInfo(std::static_pointer_cast<assets::Image>(asset));
                break;
            case assets::Type::Material:
                printMaterialInfo(std::static_pointer_cast<assets::Material>(asset));
                break;
            case assets::Type::Scene:
                printSceneInfo(std::static_pointer_cast<assets::Scene>(asset));
                break;
            case assets::Type::Target:
                printTargetInfo(std::static_pointer_cast<assets::Target>(asset));
                break;
            case assets::Type::Library:
                printLibraryInfo(std::static_pointer_cast<assets::Library>(asset));
                break;
            default:
                logError("Unsupported asset type: %s", assets::toString(asset->info().type).c_str());
                return false;
        }
        return true;
    }

    std::shared_ptr<assets::Asset> App::getAssetByImage()
    {
        models::InfoHeader assetInfo{};
        assetInfo.type = assets::Type::Image;
        assetInfo.compressed = true;
        std::shared_ptr<models::Image2D> textureSerializer = std::make_shared<models::Image2D>();
        textureSerializer->path(_input);
        auto imageInfo = std::make_shared<models::Image>(assetInfo, textureSerializer, assets::ImageTypeFlagBits::t2D);
        return modelToImage(imageInfo, _images);
    }

    std::shared_ptr<assets::Asset> App::getAssetByScene()
    {
        models::InfoHeader assetInfo{};
        assetInfo.type = assets::Type::Scene;
        assetInfo.compressed = true;
        std::shared_ptr<models::Scene> sceneInfo = std::make_shared<models::Scene>(assetInfo);
        HashMap<std::string, models::Mesh::Format> formats = {{"obj", models::Mesh::Format::Obj}};
        auto ext = _input.extension().string();
        auto it = formats.find(ext.substr(1));
        if (it == formats.end())
        {
            logError("Unsupported scene format: %ls", _input.extension().c_str());
            return nullptr;
        }
        sceneInfo->meshes().push_back(std::make_shared<models::Mesh>());
        auto &mesh = sceneInfo->meshes().back();
        mesh->path(_input);
        mesh->format(it->second);
        return modelToScene(*sceneInfo, _images);
    }

    std::shared_ptr<assets::Asset> App::getAssetByJson()
    {
        models::InfoHeader assetInfo;
        rapidjson::Document json;
        if (!assetInfo.deserializeFromFile(_input, json))
        {
            logError("Failed to load asset: %ls", _input.c_str());
            return nullptr;
        }
        switch (assetInfo.type)
        {
            case assets::Type::Image:
            {
                auto model = std::make_shared<models::Image>(assetInfo);
                if (!model->deserializeObject(json))
                {
                    logError("Failed to deserialize image configuration: %ls", _input.c_str());
                    return nullptr;
                }
                return modelToImage(model, _images);
            }
            case assets::Type::Material:
            {
                std::shared_ptr<models::Material> model = std::make_shared<models::Material>(assetInfo);
                if (!model->deserializeObject(json))
                {
                    logError("Failed to deserialize material configuration: %ls", _input.c_str());
                    return nullptr;
                }
                return modelToMaterial(model, _images);
            }
            case assets::Type::Scene:
            {
                std::shared_ptr<models::Scene> model = std::make_shared<models::Scene>(assetInfo);
                if (!model->deserializeObject(json))
                {
                    logError("Failed to deserialize scene configuration: %ls", _input.c_str());
                    return nullptr;
                }
                return modelToScene(*model, _images);
            }
            case assets::Type::Target:
            {
                models::Target model(assetInfo);
                if (!model.deserializeObject(json))
                {
                    logError("Failed to deserialize target configuration: %ls", _input.c_str());
                    return nullptr;
                }
                return std::make_shared<assets::Target>(assetInfo, model);
            }
            case assets::Type::Library:
            {
                models::Library model(assetInfo);
                if (!model.deserializeObject(json))
                {
                    logError("Failed to load library: %ls", _input.c_str());
                    return nullptr;
                }
                assets::FileNode root;
                prepareNodeByModel(model.fileTree(), root, _images);
                return std::make_shared<assets::Library>(model.assetInfo(), root);
            }
            default:
                logError("Unsupported asset type");
                return nullptr;
        }
    }
} // namespace assettool