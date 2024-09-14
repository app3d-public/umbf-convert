#include "check.hpp"
#include <core/log.hpp>
#include <memory>

namespace assettool
{

    void printMetaHeader(const std::shared_ptr<assets::Asset> &asset, assets::Type dstType)
    {
        logInfo("--------- Meta Header --------");
        logInfo("Type: %s", assets::toString(asset->header.type).c_str());
        logInfo("Checksum: %u", asset->checksum);
        if (asset->header.type != dstType)
        {
            logError("Invalid asset type");
            return;
        }
        logInfo("Compressed: %s", asset->header.compressed ? "true" : "false");
        auto it = asset->blocks.begin();
        if (it == asset->blocks.end())
            logInfo("No meta data");
        else
        {
            while (it != asset->blocks.end())
            {
                u32 signature = (*it)->signature();
                logInfo("Optional meta block: 0x%08x", signature);
                ++it;
            }
        }
    }

    void printImage2D(const std::shared_ptr<assets::Image2D> &image)
    {
        logInfo("--------- Image Info ---------");
        logInfo("Width: %d", image->width);
        logInfo("Height: %d", image->height);
        std::stringstream ss;
        for (size_t i = 0; i < image->channelCount; ++i)
        {
            ss << image->channelNames[i];
            if (i < image->channelCount - 1) ss << ", ";
        }
        logInfo("Channels: (%d) %s", image->channelCount, ss.str().c_str());
        logInfo("Bytes per channel: %d bit", image->bytesPerChannel * 8);
        logInfo("Image format: %s", vk::to_string(image->imageFormat).c_str());
        logInfo("Size: %llu", image->imageSize());
        scalable_free(image->pixels);
    }

    void printAtlas(const std::shared_ptr<assets::Atlas> &atlas)
    {
        logInfo("--------- Atlas Info ---------");
        logInfo("Images size: %zu", atlas->packData.size());
        for (const auto &rect : atlas->packData)
        {
            logInfo("------------------------------");
            logInfo("Width: %d", rect.w);
            logInfo("Height: %d", rect.h);
            logInfo("X: %d", rect.x);
            logInfo("Y: %d", rect.y);
        }
    }

    void printMaterialNode(assets::MaterialNode &node, astl::vector<assets::Asset> &textures)
    {
        if (node.textured)
        {
            logInfo("    Textured: true");
            auto tid = node.textureID;
            if (tid < 0 || tid >= textures.size()) throw std::runtime_error("Invalid texture ID");
            logInfo("    TextureID: %d", tid);
            logInfo("    Texture type: %s", assets::toString(textures[tid].header.type).c_str());
        }
        else
        {
            logInfo("    Textured: false");
            glm::vec3 color = node.rgb;
            logInfo("    Color: %f %f %f", color.x, color.y, color.z);
        }
    }

    void printMaterialInfo(const std::shared_ptr<assets::Asset> &asset)
    {
        printMetaHeader(asset, assets::Type::Material);
        logInfo("--------- Material Info -------");
        auto material = std::static_pointer_cast<assets::Material>(*asset->blocks.begin());
        auto &textures = material->textures;
        logInfo("Textures size: %zu", textures.size());
        logInfo("Albedo:");
        printMaterialNode(material->albedo, textures);
        logInfo("------------------------------");
    }

    void printSceneInfo(const std::shared_ptr<assets::Asset> &asset)
    {
        printMetaHeader(asset, assets::Type::Scene);
        auto scene = std::static_pointer_cast<assets::Scene>(asset->blocks.front());
        logInfo("--------- Scene Info ---------");
        auto &objects = scene->objects;
        logInfo("Objects size: %zu", objects.size());
        for (auto &object : objects)
        {
            auto &pos = object.transform.position;
            auto &rot = object.transform.rotation;
            auto &scale = object.transform.scale;
            logInfo("------------------------------");
            logInfo("Position: %f %f %f", pos.x, pos.y, pos.z);
            logInfo("Rotation: %f %f %f", rot.x, rot.y, rot.z);
            logInfo("Scale: %f %f %f", scale.x, scale.y, scale.z);
            if (object.meta.begin() == object.meta.end())
                logInfo("Meta: no");
            else
                for (auto &block : object.meta) logInfo("Meta block signature: 0x%08x", block->signature());
            logInfo("------------------------------");
        }
        logInfo("--------- Textures Info -----");
        logInfo("Textures size: %zu", scene->textures.size());
        int count = 0;
        for (auto &texture : scene->textures)
        {
            switch (texture.header.type)
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
                    logError("#%d | Incompatible type: %s", count, assets::toString(texture.header.type).c_str());
                    break;
            }
            ++count;
        }
        logInfo("--------- Materials Info -----");
        logInfo("Materials size: %zu", scene->materials.size());
        count = 0;
        for (auto &asset : scene->materials)
        {
            switch (asset.header.type)
            {
                case assets::Type::Invalid:
                    logWarn("#%d | Type: Invalid", count++);
                    break;
                case assets::Type::Target:
                    logInfo("#%d | Type: Target", count++);
                    break;
                case assets::Type::Material:
                {
                    logInfo("#%d | Type: Material", count++);
                    auto it = std::find_if(asset.blocks.begin(), asset.blocks.end(), [](auto &block) {
                        return block->signature() == assets::sign_block::material_info;
                    });
                    if (it == asset.blocks.end())
                        logInfo("   | Name: null");
                    else
                    {
                        auto matMeta = std::static_pointer_cast<assets::MaterialInfo>(*it);
                        logInfo("   | Name: %s", matMeta->name.c_str());
                    }
                    break;
                }
                default:
                    logError("#%d | Incompatible type: %s", count++, assets::toString(asset.header.type).c_str());
                    break;
            }
        }
        logInfo("------------------------------");
    }

    void printTargetInfo(const std::shared_ptr<assets::Asset> &asset)
    {
        printMetaHeader(asset, assets::Type::Target);
        logInfo("--------- Target Info ---------");
        auto target = std::static_pointer_cast<assets::Target>(asset->blocks.front());
        switch (target->addr.proto)
        {
            case assets::Target::Addr::Proto::File:
                logInfo("Proto: File");
                break;
            case assets::Target::Addr::Proto::Network:
                logInfo("Proto: Network");
                break;
            default:
                logWarn("Proto: Unknown");
                break;
        }
        logInfo("URL: %s", target->addr.url.c_str());
        logInfo("Type: %s", assets::toString(target->header.type).c_str());
        logInfo("Checksum: %d", target->checksum);
        logInfo("------------------------------");
    }

    void printFileHierarchy(const assets::Library::Node &node, int depth = 0, const std::string &prefix = "")
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

    void printLibraryInfo(const std::shared_ptr<assets::Asset> &asset)
    {
        printMetaHeader(asset, assets::Type::Library);
        logInfo("--------- Library Info -------");
        auto library = std::static_pointer_cast<assets::Library>(asset->blocks.front());
        printFileHierarchy(library->fileTree);
        logInfo("------------------------------");
    }
} // namespace assettool