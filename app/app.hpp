#pragma once

#include <filesystem>
#include "../convert.hpp"

namespace assettool
{
    enum class SaveMode
    {
        None,
        Image,
        Scene,
        Json
    };

    class App
    {
    public:
        App(const std::filesystem::path &input, const std::filesystem::path &output, SaveMode mode, bool check)
            : _statusCode(EXIT_SUCCESS), _input(input), _output(output), _mode(mode), _check(check)
        {
        }

        int statusCode() const { return _statusCode; }

        void run();

    private:
        int _statusCode;
        std::filesystem::path _input;
        std::filesystem::path _output;
        SaveMode _mode;
        bool _check;
        astl::vector<ImageResource> _images;

        static bool checkAsset(const std::filesystem::path &path);
        astl::shared_ptr<assets::Asset> getAssetByImage();
        astl::shared_ptr<assets::Asset> getAssetByScene();
        astl::shared_ptr<assets::Asset> getAssetByJson();
    };
} // namespace assettool