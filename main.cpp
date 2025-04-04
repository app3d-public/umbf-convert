#include <acul/log.hpp>
#include <acul/meta.hpp>
#include <args/args.hxx>
#include <umbf/umbf.hpp>
#include "convert.hpp"
#include "extract.hpp"
#include "show.hpp"

enum class ArgsCommand
{
    none,
    show,
    extract,
    convert
};

enum class ConvertFormat
{
    raw,
    json,
    image,
    scene
};

struct Args
{
    ArgsCommand command = ArgsCommand::none;
    acul::string input, output;
    bool compressed = false;
    ConvertFormat format = ConvertFormat::raw;
};

void parseShowCommand(Args &args, args::Subparser &parser)
{
    args::HelpFlag help(parser, "help", "Show help", {'h', "help"});
    args::ValueFlag<std::string> input(parser, "path", "Input file", {'i', "input"}, args::Options::Required);
    parser.Parse();
    args.input = args::get(input).c_str();
}

void parseExtractCommand(Args &args, args::Subparser &parser)
{
    args::HelpFlag help(parser, "help", "Show help", {'h', "help"});
    args::ValueFlag<std::string> input(parser, "path", "Input file", {'i', "input"}, args::Options::Required);
    args::ValueFlag<std::string> output(parser, "path", "Output file", {'o', "output"}, args::Options::Required);
    parser.Parse();
    args.input = args::get(input).c_str();
    args.output = args::get(output).c_str();
}

struct __long
{
    char *__data;
    size_t __size;
    size_t padding;
};

void parseConvertCommand(Args &args, args::Subparser &parser)
{
    args::HelpFlag help(parser, "help", "Show help", {'h', "help"});
    args::ValueFlag<std::string> input(parser, "path", "Input file", {'i', "input"}, args::Options::Required);
    args::ValueFlag<std::string> format(parser, "raw|json|image|scene", "File format", {"format"},
                                        args::Options::Required);
    args::ValueFlag<std::string> output(parser, "path", "Output file", {'o', "output"}, args::Options::Required);
    args::Flag compressed(parser, "compressed", "Compressed", {"compressed"});
    parser.Parse();
    args.input = args::get(input).c_str();
    args.output = args::get(output).c_str();
    std::string values[] = {"raw", "json", "image", "scene"};
    auto it = std::find(std::begin(values), std::end(values), args::get(format));
    if (it == std::end(values)) throw args::ValidationError("Invalid format");
    args.format = static_cast<ConvertFormat>(std::distance(std::begin(values), it));
    args.compressed = args::get(compressed);
}

bool parseArgs(int argc, char **argv, Args &args)
{
    args::ArgumentParser parser("UMBF Tool");
    args::Group commands(parser, "Commands:", args::Group::Validators::Xor);
    args::Command show(commands, "show", "Show UMBF file info",
                       [&](args::Subparser &parser) { parseShowCommand(args, parser); });
    args::Command extract(commands, "extract", "Extract UMBF file",
                          [&](args::Subparser &parser) { parseExtractCommand(args, parser); });
    args::Command convert(commands, "convert", "Convert UMBF file",
                          [&](args::Subparser &parser) { parseConvertCommand(args, parser); });

    args::HelpFlag help(parser, "help", "Show help", {'h', "help"});
    args::Flag version(parser, "version", "Show version", {'v', "version"}, args::Options::KickOut);

    try
    {
        parser.ParseCLI(argc, argv);
    }
    catch (args::Help)
    {
        std::cout << parser;
        return true;
    }
    catch (args::ParseError e)
    {
        std::cerr << e.what() << std::endl;
        return false;
    }
    catch (args::ValidationError e)
    {
        std::cerr << "Invalid usage.\nUse --help for usage information.\n";
        return false;
    }

    if (version)
    {
        std::cout << "Version: 1.0.0" << std::endl;
        return true;
    }

    if (show)
        args.command = ArgsCommand::show;
    else if (extract)
        args.command = ArgsCommand::extract;
    else if (convert)
        args.command = ArgsCommand::convert;
    return true;
}

int main(int argc, char **argv)
{
    Args args;
    if (!parseArgs(argc, argv, args)) return 1;
    if (args.command == ArgsCommand::none) return 0;

    acul::task::service_dispatch sd;
    acul::log::log_service *log_service = acul::alloc<acul::log::log_service>();
    sd.register_service(log_service);
    auto *app_log = log_service->add_logger<acul::log::console_logger>("app");
#ifdef NDEBUG
    log_service->level = acul::log::level::info;
#else
    log_service->level = acul::log::level::trace;
#endif
    app_log->set_pattern("%(color_auto)%(message)%(color_off)\n");
    log_service->default_logger = app_log;
    acul::meta::hash_resolver meta_resolver;
    meta_resolver.streams = {{acul::meta::sign_block::raw_block, &acul::meta::streams::raw_block},
                             {umbf::sign_block::meta::image2D, &umbf::streams::image2D},
                             {umbf::sign_block::meta::image_atlas, &umbf::streams::image_atlas},
                             {umbf::sign_block::meta::material, &umbf::streams::material},
                             {umbf::sign_block::meta::material_info, &umbf::streams::material_info},
                             {umbf::sign_block::meta::scene, &umbf::streams::scene},
                             {umbf::sign_block::meta::mesh, &umbf::streams::mesh},
                             {umbf::sign_block::meta::target, &umbf::streams::target},
                             {umbf::sign_block::meta::library, &umbf::streams::library}};
    acul::meta::resolver = &meta_resolver;
    bool success = false;
    try
    {
        switch (args.command)
        {
            case ArgsCommand::show:
                success = show_file(args.input);
                break;
            case ArgsCommand::extract:
                success = extractFile(args.input, args.output);
                break;
            case ArgsCommand::convert:
            {
                u32 checksum = 0;
                switch (args.format)
                {
                    case ConvertFormat::raw:
                    {
                        umbf::File file;
                        if (convertRaw(args.input, args.compressed, file))
                            checksum = file.save(args.output) ? file.checksum : 0;
                        break;
                    }
                    case ConvertFormat::image:
                    {
                        umbf::File file;
                        if (convertImage(args.input, args.compressed, file)) checksum = file.save(args.output);
                        break;
                    }
                    case ConvertFormat::scene:
                        checksum = convertScene(args.input, args.output, args.compressed);
                        break;
                    case ConvertFormat::json:
                        checksum = convertJson(args.input, args.output, args.compressed);
                        break;
                    default:
                        break;
                }
                if (checksum != 0)
                {
                    logInfo("Success. Checksum: %u", checksum);
                    success = true;
                }
                else
                    logError("Failed to convert file to %s", args.output.c_str());
            }
            break;
            default:
                return 1;
        }
    }
    catch (const std::exception &e)
    {
        logError("%s", e.what());
        success = false;
    }

    log_service->await();
    return success ? 0 : 1;
}