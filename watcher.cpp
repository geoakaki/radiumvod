#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <signal.h>

// Simple JSON parser for config file
#include <regex>

namespace fs = std::filesystem;

// Global flag for graceful shutdown
volatile bool g_running = true;

void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ". Shutting down gracefully...\n";
    g_running = false;
}

class Config {
public:
    // Watcher settings
    std::string source_dir;
    std::string dest_dir;
    int watch_interval = 5;
    std::vector<std::string> file_extensions;
    bool delete_source = false;
    bool create_subdirs = true;
    std::string log_file;
    
    // HLS settings
    int segment_duration = 10;
    
    struct Profile {
        std::string name;
        int width;
        int height;
        int video_bitrate;
        int audio_bitrate;
        int bandwidth;
        std::string folder_name;
    };
    std::vector<Profile> profiles;
    
    // FFmpeg settings
    std::string preset = "fast";
    std::string h264_profile = "high";
    std::string h264_level = "4.1";
    int threads = 0;
    std::string log_level = "warning";
    
    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Cannot open config file: " << filename << "\n";
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        // Parse watcher settings
        source_dir = extractString(content, "\"source_directory\"\\s*:\\s*\"([^\"]+)\"");
        dest_dir = extractString(content, "\"destination_directory\"\\s*:\\s*\"([^\"]+)\"");
        watch_interval = extractInt(content, "\"watch_interval_seconds\"\\s*:\\s*(\\d+)");
        delete_source = extractBool(content, "\"delete_source_after_conversion\"\\s*:\\s*(true|false)");
        create_subdirs = extractBool(content, "\"create_subdirectories\"\\s*:\\s*(true|false)");
        log_file = extractString(content, "\"log_file\"\\s*:\\s*\"([^\"]+)\"");
        
        // Parse file extensions
        std::regex ext_regex("\"file_extensions\"\\s*:\\s*\\[([^\\]]+)\\]");
        std::smatch ext_match;
        if (std::regex_search(content, ext_match, ext_regex)) {
            std::string ext_str = ext_match[1];
            std::regex item_regex("\"([^\"]+)\"");
            std::sregex_iterator it(ext_str.begin(), ext_str.end(), item_regex);
            std::sregex_iterator end;
            for (; it != end; ++it) {
                file_extensions.push_back((*it)[1]);
            }
        }
        
        // Parse HLS settings
        segment_duration = extractInt(content, "\"segment_duration\"\\s*:\\s*(\\d+)");
        
        // Parse profiles
        std::regex profiles_regex("\"profiles\"\\s*:\\s*\\[([^\\]]+(?:\\[[^\\]]*\\][^\\]]*)*?)\\]");
        std::smatch profiles_match;
        if (std::regex_search(content, profiles_match, profiles_regex)) {
            std::string profiles_str = profiles_match[1];
            
            // Parse each profile object
            std::regex profile_regex("\\{([^}]+)\\}");
            std::sregex_iterator it(profiles_str.begin(), profiles_str.end(), profile_regex);
            std::sregex_iterator end;
            
            for (; it != end; ++it) {
                Profile p;
                std::string profile_content = (*it)[1];
                p.name = extractString(profile_content, "\"name\"\\s*:\\s*\"([^\"]+)\"");
                p.width = extractInt(profile_content, "\"width\"\\s*:\\s*(\\d+)");
                p.height = extractInt(profile_content, "\"height\"\\s*:\\s*(\\d+)");
                p.video_bitrate = extractInt(profile_content, "\"video_bitrate\"\\s*:\\s*(\\d+)");
                p.audio_bitrate = extractInt(profile_content, "\"audio_bitrate\"\\s*:\\s*(\\d+)");
                p.bandwidth = extractInt(profile_content, "\"bandwidth\"\\s*:\\s*(\\d+)");
                p.folder_name = extractString(profile_content, "\"folder_name\"\\s*:\\s*\"([^\"]+)\"");
                
                if (!p.name.empty() && p.width > 0) {
                    profiles.push_back(p);
                }
            }
        }
        
