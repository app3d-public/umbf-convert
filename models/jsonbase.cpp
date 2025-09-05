#ifdef GetObject
    #undef GetObject
    #undef GetObjectA
#endif

#include "jsonbase.hpp"
#include <acul/map.hpp>
#include <acul/string/sstream.hpp>
#include <fstream>
#include <umbf/umbf.hpp>

namespace models
{
    bool JsonBase::deserialize_from_file(const acul::string &path, rapidjson::Document &object)

    {
        std::ifstream stream(path.c_str());
        acul::stringstream buffer;
        buffer << stream.rdbuf();
        stream.close();

        return deserialize_string(buffer.str().c_str(), object);
    }

    bool JsonBase::deserialize_from_file(const acul::string &path)
    {
        rapidjson::Document object;
        return deserialize_from_file(path, object);
    }

    bool JsonBase::init_document(const acul::string &s, rapidjson::Document &doc)
    {
        if (s.empty()) return false;
        return !doc.Parse(s.c_str()).HasParseError() ? true : false;
    }

    template <>
    bool get_field<bool>(const rapidjson::Value &obj, const char *key, bool required)
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
    int get_field<int>(const rapidjson::Value &obj, const char *key, bool required)
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
    i64 get_field<i64>(const rapidjson::Value &obj, const char *key, bool required)
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
    u64 get_field<u64>(const rapidjson::Value &obj, const char *key, bool required)
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
    f32 get_field<f32>(const rapidjson::Value &obj, const char *key, bool required)
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
    f64 get_field<f64>(const rapidjson::Value &obj, const char *key, bool required)
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
    acul::string get_field<acul::string>(const rapidjson::Value &obj, const char *key, bool required)
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

    template <>
    rapidjson::Value::ConstObject get_field<rapidjson::Value::ConstObject>(const rapidjson::Value &obj, const char *key,
                                                                           bool required)
    {
        static const rapidjson::Value empty_object(rapidjson::kObjectType);

        if (obj.HasMember(key))
        {
            const rapidjson::Value &val = obj[key];
            if (!val.IsObject()) throw acul::runtime_error("Field " + acul::string(key) + " is not an object");
            return val.GetObject();
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return empty_object.GetObject();
    }

    template <>
    rapidjson::Value::ConstArray get_field<rapidjson::Value::ConstArray>(const rapidjson::Value &obj, const char *key,
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
    umbf::ImageFormat::Type::enum_type get_field<umbf::ImageFormat::Type::enum_type>(const rapidjson::Value &obj,
                                                                                     const char *key, bool required)
    {
        acul::string str = get_field<acul::string>(obj, key, required);
        if (str == "uint")
            return umbf::ImageFormat::Type::uint;
        else if (str == "sfloat")
            return umbf::ImageFormat::Type::sfloat;
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return umbf::ImageFormat::Type::none;
    }

    template <>
    amal::vec3 get_field<amal::vec3>(const rapidjson::Value &obj, const char *key, bool required)
    {
        auto vec3 = get_field<rapidjson::Value::ConstArray>(obj, key, required);
        return amal::vec3(vec3[0].GetFloat(), vec3[1].GetFloat(), vec3[2].GetFloat());
    }

    u16 get_format_field(const rapidjson::Value &obj, const char *key)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsString()) throw acul::runtime_error("Field " + acul::string(key) + " is not a string");
            acul::string str = val.GetString();
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

    u32 get_image_type(const rapidjson::Value &obj, const char *key, bool required)
    {
        if (obj.HasMember(key))
        {
            auto &val = obj[key];
            if (!val.IsString()) throw acul::runtime_error("Field " + acul::string(key) + " is not a string");
            if (val == "2D") return umbf::sign_block::image;
            if (val == "atlas") return umbf::sign_block::image_atlas;
            throw acul::runtime_error("Field " + acul::string(key) + " is not a valid texture type");
        }
        if (required) throw acul::runtime_error("Missing field " + acul::string(key));
        return 0;
    }
} // namespace models