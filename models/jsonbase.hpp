#ifndef ASSETTOOL_MODELS_BASE
#define ASSETTOOL_MODELS_BASE

#include <filesystem>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

namespace models
{
    class JsonBase
    {
    public:
        bool deserializeFromFile(const std::filesystem::path &path, rapidjson::Document &object);

        bool deserializeFromFile(const std::filesystem::path &path);

        bool deserializeString(const std::string &s, rapidjson::Document &object)
        {
            if (!initDocument(s, object)) return false;
            return deserializeObject(object);
        }

        bool deserializeString(const std::string &s)
        {
            rapidjson::Document doc;
            return initDocument(s, doc);
        }

        virtual bool deserializeObject(const rapidjson::Value &obj) = 0;

    protected:
        static bool initDocument(const std::string &s, rapidjson::Document &doc);
    };

    template <typename T>
    T getField(const rapidjson::Value &obj, const char *key, bool required = true);

    uint32_t getImageType(const rapidjson::Value &obj, const char *key, bool required);
} // namespace models

#endif