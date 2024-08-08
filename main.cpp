#include <args/args.hxx>
#include <core/log.hpp>
#include <core/task.hpp>
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

    args::Group group(parser, "Source type:", args::Group::Validators::Xor);
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

    logging::LogManager::init();
    auto logger = logging::mng->addLogger<logging::ConsoleLogger>("backend");
#ifdef NDEBUG
    logging::mng->level(logging::Level::Info);
#else
    logging::mng->level(logging::Level::Trace);
#endif
    logger->setPattern("%(color_auto)%(level_name)\t%(message)%(color_off)\n");
    logging::mng->defaultLogger(logger);

    // Assets meta
    assets::meta::addStream(assets::meta::sign_block_external, new assets::meta::ExternalStream());
    assets::meta::addStream(assets::meta::sign_block_scene, new assets::meta::SceneInfoStream());
    assets::meta::addStream(assets::meta::sign_block_mesh, new assets::meta::mesh::MeshStream());

    assettool::App app(args.input, args.output, args.mode, args.check);
    app.run();

    assets::meta::clearStreams();
    TaskManager::global().await(true);
    logging::mng->await();
    logging::LogManager::destroy();
    return app.statusCode();
}