        // Parse FFmpeg settings
        preset = extractString(content, "\"preset\"\\s*:\\s*\"([^\"]+)\"");
        h264_profile = extractString(content, "\"h264_profile\"\\s*:\\s*\"([^\"]+)\"");
        h264_level = extractString(content, "\"h264_level\"\\s*:\\s*\"([^\"]+)\"");
        threads = extractInt(content, "\"threads\"\\s*:\\s*(\\d+)");
        log_level = extractString(content, "\"log_level\"\\s*:\\s*\"([^\"]+)\"");
        
        return validate();
    }
    
    bool validate() {
        if (source_dir.empty() || dest_dir.empty()) {
            std::cerr << "Source and destination directories must be specified\n";
            return false;
        }
        
        if (profiles.empty()) {
            std::cerr << "No HLS profiles defined\n";
            return false;
        }
        
        if (file_extensions.empty()) {
            // Default extensions
            file_extensions = {".mp4", ".avi", ".mkv", ".mov", ".webm"};
        }
        
        return true;
    }
    
private:
    std::string extractString(const std::string& content, const std::string& pattern) {
        std::regex regex(pattern);
        std::smatch match;
        if (std::regex_search(content, match, regex)) {
            return match[1];
        }
        return "";
    }
    
    int extractInt(const std::string& content, const std::string& pattern) {
        std::string str = extractString(content, pattern);
        if (!str.empty()) {
            try {
                return std::stoi(str);
            } catch (...) {}
        }
        return 0;
    }
    
    bool extractBool(const std::string& content, const std::string& pattern) {
        std::string str = extractString(content, pattern);
        return str == "true";
    }
};

class Logger {
private:
    std::string log_file;
    bool use_file;
    std::ofstream file_stream;
    
public:
    Logger(const std::string& filename = "") : log_file(filename), use_file(!filename.empty()) {
        if (use_file && !log_file.empty()) {
            file_stream.open(log_file, std::ios::app);
        }
    }
    
    ~Logger() {
        if (file_stream.is_open()) {
            file_stream.close();
        }
    }
    
    void log(const std::string& level, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] ";
        ss << "[" << level << "] " << message << "\n";
        
        std::cout << ss.str();
        
        if (use_file && file_stream.is_open()) {
            file_stream << ss.str();
            file_stream.flush();
        }
    }
    
    void info(const std::string& msg) { log("INFO", msg); }
    void error(const std::string& msg) { log("ERROR", msg); }
    void warning(const std::string& msg) { log("WARN", msg); }
};

class HLSWatcher {
private:
    Config config;
    Logger logger;
    std::set<std::string> processed_files;
    std::set<std::string> processing_files;
    std::map<std::string, std::filesystem::file_time_type> file_times;
    
public:
    HLSWatcher(const Config& cfg) : config(cfg), logger(cfg.log_file) {}
    
