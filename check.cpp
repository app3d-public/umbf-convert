#include "check.hpp"
#include <core/log.hpp>

namespace assettool
{

    void printMetaHeader(const std::shared_ptr<assets::Asset> &asset, assets::Type dstType)
    {
        logInfo("--------- Meta Header --------");
        const auto &assetInfo = asset->info();
        logInfo("Type: %s", assets::toString(assetInfo.type).c_str());
        logInfo("Checksum: %u", asset->checksum());
        if (assetInfo.type != dstType)
        {
            logError("Invalid asset type");
            return;
        }
        logInfo("Compressed: %s", assetInfo.compressed ? "true" : "false");
    }

    void printImageInfo(const std::shared_ptr<assets::Image> &image)
    {
        printMetaHeader(image, assets::Type::Image);
        std::shared_ptr<assets::ImageInfo> imageInfo;
        bool isAtlas{false};
        if (image->flags() & assets::ImageTypeFlagBits::tAtlas)
        {
            imageInfo = std::static_pointer_cast<assets::Atlas>(image->stream());
            isAtlas = true;
        }
        else if (image->flags() & assets::ImageTypeFlagBits::t2D)
            imageInfo = std::static_pointer_cast<assets::Image2D>(image->stream());
        else
            throw std::runtime_error("Unsupported texture type");
        logInfo("--------- Image Info ---------");
        logInfo("Width: %d", imageInfo->width);
        logInfo("Height: %d", imageInfo->height);
        std::stringstream ss;
        for (size_t i = 0; i < imageInfo->channelCount; ++i)
        {
            ss << imageInfo->channelNames[i];
            if (i < imageInfo->channelCount - 1) ss << ", ";
        }
        logInfo("Channels: (%d) %s", imageInfo->channelCount, ss.str().c_str());
        logInfo("Bytes per channel: %d bit", imageInfo->bytesPerChannel * 8);
        logInfo("Image format: %s", vk::to_string(imageInfo->imageFormat).c_str());
        logInfo("Size: %llu", imageInfo->imageSize());
        logInfo("Mip levels: %d", imageInfo->mipLevels);
        scalable_free(imageInfo->pixels);
        if (isAtlas)
        {
            auto atlas = std::static_pointer_cast<assets::Atlas>(image->stream());
            logInfo("--------- Atlas Info ---------");
            logInfo("Images size: %zu", atlas->packData().size());
            for (const auto &rect : atlas->packData())
            {
                logInfo("------------------------------");
                logInfo("Width: %d", rect.w);
                logInfo("Height: %d", rect.h);
                logInfo("X: %d", rect.x);
                logInfo("Y: %d", rect.y);
            }
        }
        logInfo("------------------------------");
    }

    void printMaterialNode(assets::MaterialNode &node, DArray<std::shared_ptr<assets::Asset>> &textures)
    {
        if (node.textured)
        {
            logInfo("    Textured: true");
            auto tid = node.textureID;
            if (tid < 0 || tid >= textures.size()) throw std::runtime_error("Invalid texture ID");
            logInfo("    TextureID: %d", tid);
            logInfo("    Texture type: %s", assets::toString(textures[tid]->info().type).c_str());
        }
        else
        {
            logInfo("    Textured: false");
            glm::vec3 color = node.rgb;
            logInfo("    Color: %f %f %f", color.x, color.y, color.z);
        }
    }

    void printMaterialInfo(const std::shared_ptr<assets::Material> &material)
    {
        printMetaHeader(material, assets::Type::Material);
        logInfo("--------- Material Info -------");
        auto &textures = material->textures;
        logInfo("Textures size: %zu", textures.size());
        logInfo("Albedo:");
        printMaterialNode(material->info.albedo, textures);
        logInfo("------------------------------");
    }

