#ifndef ASSETTOOL_MODELS_BASE
#define ASSETTOOL_MODELS_BASE

#include <acul/string/string.hpp>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

namespace models
{
    class JsonBase
    {
    public:
        bool deserialize_from_file(const acul::string &path, rapidjson::Document &object);

        bool deserialize_from_file(const acul::string &path);

        bool deserialize_string(const char *s, rapidjson::Document &object)
        {
            if (!init_document(s, object)) return false;
            return deserialize_object(object);
        }

        bool deserialize_string(const char *s)
        {
            rapidjson::Document doc;
            return init_document(s, doc);
        }

        virtual bool deserialize_object(const rapidjson::Value &obj) = 0;

    protected:
        static bool init_document(const acul::string &s, rapidjson::Document &doc);
    };

    template <typename T>
    T get_field(const rapidjson::Value &obj, const char *key, bool required = true);

    u32 get_image_type(const rapidjson::Value &obj, const char *key, bool required);

    u16 get_format_field(const rapidjson::Value &obj, const char *key);
} // namespace models

#endif