    bool start() {
        logger.info("HLS Watcher started");
        logger.info("Source directory: " + config.source_dir);
        logger.info("Destination directory: " + config.dest_dir);
        logger.info("Watch interval: " + std::to_string(config.watch_interval) + " seconds");
        
        // Create directories if they don't exist
        if (!fs::exists(config.source_dir)) {
            fs::create_directories(config.source_dir);
            logger.info("Created source directory: " + config.source_dir);
        }
        
        if (!fs::exists(config.dest_dir)) {
            fs::create_directories(config.dest_dir);
            logger.info("Created destination directory: " + config.dest_dir);
        }
        
        // Load existing processed files to avoid reprocessing
        loadProcessedFiles();
        
        // Main watch loop
        while (g_running) {
            scanAndProcess();
            
            // Wait for next scan
            for (int i = 0; i < config.watch_interval && g_running; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        logger.info("HLS Watcher stopped");
        return true;
    }
    
private:
    void loadProcessedFiles() {
        // Check destination directory for already processed files
        if (!fs::exists(config.dest_dir)) {
            return;
        }
        
        for (const auto& entry : fs::recursive_directory_iterator(config.dest_dir)) {
            if (entry.is_regular_file() && entry.path().filename() == "playlist.m3u8") {
                // Extract original filename from parent directory name
                std::string parent_dir = entry.path().parent_path().filename().string();
                processed_files.insert(parent_dir);
            }
        }
        
        logger.info("Loaded " + std::to_string(processed_files.size()) + " previously processed files");
    }
    
    void scanAndProcess() {
        if (!fs::exists(config.source_dir)) {
            logger.warning("Source directory does not exist: " + config.source_dir);
            return;
        }
        
        for (const auto& entry : fs::directory_iterator(config.source_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            
            std::string filepath = entry.path().string();
            std::string filename = entry.path().filename().string();
            std::string extension = entry.path().extension().string();
            
            // Check if file has valid extension
            if (!isValidExtension(extension)) {
                continue;
            }
            
            // Check if file is stable (not being written)
            if (!isFileStable(entry)) {
                continue;
            }
            
            // Check if already processed or processing
            if (processed_files.count(filename) > 0 || processing_files.count(filename) > 0) {
                continue;
            }
            
            // Process the file
            logger.info("New file detected: " + filename);
            processFile(filepath, filename);
        }
    }
    
    bool isValidExtension(const std::string& ext) {
        std::string lower_ext = ext;
        std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), ::tolower);
        
        for (const auto& valid_ext : config.file_extensions) {
            if (lower_ext == valid_ext) {
                return true;
            }
        }
        return false;
    }
    
    bool isFileStable(const fs::directory_entry& entry) {
        auto current_time = entry.last_write_time();
        auto filepath = entry.path().string();
        
        if (file_times.count(filepath) > 0) {
            // Check if file modification time hasn't changed
            if (file_times[filepath] == current_time) {
                return true;  // File is stable
            }
        }
        
        // Update the time and wait for next scan
        file_times[filepath] = current_time;
        return false;
    }
    
    void processFile(const std::string& filepath, const std::string& filename) {
        processing_files.insert(filename);
        logger.info("Starting conversion: " + filename);
        
        // Generate output directory name
        std::string base_name = fs::path(filename).stem().string();
        std::string output_dir = config.dest_dir + "/" + base_name;
        
        // Convert to HLS
        if (convertToHLS(filepath, output_dir)) {
            logger.info("Successfully converted: " + filename);
            processed_files.insert(filename);
            
            // Delete source file if configured
            if (config.delete_source) {
                try {
                    fs::remove(filepath);
                    logger.info("Deleted source file: " + filename);
                } catch (const std::exception& e) {
                    logger.error("Failed to delete source file: " + std::string(e.what()));
                }
            }
        } else {
            logger.error("Failed to convert: " + filename);
        }
        
        processing_files.erase(filename);
    }
    
    bool convertToHLS(const std::string& input_file, const std::string& output_dir) {
        // Create output directory
        try {
            fs::create_directories(output_dir);
        } catch (const std::exception& e) {
            logger.error("Failed to create output directory: " + std::string(e.what()));
            return false;
        }
        
        // Process each profile
        bool all_success = true;
        
        for (const auto& profile : config.profiles) {
            std::string profile_dir = output_dir + "/" + profile.folder_name;
            
            if (!processProfile(input_file, profile_dir, profile)) {
                logger.error("Failed to process profile: " + profile.name);
                all_success = false;
            } else {
                logger.info("Processed profile: " + profile.name + " for " + 
                          fs::path(input_file).filename().string());
            }
        }
        
        // Generate master playlist
        if (all_success) {
            if (!generateMasterPlaylist(output_dir)) {
                logger.error("Failed to generate master playlist");
                return false;
            }
        }
        
        return all_success;
    }
    
    bool processProfile(const std::string& input_file, 
                       const std::string& profile_dir, 
                       const Config::Profile& profile) {
        
        // Create profile directory
        try {
            fs::create_directories(profile_dir);
        } catch (const std::exception& e) {
            return false;
        }
        
        // Build FFmpeg command
        std::stringstream cmd;
        cmd << "ffmpeg -i \"" << input_file << "\" ";
        
        // Video encoding settings
        cmd << "-c:v libx264 ";
        cmd << "-b:v " << profile.video_bitrate << " ";
        cmd << "-maxrate " << profile.video_bitrate << " ";
        cmd << "-bufsize " << (profile.video_bitrate * 2) << " ";
        cmd << "-vf scale=" << profile.width << ":" << profile.height << " ";
        cmd << "-preset " << config.preset << " ";
        cmd << "-profile:v " << config.h264_profile << " ";
        cmd << "-level " << config.h264_level << " ";
        
        // Force keyframe interval
        cmd << "-g " << (30 * config.segment_duration) << " ";
        cmd << "-keyint_min " << (30 * config.segment_duration) << " ";
        cmd << "-sc_threshold 0 ";
        
        // Audio encoding
        cmd << "-c:a aac ";
        cmd << "-b:a " << profile.audio_bitrate << " ";
        cmd << "-ar 44100 ";
        cmd << "-ac 2 ";
        
        // HLS settings
        cmd << "-f hls ";
        cmd << "-hls_time " << config.segment_duration << " ";
        cmd << "-hls_list_size 0 ";
        cmd << "-hls_segment_filename \"" << profile_dir << "/segment_%03d.ts\" ";
        cmd << "-hls_flags independent_segments ";
        
        // Output
        cmd << "\"" << profile_dir << "/index.m3u8\" ";
        
        // FFmpeg options
        cmd << "-y -hide_banner -loglevel " << config.log_level;
        
        if (config.threads > 0) {
            cmd << " -threads " << config.threads;
        }
        
        cmd << " 2>&1";
        
        // Execute command
        int result = system(cmd.str().c_str());
        
        return (result == 0) && fs::exists(profile_dir + "/index.m3u8");
    }
    
    bool generateMasterPlaylist(const std::string& output_dir) {
        std::string playlist_path = output_dir + "/playlist.m3u8";
        std::ofstream playlist(playlist_path);
        
        if (!playlist.is_open()) {
            return false;
        }
        
        // Write header
        playlist << "#EXTM3U\n";
        playlist << "#EXT-X-VERSION:3\n\n";
        
        // Write stream info for each profile
        for (const auto& profile : config.profiles) {
            playlist << "#EXT-X-STREAM-INF:BANDWIDTH=" << profile.bandwidth;
            playlist << ",RESOLUTION=" << profile.width << "x" << profile.height;
            playlist << "\n";
            playlist << profile.folder_name << "/index.m3u8\n\n";
        }
        
        playlist.close();
        return true;
    }
};

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [config_file]\n";
    std::cout << "\nHLS Watcher - Automatic HLS converter with directory monitoring\n";
    std::cout << "\nMonitors a source directory for new video files and automatically\n";
    std::cout << "converts them to HLS format in the destination directory.\n";
    std::cout << "\nOptions:\n";
    std::cout << "  config_file    Path to configuration file (default: config.json)\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << program << " /etc/hls_watcher/config.json\n";
    std::cout << "\nSignals:\n";
    std::cout << "  SIGINT/SIGTERM - Graceful shutdown\n";
}