    void printSceneInfo(const std::shared_ptr<assets::Scene> &scene)
    {
        printMetaHeader(scene, assets::Type::Scene);
        logInfo("--------- Scene Info ---------");
        auto project = scene->meta;
        logInfo("Author: %s", project.author.c_str());
        logInfo("Project info: %s", project.info.c_str());
        auto &objects = scene->objects;
        logInfo("Objects size: %zu", objects.size());
        for (auto &object : objects)
        {
            auto &pos = object->transform.position;
            auto &rot = object->transform.rotation;
            auto &scale = object->transform.scale;
            logInfo("------------------------------");
            logInfo("Position: %f %f %f", pos.x, pos.y, pos.z);
            logInfo("Rotation: %f %f %f", rot.x, rot.y, rot.z);
            logInfo("Scale: %f %f %f", scale.x, scale.y, scale.z);
            logInfo("Material ID: %d", object->matID);
            if (!object->meta)
            {
                logInfo("Meta block: null");
                continue;
            }
            else
                logInfo("Signature: 0x%08x", object->meta->signature());
            logInfo("------------------------------");
        }
        logInfo("--------- Textures Info -----");
        logInfo("Textures size: %zu", scene->textures.size());
        int count = 0;
        for (auto &texture : scene->textures)
        {
            switch (texture->info().type)
            {
                case assets::Type::Invalid:
                    logWarn("#%d | Type: Invalid", count);
                    break;
                case assets::Type::Target:
                    logInfo("#%d | Type: Target", count);
                    break;
                case assets::Type::Image:
                    logInfo("#%d | Type: Image", count);
                    break;
                default:
                    logError("#%d | Incompatible type: %s", count, assets::toString(texture->info().type).c_str());
                    break;
            }
            ++count;
        }
        logInfo("--------- Materials Info -----");
        logInfo("Materials size: %zu", scene->materials.size());
        count = 0;
        for (auto &node : scene->materials)
        {
            logInfo("#%d | Name: %s", count++, node.name.c_str());
            auto &info = node.asset->info();
            switch (info.type)
            {
                case assets::Type::Invalid:
                    logWarn("   | Type: Invalid");
                    break;
                case assets::Type::Target:
                    logInfo("   | Type: Target");
                    break;
                case assets::Type::Material:
                    logInfo("   | Type: Material");
                    break;
                default:
                    logError("   | Incompatible type: %s", assets::toString(info.type).c_str());
                    break;
            }
        }
        logInfo("------------------------------");
    }

    void printTargetInfo(const std::shared_ptr<assets::Target> &target)
    {
        printMetaHeader(target, assets::Type::Target);
        logInfo("--------- Target Info ---------");
        assets::TargetInfo targetInfo = target->targetInfo();
        switch (targetInfo.proto)
        {
            case assets::TargetInfo::Proto::File:
                logInfo("Proto: File");
                break;
            case assets::TargetInfo::Proto::Network:
                logInfo("Proto: Network");
                break;
            default:
                logWarn("Proto: Unknown");
                break;
        }
        logInfo("URL: %s", targetInfo.url.c_str());
        logInfo("Type: %s", assets::toString(targetInfo.type).c_str());
        logInfo("Checksum: %d", targetInfo.checksum);
        logInfo("------------------------------");
    }

    void printFileHierarchy(const assets::FileNode &node, int depth = 0, const std::string &prefix = "")
    {
        if (depth == 0)
            logInfo("| %s", node.name.c_str());
        else
            logInfo("%s%s %s", prefix.c_str(), (const char *)(node.isFolder ? "|----" : "|____"), node.name.c_str());

        std::string newPrefix = prefix + (depth > 0 ? "|     " : "");

        for (size_t i = 0; i < node.children.size(); ++i)
            if (i == node.children.size() - 1)
                printFileHierarchy(node.children[i], depth + 1, prefix + (depth > 0 ? "      " : ""));
            else
                printFileHierarchy(node.children[i], depth + 1, newPrefix);
    }

    void printLibraryInfo(const std::shared_ptr<assets::Library> &library)
    {
        printMetaHeader(library, assets::Type::Library);
        logInfo("--------- Library Info -------");
        printFileHierarchy(library->fileTree());
        logInfo("------------------------------");
    }
} // namespace assettool