#include <iostream>
#include <string>
#include <cstring>
#include <filesystem>
#include <getopt.h>
#include <unistd.h>

// Include converter headers
#include "converter_standard.h"
#include "converter_abr.h"
#include "converter_hls.h"
#include "watcher.h"

namespace fs = std::filesystem;

// Version information
const std::string VERSION = "1.0.0";
const std::string PROGRAM_NAME = "radiumvod";

// Command structure
enum Command {
    CMD_NONE,
    CMD_DAEMON,
    CMD_CONVERT,
    CMD_VERSION,
    CMD_HELP
};

enum ConvertFormat {
    FORMAT_H264,
    FORMAT_H265,
    FORMAT_HLS
};

enum ConvertProfile {
    PROFILE_HIGH,
    PROFILE_MEDIUM,
    PROFILE_LOW,
    PROFILE_ALL
};

struct Options {
    Command command = CMD_NONE;
    std::string config_file = "/etc/radiumvod/radiumvod.conf";
    std::string input_file;
    std::string output_file;
    ConvertFormat format = FORMAT_H264;
    ConvertProfile profile = PROFILE_HIGH;
    bool verbose = false;
};

void printVersion() {
    std::cout << PROGRAM_NAME << " version " << VERSION << "\n";
    std::cout << "Video On Demand Converter and Streaming Service\n";
    std::cout << "Created with assistance\n";
}

void printUsage() {
    std::cout << "Usage: " << PROGRAM_NAME << " [command] [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  daemon                      Run as daemon service\n";
    std::cout << "  convert                     Convert video file\n";
    std::cout << "  version                     Show version information\n";
    std::cout << "  help                        Show this help message\n\n";
    std::cout << "Daemon Options:\n";
    std::cout << "  -c, --config <file>         Config file (default: /etc/radiumvod/radiumvod.conf)\n\n";
    std::cout << "Convert Options:\n";
    std::cout << "  -i, --input <file>          Input video file (required)\n";
    std::cout << "  -o, --output <file>         Output file/directory (required)\n";
    std::cout << "  -f, --format <format>       Output format: h264, h265, hls (default: h264)\n";
    std::cout << "  -p, --profile <profile>     Quality profile: high, medium, low, all (default: high)\n";
    std::cout << "  -v, --verbose               Verbose output\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << PROGRAM_NAME << " daemon -c /etc/radiumvod/radiumvod.conf\n";
    std::cout << "  " << PROGRAM_NAME << " convert -i input.mp4 -o output.mp4 -f h264 -p high\n";
    std::cout << "  " << PROGRAM_NAME << " convert -i input.mp4 -o output_dir -f hls -p all\n";
    std::cout << "  " << PROGRAM_NAME << " convert -i input.mp4 -o output -f h264 -p all\n\n";
    std::cout << "System Service:\n";
    std::cout << "  sudo systemctl start radiumvod    # Start daemon\n";
    std::cout << "  sudo systemctl stop radiumvod     # Stop daemon\n";
    std::cout << "  sudo systemctl status radiumvod   # Check status\n";
    std::cout << "  sudo systemctl enable radiumvod   # Enable on boot\n";
}

ConvertFormat parseFormat(const std::string& format) {
    if (format == "h265") return FORMAT_H265;
    if (format == "hls") return FORMAT_HLS;
    return FORMAT_H264; // default
}

ConvertProfile parseProfile(const std::string& profile) {
    if (profile == "medium") return PROFILE_MEDIUM;
    if (profile == "low") return PROFILE_LOW;
    if (profile == "all") return PROFILE_ALL;
    return PROFILE_HIGH; // default
}

std::string profileToString(ConvertProfile profile) {
    switch (profile) {
        case PROFILE_MEDIUM: return "medium";
        case PROFILE_LOW: return "low";
        case PROFILE_ALL: return "all";
        default: return "high";
    }
}

