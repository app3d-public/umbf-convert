#include <args/args.hxx>
#include <core/log.hpp>
#include <iostream>
#include "app/app.hpp"

#define RESULT_EXIT_SUCCESS 0
#define RESULT_EXIT_FAILURE 1
#define RESULT_CONTINUE     2

struct Args
{
    assettool::SaveMode mode;
    std::filesystem::path input;
    std::filesystem::path output;
    bool check;
};

std::pair<int, Args> parseArgs(int argc, char **argv)
{
    args::ArgumentParser parser("App3d Asset Tool");
    args::HelpFlag help(parser, "help", "Show help", {'h', "help"});
    args::Flag version(parser, "version", "Show version", {'v', "version"}, args::Options::KickOut);

    args::Group group(parser, "Source type:");
    args::Flag image(group, "image", "Image", {"image"});
    args::Flag scene(group, "scene", "Scene", {"scene"});
    args::Flag json(group, "json", "JSON Configuration", {"json"});
    args::Flag check(parser, "check", "Check asset file", {"check"});
    args::ValueFlag<std::string> input(parser, "input", "Input file", {'i', "input"}, args::Options::Required);
    args::ValueFlag<std::string> output(parser, "output", "Output file", {'o', "output"});

    try
    {
        parser.ParseCLI(argc, argv);
    }
    catch (args::Help)
    {
        std::cout << parser;
        return {RESULT_EXIT_SUCCESS, {}};
    }
    catch (args::ParseError e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return {RESULT_EXIT_FAILURE, {}};
    }
    catch (args::ValidationError e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return {RESULT_EXIT_FAILURE, {}};
    }

    if (version)
    {
        std::cout << "Version: 1.0.0" << std::endl;
        return {RESULT_EXIT_SUCCESS, {}};
    }

    if (!input)
    {
        std::cerr << "Error: --input is required" << std::endl;
        std::cerr << parser;
        return {RESULT_EXIT_FAILURE, {}};
    }
    bool isModeSelected = image || scene || json;
    Args ret;
    ret.mode = assettool::SaveMode::None;
    ret.input = args::get(input);
    ret.output = output ? args::get(output) : std::filesystem::path(ret.input.stem().string() + ".a3d");
    ret.check = check;
    if (!isModeSelected && !check)
    {
        std::cerr << "Error: Mode is required if not checking existing file." << std::endl;
        std::cerr << parser;
        return {RESULT_EXIT_FAILURE, {}};
    }
    else
    {
        if (image)
            ret.mode = assettool::SaveMode::Image;
        else if (scene)
            ret.mode = assettool::SaveMode::Scene;
        else if (json)
            ret.mode = assettool::SaveMode::Json;
    }

    return {RESULT_CONTINUE, ret};
}

int main(int argc, char **argv)
{
    auto [result, args] = parseArgs(argc, argv);
    if (result != RESULT_CONTINUE) return result;

    task::ServiceDispatch sd;
    logging::g_LogService = astl::alloc<logging::LogService>();
    sd.registerService(logging::g_LogService);
    logging::g_DefaultLogger = logging::g_LogService->addLogger<logging::ConsoleLogger>("app");
#ifdef NDEBUG
    logging::g_LogService->level(logging::Level::Info);
#else
    logging::g_LogService->level(logging::Level::Trace);
#endif
    logging::g_DefaultLogger->setPattern("%(color_auto)%(level_name)\t%(message)%(color_off)\n");

    // Assets meta
    meta::initStreams({{meta::sign_block::external_block, &meta::streams::external_block},
                       {assets::sign_block::image2D, &assets::streams::image2D},
                       {assets::sign_block::image_atlas, &assets::streams::image_atlas},
                       {assets::sign_block::scene, &assets::streams::scene},
                       {assets::sign_block::mesh, &assets::streams::mesh},
                       {assets::sign_block::material, &assets::streams::material},
                       {assets::sign_block::material_info, &assets::streams::material_info},
                       {assets::sign_block::target, &assets::streams::target},
                       {assets::sign_block::library, &assets::streams::library}});

    assettool::App app(args.input, args.output, args.mode, args.check);
    app.run();

    meta::clearStreams();
    logging::g_LogService->await();
    return app.statusCode();
}
