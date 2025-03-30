#include "jsonbase.hpp"
#include <acul/map.hpp>
#include <acul/string/sstream.hpp>
#include <assets/asset.hpp>
#include <fstream>
#include <vulkan/vulkan.hpp>

namespace models
{
    bool JsonBase::deserializeFromFile(const acul::string &path, rapidjson::Document &object)

    {
        std::ifstream stream(path.c_str());
        std::stringstream buffer;
        buffer << stream.rdbuf();
        stream.close();

        return deserializeString(buffer.str().c_str(), object);
    }

    bool JsonBase::deserializeFromFile(const acul::string &path)
    {
        rapidjson::Document object;
        return deserializeFromFile(path, object);
    }

    bool JsonBase::initDocument(const acul::string &s, rapidjson::Document &doc)
    {
        if (s.empty()) return false;
        return !doc.Parse(s.c_str()).HasParseError() ? true : false;
    }

    template <>
    bool getField<bool>(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsBool()) throw acul::runtime_error("Field " + acul::string(key) + " is not a bool");
            return val.GetBool();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return false;
    }

    template <>
    int getField<int>(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsInt()) throw acul::runtime_error("Field " + acul::string(key) + " is not an int");
            return val.GetInt();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return 0;
    }

    template <>
    i64 getField<i64>(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsInt64()) throw acul::runtime_error("Field " + acul::string(key) + " is not an i64");
            return val.GetInt64();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return 0;
    }

    template <>
    u64 getField<u64>(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsUint64()) throw acul::runtime_error("Field " + acul::string(key) + " is not an u64");
            return val.GetUint64();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return 0;
    }

    template <>
    f32 getField<f32>(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsFloat()) throw acul::runtime_error("Field " + acul::string(key) + " is not a float");
            return val.GetFloat();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return 0;
    }

    template <>
    f64 getField<f64>(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsDouble()) throw acul::runtime_error("Field " + acul::string(key) + " is not a double");
            return val.GetDouble();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return 0;
    }

    template <>
    acul::string getField<acul::string>(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsString()) throw acul::runtime_error("Field " + acul::string(key) + " is not a string");
            return val.GetString();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return "";
    }

#ifdef GetObject
    #undef GetObject
    #undef GetObjectA
#endif

    template <>
    rapidjson::Value::ConstObject getField<rapidjson::Value::ConstObject>(const rapidjson::Value &obj, const char *key,
                                                                          bool required)
    {
        static const rapidjson::Value emptyObject(rapidjson::kObjectType);

        if (obj.HasMember(key))
        {
            const rapidjson::Value &val = obj[key];
            if (!val.IsObject()) throw acul::runtime_error("Field " + acul::string(key) + " is not an object");
            return val.GetObjectA();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return emptyObject.GetObjectA();
    }

    template <>
    rapidjson::Value::ConstArray getField<rapidjson::Value::ConstArray>(const rapidjson::Value &obj, const char *key,
                                                                        bool required)
    {
        static const rapidjson::Value emptyArray(rapidjson::kArrayType);
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsArray()) throw acul::runtime_error("Field " + acul::string(key) + " is not an array");
            return val.GetArray();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return emptyArray.GetArray();
    }

    template <>
    glm::vec3 getField<glm::vec3>(const rapidjson::Value &obj, const char *key, bool required)
    {
        auto vec3 = getField<rapidjson::Value::ConstArray>(obj, key, required);
        return glm::vec3(vec3[0].GetFloat(), vec3[1].GetFloat(), vec3[2].GetFloat());
    }

    u16 getFormatField(const rapidjson::Value &obj, const char *key)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsString()) throw acul::runtime_error("Field " + acul::string(key) + " is not a string");
            std::string str = val.GetString();
            if (str == "material") return umbf::sign_block::format::material;
            if (str == "image") return umbf::sign_block::format::image;
            if (str == "scene") return umbf::sign_block::format::scene;
            if (str == "target") return umbf::sign_block::format::target;
            if (str == "library") return umbf::sign_block::format::library;
            if (str == "raw") return umbf::sign_block::format::raw;
            throw acul::runtime_error("Field " + acul::string(key) + " is not a valid asset type");
        }
        throw acul::runtime_error("Missing field " + acul::string(key));
        return umbf::sign_block::format::none;
    }

    vk::Format parseVkFormat(acul::string str)
    {
        static const acul::map<acul::string, vk::Format> formatMap = {
            {"R8G8B8A8_UNORM", vk::Format::eR8G8B8A8Unorm},
            {"R8G8B8A8_SNORM", vk::Format::eR8G8B8A8Snorm},
            {"R8G8B8A8_SRGB", vk::Format::eR8G8B8A8Srgb},
            {"R8G8B8A8_SINT", vk::Format::eR8G8B8A8Sint},
            {"R8G8B8A8_UINT", vk::Format::eR8G8B8A8Uint},
            {"R16G16B16A16_SFLOAT", vk::Format::eR16G16B16A16Sfloat},
            {"R16G16B16A16_SINT", vk::Format::eR16G16B16A16Sint},
            {"R16G16B16A16_UINT", vk::Format::eR16G16B16A16Uint},
            {"R32G32B32A32_SFLOAT", vk::Format::eR32G32B32A32Sfloat},
            {"R32G32B32A32_SINT", vk::Format::eR32G32B32A32Sint},
            {"R32G32B32A32_UINT", vk::Format::eR32G32B32A32Uint},
            {"B8G8R8A8_SRGB", vk::Format::eB8G8R8A8Srgb},
            {"B8G8R8A8_SINT", vk::Format::eB8G8R8A8Sint},
            {"B8G8R8A8_UINT", vk::Format::eB8G8R8A8Uint},
            {"B8G8R8A8_UNORM", vk::Format::eB8G8R8A8Unorm},
            {"B8G8R8A8_SNORM", vk::Format::eB8G8R8A8Snorm}};
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        auto it = formatMap.find(str);
        if (it == formatMap.end()) throw acul::runtime_error("Unknown format: " + str);
        return it->second;
    }

    template <>
    vk::Format getField<vk::Format>(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsString()) throw acul::runtime_error("Field " + acul::string(key) + " is not a string");
            return parseVkFormat(val.GetString());
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return vk::Format::eUndefined;
    }

    u32 getImageType(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsString()) throw acul::runtime_error("Field " + acul::string(key) + " is not a string");
            if (val == "2D") return umbf::sign_block::meta::image2D;
            if (val == "atlas") return umbf::sign_block::meta::image_atlas;
            throw acul::runtime_error("Field " + acul::string(key) + " is not a valid texture type");
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return 0;
    }
} // namespace models