Options parseOptions(int argc, char* argv[]) {
    Options opts;
    
    if (argc < 2) {
        return opts;
    }
    
    // Parse command
    std::string cmd = argv[1];
    if (cmd == "daemon") {
        opts.command = CMD_DAEMON;
    } else if (cmd == "convert") {
        opts.command = CMD_CONVERT;
    } else if (cmd == "version" || cmd == "--version" || cmd == "-v") {
        opts.command = CMD_VERSION;
        return opts;
    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        opts.command = CMD_HELP;
        return opts;
    } else if (cmd[0] == '-') {
        // If first arg starts with -, assume convert command
        opts.command = CMD_CONVERT;
        // Shift argv to include the flag
        argv++;
        argc++;
    } else {
        std::cerr << "Error: Unknown command '" << cmd << "'\n";
        std::cerr << "Use '" << PROGRAM_NAME << " help' for usage information\n";
        return opts;
    }
    
    // Setup option parsing
    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"format", required_argument, 0, 'f'},
        {"profile", required_argument, 0, 'p'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt_index = 0;
    int c;
    optind = 2; // Start after the command
    
    while ((c = getopt_long(argc, argv, "c:i:o:f:p:vh", long_options, &opt_index)) != -1) {
        switch (c) {
            case 'c':
                opts.config_file = optarg;
                break;
            case 'i':
                opts.input_file = optarg;
                break;
            case 'o':
                opts.output_file = optarg;
                break;
            case 'f':
                opts.format = parseFormat(optarg);
                break;
            case 'p':
                opts.profile = parseProfile(optarg);
                break;
            case 'v':
                opts.verbose = true;
                break;
            case 'h':
                opts.command = CMD_HELP;
                return opts;
            case '?':
                // Error already printed by getopt
                opts.command = CMD_NONE;
                return opts;
        }
    }
    
    return opts;
}

int runDaemon(const Options& opts) {
    std::cout << "Starting RadiumVOD daemon...\n";
    std::cout << "Config: " << opts.config_file << "\n";
    
    // Check if running as daemon (systemd or background)
    if (getppid() == 1) {
        // Running as daemon, suppress interactive output
        freopen("/dev/null", "r", stdin);
        freopen("/var/log/radiumvod.log", "a", stdout);
        freopen("/var/log/radiumvod.log", "a", stderr);
    }
    
    return run_watcher(opts.config_file);
}

int runConvert(const Options& opts) {
    // Validate required options
    if (opts.input_file.empty()) {
        std::cerr << "Error: Input file is required (-i)\n";
        return 1;
    }
    
    if (opts.output_file.empty()) {
        std::cerr << "Error: Output file is required (-o)\n";
        return 1;
    }
    
    // Check input file exists
    if (!fs::exists(opts.input_file)) {
        std::cerr << "Error: Input file does not exist: " << opts.input_file << "\n";
        return 1;
    }
    
    if (opts.verbose) {
        std::cout << "RadiumVOD Convert\n";
        std::cout << "================\n";
        std::cout << "Input:   " << opts.input_file << "\n";
        std::cout << "Output:  " << opts.output_file << "\n";
        std::cout << "Format:  " << (opts.format == FORMAT_HLS ? "HLS" : 
                                       opts.format == FORMAT_H265 ? "H.265" : "H.264") << "\n";
        std::cout << "Profile: " << profileToString(opts.profile) << "\n\n";
    }
    
    // Execute conversion based on format
    switch (opts.format) {
        case FORMAT_HLS:
            return convert_hls(opts.input_file, opts.output_file);
            
        case FORMAT_H265:
            std::cerr << "H.265 encoding not yet implemented\n";
            return 1;
            
        case FORMAT_H264:
        default:
            if (opts.profile == PROFILE_ALL || 
                opts.profile == PROFILE_HIGH || 
                opts.profile == PROFILE_MEDIUM || 
                opts.profile == PROFILE_LOW) {
                
                std::string profile_str = profileToString(opts.profile);
                return convert_abr(opts.input_file, opts.output_file, profile_str);
            }
            break;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    Options opts = parseOptions(argc, argv);
    
    switch (opts.command) {
        case CMD_VERSION:
            printVersion();
            return 0;
            
        case CMD_HELP:
            printUsage();
            return 0;
            
        case CMD_DAEMON:
            return runDaemon(opts);
            
        case CMD_CONVERT:
            return runConvert(opts);
            
        case CMD_NONE:
        default:
            printUsage();
            return 1;
    }
    
    return 0;
}