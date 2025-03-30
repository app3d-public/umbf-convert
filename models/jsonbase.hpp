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
        bool deserializeFromFile(const acul::string &path, rapidjson::Document &object);

        bool deserializeFromFile(const acul::string &path);

        bool deserializeString(const char *s, rapidjson::Document &object)
        {
            if (!initDocument(s, object)) return false;
            return deserializeObject(object);
        }

        bool deserializeString(const char *s)
        {
            rapidjson::Document doc;
            return initDocument(s, doc);
        }

        virtual bool deserializeObject(const rapidjson::Value &obj) = 0;

    protected:
        static bool initDocument(const acul::string &s, rapidjson::Document &doc);
    };

    template <typename T>
    T getField(const rapidjson::Value &obj, const char *key, bool required = true);

    u32 getImageType(const rapidjson::Value &obj, const char *key, bool required);

    u16 getFormatField(const rapidjson::Value &obj, const char *key);
} // namespace models

#endif