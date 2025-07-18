#pragma once

#include <umbf/umbf.hpp>
#include "jsonbase.hpp"

namespace models
{
    class UMBFRoot : public JsonBase
    {
    public:
        u16 type_sign = umbf::sign_block::format::none;

        UMBFRoot() : UMBFRoot(umbf::sign_block::format::none) {}
        virtual bool deserialize_object(const rapidjson::Value &obj) override;

    protected:
        UMBFRoot(u16 type) : type_sign(type) {}
    };

    class IPath final : public UMBFRoot
    {
    public:
        IPath(u16 type_id) : UMBFRoot(type_id) {}
        virtual bool deserialize_object(const rapidjson::Value &obj) override;

        void path(const acul::string &path) { _path = path; }
        acul::string path() const { return _path; }

    private:
        acul::string _path;
    };

    class Image final : public UMBFRoot
    {
    public:
        explicit Image(const acul::shared_ptr<UMBFRoot> &serializer = nullptr, u32 signature = 0)
            : UMBFRoot(umbf::sign_block::format::image), _signature(signature), _serializer(serializer)
        {
        }

        virtual bool deserialize_object(const rapidjson::Value &obj) override;

        acul::shared_ptr<UMBFRoot> serializer() const { return _serializer; }

        u32 signature() const { return _signature; }

    private:
        u32 _signature;
        acul::shared_ptr<UMBFRoot> _serializer;
    };

    class Atlas final : public UMBFRoot
    {
    public:
        explicit Atlas() : UMBFRoot(umbf::sign_block::format::image) {}

        virtual bool deserialize_object(const rapidjson::Value &obj) override;

        const acul::vector<acul::shared_ptr<IPath>> &images() const { return _images; }

        u64 width() const { return _width; }

        u64 height() const { return _height; }

        int precision() const { return _precision; }

        vk::Format format() const { return _format; }

        u8 bytes_per_channel() const { return _bytes_per_channel; }

    private:
        u64 _width;
        u64 _height;
        u8 _bytes_per_channel;
        vk::Format _format;
        int _precision;
        acul::vector<acul::shared_ptr<IPath>> _images;
    };

    class Material final : public UMBFRoot
    {
    public:
        explicit Material() : UMBFRoot(umbf::sign_block::format::material) {}
        const acul::vector<acul::shared_ptr<UMBFRoot>> &textures() const { return _textures; }

        umbf::MaterialNode albedo() const { return _albedo_node; }

        virtual bool deserialize_object(const rapidjson::Value &obj) override;

    private:
        acul::vector<acul::shared_ptr<UMBFRoot>> _textures;
        umbf::MaterialNode _albedo_node;

        static void parse_node_info(const rapidjson::Value &nodeInfo, umbf::MaterialNode &node);
    };

    class Mesh final : public JsonBase
    {
    public:
        virtual bool deserialize_object(const rapidjson::Value &obj) override;

        acul::string path() const { return _path; }
        void path(acul::string &path) { _path = path; }

        int mat_id() const { return _mat_id; }
        void mat_id(int mat_id) { _mat_id = mat_id; }

    private:
        acul::string _path;
        int _mat_id = -1;
    };

    class Scene final : public UMBFRoot
    {
    public:
        struct MaterialNode
        {
            acul::string name;
            acul::shared_ptr<UMBFRoot> asset;
        };

        explicit Scene() : UMBFRoot(umbf::sign_block::format::scene) {}

        acul::vector<acul::shared_ptr<Mesh>> &meshes() { return _meshes; }

        const acul::vector<acul::shared_ptr<UMBFRoot>> &textures() const { return _textures; }

        const acul::vector<MaterialNode> &materials() const { return _materials; }

        virtual bool deserialize_object(const rapidjson::Value &obj) override;

    private:
        acul::vector<acul::shared_ptr<Mesh>> _meshes;
        acul::vector<acul::shared_ptr<UMBFRoot>> _textures;
        acul::vector<MaterialNode> _materials;

        bool deserialize_meshes(const rapidjson::Value &obj);
        bool deserialize_textures(const rapidjson::Value &obj);
        bool deserialize_materials(const rapidjson::Value &obj);
    };

    class Target final : public UMBFRoot
    {
    public:
        Target() : UMBFRoot(umbf::sign_block::format::target) {}

        virtual bool deserialize_object(const rapidjson::Value &obj) override;

        acul::string url() const { return _url; }

        umbf::File::Header header() const { return _header; }

        u32 checksum() const { return _checksum; }

    private:
        umbf::File::Header _header;
        acul::string _url;
        u32 _checksum;
    };

    struct FileNode
    {
        acul::string name;                // Name of the file node.
        acul::vector<FileNode> children;  // Child nodes of this file node.
        bool is_folder{false};             // Flag indicating if the node is a folder.
        acul::shared_ptr<UMBFRoot> asset; // Shared pointer to the asset associated with the node.
    };

    class Library final : public UMBFRoot
    {
    public:
        explicit Library() : UMBFRoot(umbf::sign_block::format::library) {}

        virtual bool deserialize_object(const rapidjson::Value &obj) override
        {
            return parse_file_tree(obj, _file_tree);
        }

        const FileNode &file_tree() const { return _file_tree; }

    private:
        FileNode _file_tree;

        static bool parse_file_tree(const rapidjson::Value &obj, FileNode &node);

        static acul::shared_ptr<UMBFRoot> parse_asset(const rapidjson::Value &obj, FileNode &node);
    };
} // namespace models