#ifndef ASSETTOOL_MODELS_ASSET
#define ASSETTOOL_MODELS_ASSET

#include <assets/asset.hpp>
#include <assets/image.hpp>
#include <assets/library.hpp>
#include <assets/material.hpp>
#include "jsonbase.hpp"

namespace models
{
    class InfoHeader : public JsonBase, public assets::InfoHeader
    {
    public:
        virtual bool deserializeObject(const rapidjson::Value &obj) override;
    };

    class AssetBase
    {
    public:
        explicit AssetBase(const InfoHeader &assetInfo) : _assetInfo(assetInfo) {}
        virtual ~AssetBase() = default;

        const InfoHeader &assetInfo() const { return _assetInfo; }

    protected:
        InfoHeader _assetInfo;
    };

    class Image final : public JsonBase, public AssetBase
    {
    public:
        explicit Image(const InfoHeader &assetInfo, const std::shared_ptr<JsonBase> &serializer = nullptr,
                       assets::ImageTypeFlags flags = assets::ImageTypeFlagBits::tUndefined)
            : AssetBase(assetInfo), _type(flags), _serializer(serializer)
        {
        }

        virtual bool deserializeObject(const rapidjson::Value &obj) override;

        std::shared_ptr<JsonBase> &serializer() { return _serializer; }

        assets::ImageTypeFlags type() const { return _type; }

    private:
        assets::ImageTypeFlags _type;
        std::shared_ptr<JsonBase> _serializer;
    };

    class Image2D final : public JsonBase
    {
    public:
        virtual bool deserializeObject(const rapidjson::Value &obj) override;

        void path(const std::filesystem::path &path) { _path = path; }
        std::filesystem::path path() const { return _path; }

    private:
        std::filesystem::path _path;
    };

    class Atlas final : public JsonBase
    {
    public:
        Atlas() = default;

        virtual bool deserializeObject(const rapidjson::Value &obj) override;

        DArray<std::shared_ptr<Image2D>> &images() { return _images; }

        u64 width() const { return _width; }

        u64 height() const { return _height; }

        int precision() const { return _precision; }

        vk::Format imageFormat() const { return _imageFormat; }

        u8 bytesPerChannel() const { return _bytesPerChannel; }

        u32 mipLevels() const { return _mipLevels; }

    private:
        u64 _width;
        u64 _height;
        u8 _bytesPerChannel;
        vk::Format _imageFormat;
        u32 _mipLevels;
        int _precision;
        DArray<std::shared_ptr<Image2D>> _images;
    };

    class Material final : public JsonBase, public AssetBase
    {
    public:
        explicit Material(const InfoHeader &assetInfo) : AssetBase(assetInfo) {}

        const DArray<std::shared_ptr<AssetBase>> &textures() const { return _textures; }

        assets::MaterialNode albedo() { return _albedoNode; }

        virtual bool deserializeObject(const rapidjson::Value &obj) override;

    private:
        DArray<std::shared_ptr<AssetBase>> _textures;
        assets::MaterialNode _albedoNode;

        static void parseNodeInfo(const rapidjson::Value &nodeInfo, assets::MaterialNode &node);
    };

    class Mesh final : public JsonBase
    {
    public:
        enum class Format
        {
            Unknown,
            Obj
        };

        virtual bool deserializeObject(const rapidjson::Value &obj) override;

        std::filesystem::path path() const { return _path; }
        void path(const std::filesystem::path &path) { _path = path; }

        Format format() const { return _format; }
        void format(Format format) { _format = format; }

        int matID() const { return _matID; }
        void matID(int matID) { _matID = matID; }

    private:
        std::filesystem::path _path;
        Format _format;
        int _matID = -1;
    };

    class Scene final : public JsonBase, public AssetBase
    {
    public:
        struct MaterialNode
        {
            std::string name;
            std::shared_ptr<AssetBase> asset;
        };

        explicit Scene(const InfoHeader &assetInfo) : AssetBase(assetInfo) {}

        DArray<std::shared_ptr<Mesh>> &meshes() { return _meshes; }

        const DArray<std::shared_ptr<AssetBase>> &textures() const { return _textures; }

        const DArray<MaterialNode> &materials() const { return _materials; }

        virtual bool deserializeObject(const rapidjson::Value &obj) override;

    private:
        DArray<std::shared_ptr<Mesh>> _meshes;
        DArray<std::shared_ptr<AssetBase>> _textures;
        DArray<MaterialNode> _materials;

        bool deserializeMeshes(const rapidjson::Value &obj);
        bool deserializeTextures(const rapidjson::Value &obj);
        bool deserializeMaterials(const rapidjson::Value &obj);
    };

    class Target final : public JsonBase, public AssetBase, public assets::TargetInfo
    {
    public:
        explicit Target(const models::InfoHeader &assetInfo) : AssetBase(assetInfo) {}

        virtual bool deserializeObject(const rapidjson::Value &obj) override;
    };

    struct FileNode
    {
        std::string name;                 // Name of the file node.
        DArray<FileNode> children;        // Child nodes of this file node.
        bool isFolder{false};             // Flag indicating if the node is a folder.
        std::shared_ptr<AssetBase> asset; // Shared pointer to the asset associated with the node.
    };

    class Library final : public JsonBase, public AssetBase
    {
    public:
        explicit Library(const InfoHeader &assetInfo) : AssetBase(assetInfo) {}

        virtual bool deserializeObject(const rapidjson::Value &obj) override;

        const FileNode &fileTree() const { return _fileTree; }

    private:
        FileNode _fileTree;

        static bool parseFileTree(const rapidjson::Value &obj, FileNode &node);

        static std::shared_ptr<AssetBase> parseAsset(const rapidjson::Value &obj, FileNode &node);
    };

} // namespace models

#endif