int main(int argc, char* argv[]) {
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::string config_file = "config.json";
    
    if (argc > 1) {
        if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        config_file = argv[1];
    }
    
    // Check if config file exists
    if (!fs::exists(config_file)) {
        std::cerr << "Configuration file not found: " << config_file << "\n";
        std::cerr << "Please create a config file or specify the path to an existing one.\n";
        printUsage(argv[0]);
        return 1;
    }
    
    // Load configuration
    Config config;
    if (!config.loadFromFile(config_file)) {
        std::cerr << "Failed to load configuration from: " << config_file << "\n";
        return 1;
    }
    
    std::cout << "========================================\n";
    std::cout << "HLS Watcher Service\n";
    std::cout << "========================================\n";
    std::cout << "Config: " << config_file << "\n";
    std::cout << "Source: " << config.source_dir << "\n";
    std::cout << "Destination: " << config.dest_dir << "\n";
    std::cout << "Monitoring " << config.file_extensions.size() << " file types\n";
    std::cout << "Press Ctrl+C to stop\n";
    std::cout << "========================================\n\n";
    
    // Start watcher
    HLSWatcher watcher(config);
    
    if (watcher.start()) {
        std::cout << "\nWatcher stopped successfully\n";
        return 0;
    } else {
        std::cerr << "\nWatcher encountered an error\n";
        return 1;
    }
}