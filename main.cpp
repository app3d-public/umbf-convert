#include <acul/log.hpp>
#include <acul/meta.hpp>
#include <args/args.hxx>
#include <umbf/umbf.hpp>
#include "convert.hpp"
#include "extract.hpp"
#include "show.hpp"

enum class ArgsCommand
{
    None,
    Show,
    Extract,
    Convert
};

enum class ConvertFormat
{
    Raw,
    Json,
    Image,
    Scene
};

struct Args
{
    ArgsCommand command = ArgsCommand::None;
    acul::string input, output;
    bool compressed = false;
    ConvertFormat format = ConvertFormat::Raw;
};

void parse_show_command(Args &args, args::Subparser &parser)
{
    args::HelpFlag help(parser, "help", "Show help", {'h', "help"});
    args::ValueFlag<std::string> input(parser, "path", "Input file", {'i', "input"}, args::Options::Required);
    parser.Parse();
    args.input = args::get(input).c_str();
}

void parse_extract_command(Args &args, args::Subparser &parser)
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

void parse_convert_command(Args &args, args::Subparser &parser)
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

bool parse_args(int argc, char **argv, Args &args)
{
    args::ArgumentParser parser("UMBF Tool");
    args::Group commands(parser, "Commands:", args::Group::Validators::Xor);
    args::Command show(commands, "show", "Show UMBF file info",
                       [&](args::Subparser &parser) { parse_show_command(args, parser); });
    args::Command extract(commands, "extract", "Extract UMBF file",
                          [&](args::Subparser &parser) { parse_extract_command(args, parser); });
    args::Command convert(commands, "convert", "Convert UMBF file",
                          [&](args::Subparser &parser) { parse_convert_command(args, parser); });

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
        args.command = ArgsCommand::Show;
    else if (extract)
        args.command = ArgsCommand::Extract;
    else if (convert)
        args.command = ArgsCommand::Convert;
    return true;
}

int main(int argc, char **argv)
{
    Args args;
    if (!parse_args(argc, argv, args)) return 1;
    if (args.command == ArgsCommand::None) return 0;

    acul::task::service_dispatch sd;
    acul::log::log_service *log_service = acul::alloc<acul::log::log_service>();
    sd.register_service(log_service);
    auto *app_log = log_service->add_logger<acul::log::console_logger>("app");
#ifdef NDEBUG
    log_service->level = acul::log::level::Info;
#else
    log_service->level = acul::log::level::Trace;
#endif
    app_log->set_pattern("%(message)\n");
    log_service->default_logger = app_log;
    acul::meta::hash_resolver meta_resolver;
    meta_resolver.streams = {{acul::meta::sign_block::Raw, &acul::meta::streams::raw_block},
                             {umbf::sign_block::meta::Image2D, &umbf::streams::image2D},
                             {umbf::sign_block::meta::ImageAtlas, &umbf::streams::image_atlas},
                             {umbf::sign_block::meta::Material, &umbf::streams::material},
                             {umbf::sign_block::meta::MaterialInfo, &umbf::streams::material_info},
                             {umbf::sign_block::meta::Scene, &umbf::streams::scene},
                             {umbf::sign_block::meta::Mesh, &umbf::streams::mesh},
                             {umbf::sign_block::meta::Target, &umbf::streams::target},
                             {umbf::sign_block::meta::Library, &umbf::streams::library}};
    acul::meta::resolver = &meta_resolver;
    bool success = false;
    try
    {
        switch (args.command)
        {
            case ArgsCommand::Show:
                success = show_file(args.input);
                break;
            case ArgsCommand::Extract:
                success = extract_file(args.input, args.output);
                break;
            case ArgsCommand::Convert:
            {
                u32 checksum = 0;
                switch (args.format)
                {
                    case ConvertFormat::Raw:
                    {
                        umbf::File file;
                        if (convert_raw(args.input, args.compressed, file))
                            checksum = file.save(args.output) ? file.checksum : 0;
                        break;
                    }
                    case ConvertFormat::Image:
                    {
                        umbf::File file;
                        if (convert_image(args.input, args.compressed, file)) checksum = file.save(args.output);
                        break;
                    }
                    case ConvertFormat::Scene:
                        checksum = convert_scene(args.input, args.output, args.compressed);
                        break;
                    case ConvertFormat::Json:
                        checksum = convert_json(args.input, args.output, args.compressed);
                        break;
                    default:
                        break;
                }
                if (checksum != 0)
                {
                    LOG_INFO("Success. Checksum: %u", checksum);
                    success = true;
                }
                else
                    LOG_ERROR("Failed to convert file to %s", args.output.c_str());
            }
            break;
            default:
                return 1;
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("%s", e.what());
        success = false;
    }

    log_service->await();
    return success ? 0 : 1;
}