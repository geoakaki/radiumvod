#include "watcher.h"
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
#include <numeric>
#include <signal.h>
#include <regex>
#include <random>

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
    
    // SFTP settings
    bool sftp_enabled = false;
    std::string sftp_host;
    int sftp_port = 22;
    std::string sftp_username;
    std::string sftp_password;
    std::string sftp_remote_path;
    bool sftp_delete_source_after_upload = false;
    bool sftp_delete_local_after_upload = false;
    int sftp_retry_attempts = 3;
    int sftp_retry_delay = 5;
    
    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open config file: " << filename << "\n";
            return false;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        
        // Parse watcher settings
        source_dir = parseString(content, "source_directory");
        dest_dir = parseString(content, "destination_directory");
        watch_interval = parseInt(content, "watch_interval_seconds", 5);
        delete_source = parseBool(content, "delete_source_after_conversion");
        create_subdirs = parseBool(content, "create_subdirectories", true);
        log_file = parseString(content, "log_file");
        
        // Parse file extensions
        size_t ext_start = content.find("\"file_extensions\"");
        if (ext_start != std::string::npos) {
            size_t array_start = content.find("[", ext_start);
            size_t array_end = content.find("]", array_start);
            if (array_start != std::string::npos && array_end != std::string::npos) {
                std::string extensions = content.substr(array_start + 1, array_end - array_start - 1);
                std::regex ext_regex("\"([^\"]+)\"");
                std::sregex_iterator it(extensions.begin(), extensions.end(), ext_regex);
                std::sregex_iterator end;
                for (; it != end; ++it) {
                    file_extensions.push_back(it->str(1));
                }
            }
        }
        
        // Parse HLS settings
        segment_duration = parseInt(content, "segment_duration", 10);
        
        // Parse profiles
        parseProfiles(content);
        
        // Parse FFmpeg settings
        preset = parseString(content, "preset", "fast");
        h264_profile = parseString(content, "h264_profile", "high");
        h264_level = parseString(content, "h264_level", "4.1");
        threads = parseInt(content, "threads", 0);
        log_level = parseString(content, "log_level", "warning");
        
        // Parse SFTP settings
        sftp_enabled = parseBool(content, "enabled", false);
        sftp_host = parseString(content, "host");
        sftp_port = parseInt(content, "port", 22);
        sftp_username = parseString(content, "username");
        sftp_password = parseString(content, "password");
        sftp_remote_path = parseString(content, "remote_path");
        sftp_delete_source_after_upload = parseBool(content, "delete_source_after_upload", false);
        sftp_delete_local_after_upload = parseBool(content, "delete_local_after_upload", false);
        sftp_retry_attempts = parseInt(content, "retry_attempts", 3);
        sftp_retry_delay = parseInt(content, "retry_delay_seconds", 5);
        
        return true;
    }
    
private:
    std::string parseString(const std::string& json, const std::string& key, const std::string& default_val = "") {
        std::regex regex("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch match;
        if (std::regex_search(json, match, regex) && match.size() > 1) {
            return match[1];
        }
        return default_val;
    }
    
    int parseInt(const std::string& json, const std::string& key, int default_val = 0) {
        std::regex regex("\"" + key + "\"\\s*:\\s*(\\d+)");
        std::smatch match;
        if (std::regex_search(json, match, regex) && match.size() > 1) {
            return std::stoi(match[1]);
        }
        return default_val;
    }
    
    bool parseBool(const std::string& json, const std::string& key, bool default_val = false) {
        std::regex regex("\"" + key + "\"\\s*:\\s*(true|false)");
        std::smatch match;
        if (std::regex_search(json, match, regex) && match.size() > 1) {
            return match[1] == "true";
        }
        return default_val;
    }
    
    void parseProfiles(const std::string& json) {
        // Default profiles
        profiles = {
            {"720p", 1280, 720, 3200000, 128000, 3500000, "stream_3500"},
            {"432p", 768, 432, 1300000, 96000, 1500000, "stream_1500"},
            {"288p", 512, 288, 400000, 64000, 500000, "stream_500"}
        };
        
        // Try to parse custom profiles from JSON
        size_t profiles_start = json.find("\"profiles\"");
        if (profiles_start != std::string::npos) {
            size_t array_start = json.find("[", profiles_start);
            size_t array_end = json.rfind("]");
            
            if (array_start != std::string::npos && array_end != std::string::npos) {
                profiles.clear();
                std::string profiles_json = json.substr(array_start, array_end - array_start + 1);
                
                std::regex profile_regex(R"(\{[^}]+\})");
                std::sregex_iterator it(profiles_json.begin(), profiles_json.end(), profile_regex);
                std::sregex_iterator end;
                
                for (; it != end; ++it) {
                    std::string profile_str = it->str();
                    Profile p;
                    p.name = parseString(profile_str, "name");
                    p.width = parseInt(profile_str, "width");
                    p.height = parseInt(profile_str, "height");
                    p.video_bitrate = parseInt(profile_str, "video_bitrate");
                    p.audio_bitrate = parseInt(profile_str, "audio_bitrate");
                    p.bandwidth = parseInt(profile_str, "bandwidth");
                    p.folder_name = parseString(profile_str, "folder_name");
                    
                    if (!p.name.empty() && p.width > 0 && p.height > 0) {
                        profiles.push_back(p);
                    }
                }
            }
        }
    }
};

class HLSWatcherSFTP {
private:
    Config config;
    std::set<std::string> processed_files;
    std::ofstream log_stream;
    
    void log(const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] " << message;
        
        std::cout << ss.str() << std::endl;
        
        if (log_stream.is_open()) {
            log_stream << ss.str() << std::endl;
            log_stream.flush();
        }
    }
    
    bool isFileStable(const fs::path& path) {
        try {
            auto size1 = fs::file_size(path);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            auto size2 = fs::file_size(path);
            return size1 == size2;
        } catch (...) {
            return false;
        }
    }
    
    bool hasValidExtension(const fs::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        for (const auto& valid_ext : config.file_extensions) {
            if (ext == valid_ext) {
                return true;
            }
        }
        return false;
    }
    
    std::string generateUniqueID(const std::string& prefix, int length = 16) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 9);
        
        std::stringstream id;
        id << prefix;
        for (int i = prefix.length(); i < length; i++) {
            id << dis(gen);
        }
        return id.str();
    }
    
    bool generatePosters(const fs::path& input_file, const fs::path& output_dir, const std::string& basename) {
        log("Generating posters from video: " + input_file.string());
        
        // Generate poster at 10% and 30% of video duration
        std::string poster1 = (output_dir / (basename + "-poster1.jpg")).string();
        std::string poster2 = (output_dir / (basename + "-poster2.jpg")).string();
        
        // Get video duration
        std::stringstream duration_cmd;
        duration_cmd << "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" 
                     << input_file.string() << "\" 2>/dev/null";
        
        FILE* pipe = popen(duration_cmd.str().c_str(), "r");
        if (!pipe) {
            log("ERROR: Failed to get video duration");
            return false;
        }
        
        char buffer[128];
        std::string duration_str;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            duration_str += buffer;
        }
        pclose(pipe);
        
        float duration = 10.0f; // default
        try {
            duration = std::stof(duration_str);
        } catch (...) {
            log("WARNING: Could not parse video duration, using default positions");
        }
        
        // Generate poster 1 at 10% of video
        float pos1 = duration * 0.1f;
        std::stringstream cmd1;
        cmd1 << "ffmpeg -ss " << pos1 << " -i \"" << input_file.string() << "\" ";
        cmd1 << "-vf \"scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2\" ";
        cmd1 << "-vframes 1 -q:v 2 -loglevel error \"" << poster1 << "\"";
        
        if (system(cmd1.str().c_str()) != 0) {
            log("ERROR: Failed to generate poster1");
            return false;
        }
        
        // Generate poster 2 at 30% of video
        float pos2 = duration * 0.3f;
        std::stringstream cmd2;
        cmd2 << "ffmpeg -ss " << pos2 << " -i \"" << input_file.string() << "\" ";
        cmd2 << "-vf \"scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2\" ";
        cmd2 << "-vframes 1 -q:v 2 -loglevel error \"" << poster2 << "\"";
        
        if (system(cmd2.str().c_str()) != 0) {
            log("ERROR: Failed to generate poster2");
            return false;
        }
        
        log("Posters generated successfully");
        return true;
    }
    
    bool generateVODXML(const fs::path& output_dir, const std::string& basename, const std::string& title = "") {
        log("Generating VOD XML metadata: " + basename);
        
        // Generate unique IDs
        std::string prod_id = generateUniqueID("PROD", 19);
        std::string asset_id = generateUniqueID("ASST", 19);
        std::string poster_id = generateUniqueID("ASST", 19);
        
        // Get current date
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream date_stream;
        date_stream << std::put_time(std::localtime(&time_t), "%Y-%m-%d");
        std::string current_date = date_stream.str();
        
        // Calculate end date (5 years from now)
        auto end_time = now + std::chrono::hours(24 * 365 * 5);
        auto end_time_t = std::chrono::system_clock::to_time_t(end_time);
        std::stringstream end_date_stream;
        end_date_stream << std::put_time(std::localtime(&end_time_t), "%Y-%m-%dT23:59:59");
        std::string end_date = end_date_stream.str();
        
        // Use basename as title if not provided
        std::string video_title = title.empty() ? basename : title;
        
        // Create XML file
        fs::path xml_path = output_dir / ("vod-" + basename + ".xml");
        std::ofstream xml(xml_path);
        
        if (!xml.is_open()) {
            log("ERROR: Cannot create VOD XML file");
            return false;
        }
        
        xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        xml << "<ADI>\n";
        xml << "  <Metadata>\n";
        xml << "    <AMS Asset_Class=\"package\" Asset_ID=\"" << prod_id << "\" ";
        xml << "Asset_Name=\"" << video_title << " HD\" ";
        xml << "Creation_Date=\"" << current_date << "\" ";
        xml << "Description=\"" << video_title << " HD Package\" ";
        xml << "Provider=\"000600\" Verb=\"\" Version_Major=\"1\" Version_Minor=\"0\" />\n";
        xml << "  </Metadata>\n";
        xml << "  <Asset>\n";
        xml << "    <Metadata>\n";
        xml << "      <AMS Asset_Class=\"title\" Asset_ID=\"" << prod_id << "\" ";
        xml << "Asset_Name=\"" << video_title << " HD Title\" ";
        xml << "Creation_Date=\"" << current_date << "\" ";
        xml << "Description=\"" << video_title << " HD Title\" ";
        xml << "Provider=\"000600\" Verb=\"\" Version_Major=\"1\" Version_Minor=\"0\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Type\" Value=\"title\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Year\" Value=\"2024\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Category\" Value=\"VODAll/ფავორიტი\" />\n";
        xml << "      <App_Data App=\"MOD\" Language=\"en\" Name=\"Genre\" Value=\"General\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Licensing_Window_Start\" Value=\"" << current_date << "\"/>\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Licensing_Window_End\" Value=\"" << end_date << "\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Country_of_Origin\" Value=\"1\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Preview_Period\" Value=\"300\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Suggested_Price\" Value=\"0\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Is_Series\" Value=\"N\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Rating\" Value=\"General\" />\n";
        xml << "      <App_Data App=\"MOD\" Language=\"en\" Name=\"Title\" Value=\"" << video_title << "\" />\n";
        xml << "      <App_Data App=\"MOD\" Language=\"ka\" Name=\"Title\" Value=\"" << video_title << "\" />\n";
        xml << "      <App_Data App=\"MOD\" Language=\"ru\" Name=\"Title\" Value=\"" << video_title << "\" />\n";
        xml << "      <App_Data App=\"MOD\" Language=\"en\" Name=\"Summary_Medium\" Value=\"" << video_title << "\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Summary_Medium\" Language=\"ka\" Value=\"" << video_title << "\" />\n";
        xml << "      <App_Data App=\"MOD\" Name=\"Summary_Medium\" Language=\"ru\" Value=\"" << video_title << "\" />\n";
        xml << "    </Metadata>\n";
        xml << "    <Asset>\n";
        xml << "      <Metadata>\n";
        xml << "        <AMS Asset_Class=\"movie\" Asset_ID=\"" << asset_id << "\" ";
        xml << "Asset_Name=\"" << video_title << " HD Content\" ";
        xml << "Creation_Date=\"" << current_date << "\" ";
        xml << "Description=\"" << video_title << " HD Content\" ";
        xml << "Provider=\"000600\" Verb=\"\" Version_Major=\"1\" Version_Minor=\"0\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"Encryption\" Value=\"N\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"Type\" Value=\"movie\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"HDContent\" Value=\"Y\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"Languages\" Value=\"ka\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"Subtitle_Languages\" Value=\"\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"Bit_Rate\" Value=\"3500\" />\n";
        xml << "        <App_Data Value=\"WEBTV\" Name=\"Domain\" App=\"MOD\"/>\n";
        xml << "        <App_Data App=\"MOD\" Name=\"Encoder_Mode\" Value=\"3\"/>\n";
        xml << "        <App_Data App=\"MOD\" Name=\"MimeType\" Value=\"HLS\"/>\n";
        xml << "        <App_Data App=\"MOD\" Name=\"IsPreview\" Value=\"Y\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"PreviewDuration\" Value=\"300\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"PreviewStartTime\" Value=\"0\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"Video_Codec_Type\" Value=\"2\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"Audio_Codec_Type\" Value=\"AAC\" />\n";
        xml << "      </Metadata>\n";
        xml << "      <Content Value=\"playlist.m3u8\" />\n";
        xml << "    </Asset>\n";
        xml << "    <Asset>\n";
        xml << "      <Metadata>\n";
        xml << "        <AMS Asset_Class=\"box cover\" Asset_ID=\"" << poster_id << "\" ";
        xml << "Asset_Name=\"" << video_title << " HD Poster\" ";
        xml << "Creation_Date=\"" << current_date << "\" ";
        xml << "Description=\"" << video_title << " HD Poster\" ";
        xml << "Provider=\"000600\" Verb=\"\" Version_Major=\"1\" Version_Minor=\"0\" />\n";
        xml << "        <App_Data App=\"MOD\" Name=\"Type\" Value=\"poster\" />\n";
        xml << "      </Metadata>\n";
        xml << "      <Content Value=\"" << basename << "-poster1.jpg\" />\n";
        xml << "    </Asset>\n";
        xml << "  </Asset>\n";
        xml << "</ADI>\n";
        
        xml.close();
        log("VOD XML generated: " + xml_path.string());
        return true;
    }
    
    bool convertToHLS(const fs::path& input_file, const fs::path& output_dir) {
        log("Starting HLS conversion: " + input_file.string());
        
        // Create output directory
        fs::create_directories(output_dir);
        
        std::string basename = input_file.stem().string();
        
        for (const auto& profile : config.profiles) {
            fs::path profile_dir = output_dir / profile.folder_name;
            fs::create_directories(profile_dir);
            
            std::stringstream cmd;
            cmd << "ffmpeg -i \"" << input_file.string() << "\" ";
            cmd << "-c:v libx264 -preset " << config.preset << " ";
            cmd << "-profile:v " << config.h264_profile << " ";
            cmd << "-level:v " << config.h264_level << " ";
            cmd << "-vf \"scale=" << profile.width << ":" << profile.height << ":force_original_aspect_ratio=decrease,pad=" 
                << profile.width << ":" << profile.height << ":(ow-iw)/2:(oh-ih)/2\" ";
            cmd << "-b:v " << profile.video_bitrate << " ";
            cmd << "-maxrate " << static_cast<int>(profile.video_bitrate * 1.1) << " ";
            cmd << "-bufsize " << profile.video_bitrate * 2 << " ";
            cmd << "-c:a aac -b:a " << profile.audio_bitrate << " -ac 2 ";
            cmd << "-f hls -hls_time " << config.segment_duration << " ";
            cmd << "-hls_playlist_type vod ";
            cmd << "-hls_segment_filename \"" << profile_dir.string() << "/segment_%03d.ts\" ";
            cmd << "-loglevel " << config.log_level << " ";
            if (config.threads > 0) {
                cmd << "-threads " << config.threads << " ";
            }
            cmd << "\"" << (profile_dir / "index.m3u8").string() << "\"";
            
            log("Converting profile: " + profile.name);
            
            int result = system(cmd.str().c_str());
            if (result != 0) {
                log("ERROR: Failed to convert profile " + profile.name);
                return false;
            }
        }
        
        // Create master playlist
        fs::path playlist_path = output_dir / "playlist.m3u8";
        std::ofstream playlist(playlist_path);
        
        if (!playlist.is_open()) {
            log("ERROR: Cannot create master playlist");
            return false;
        }
        
        playlist << "#EXTM3U\n";
        playlist << "#EXT-X-VERSION:3\n\n";
        
        for (const auto& profile : config.profiles) {
            playlist << "#EXT-X-STREAM-INF:BANDWIDTH=" << profile.bandwidth;
            playlist << ",RESOLUTION=" << profile.width << "x" << profile.height;
            playlist << "\n";
            playlist << profile.folder_name << "/index.m3u8\n\n";
        }
        
        playlist.close();
        
        // Generate posters from the input video
        if (!generatePosters(input_file, output_dir, basename)) {
            log("WARNING: Failed to generate posters, continuing anyway");
        }
        
        // Generate VOD XML metadata
        if (!generateVODXML(output_dir, basename)) {
            log("WARNING: Failed to generate VOD XML, continuing anyway");
        }
        
        log("HLS conversion completed: " + output_dir.string());
        return true;
    }
    
    bool uploadToSFTP(const fs::path& local_dir, const std::string& remote_name) {
        if (!config.sftp_enabled) {
            return true;
        }
        
        log("Starting SFTP upload: " + local_dir.string() + " -> " + config.sftp_remote_path + "/" + remote_name);
        
        // Check if sshpass is installed
        if (system("which sshpass > /dev/null 2>&1") != 0) {
            log("ERROR: sshpass is not installed. Please install it manually:");
            log("  sudo apt-get install sshpass");
            return false;
        }
        
        for (int attempt = 1; attempt <= config.sftp_retry_attempts; attempt++) {
            // Create batch file for SFTP commands
            std::string batch_file = "/tmp/sftp_batch_" + std::to_string(std::time(nullptr)) + ".txt";
            std::ofstream batch(batch_file);
            
            // Navigate to remote path first
            if (!config.sftp_remote_path.empty() && config.sftp_remote_path != "/") {
                batch << "cd " << config.sftp_remote_path << "\n";
            }
            
            // Create directory for this upload
            batch << "-mkdir " << remote_name << "\n";  // Use -mkdir to ignore error if exists
            batch << "cd " << remote_name << "\n";
            
            // Upload all files and directories
            for (const auto& entry : fs::recursive_directory_iterator(local_dir)) {
                if (fs::is_regular_file(entry)) {
                    fs::path rel_path = fs::relative(entry.path(), local_dir);
                    fs::path remote_file_dir = rel_path.parent_path();
                    
                    if (!remote_file_dir.empty() && remote_file_dir != ".") {
                        batch << "-mkdir " << remote_file_dir.string() << "\n";
                    }
                    
                    batch << "put \"" << entry.path().string() << "\" \"" 
                          << rel_path.string() << "\"\n";
                }
            }
            
            batch.close();
            
            // Build SFTP command (connect without path in URL)
            std::stringstream cmd;
            cmd << "sshpass -p '" << config.sftp_password << "' ";
            cmd << "sftp -oBatchMode=no -oStrictHostKeyChecking=no ";
            cmd << "-P " << config.sftp_port << " ";
            cmd << config.sftp_username << "@" << config.sftp_host << " ";
            cmd << "< " << batch_file << " 2>&1";
            
            log("SFTP upload attempt " + std::to_string(attempt) + "/" + 
                std::to_string(config.sftp_retry_attempts));
            
            int result = system(cmd.str().c_str());
            
            // Clean up batch file
            fs::remove(batch_file);
            
            if (result == 0) {
                log("SFTP upload successful");
                return true;
            } else {
                log("SFTP upload failed (attempt " + std::to_string(attempt) + ")");
                if (attempt < config.sftp_retry_attempts) {
                    std::this_thread::sleep_for(std::chrono::seconds(config.sftp_retry_delay));
                }
            }
        }
        
        log("ERROR: SFTP upload failed after all retry attempts");
        return false;
    }
    
public:
    bool initialize(const std::string& config_file) {
        if (!config.loadFromFile(config_file)) {
            return false;
        }
        
        // Validate directories
        if (!fs::exists(config.source_dir)) {
            std::cerr << "Error: Source directory does not exist: " << config.source_dir << "\n";
            return false;
        }
        
        // Create destination directory if it doesn't exist
        fs::create_directories(config.dest_dir);
        
        // Open log file if specified
        if (!config.log_file.empty()) {
            log_stream.open(config.log_file, std::ios::app);
            if (!log_stream.is_open()) {
                std::cerr << "Warning: Cannot open log file: " << config.log_file << "\n";
            }
        }
        
        // Load previously processed files
        std::string processed_file = config.dest_dir + "/.processed_files";
        std::ifstream pf(processed_file);
        if (pf.is_open()) {
            std::string line;
            while (std::getline(pf, line)) {
                processed_files.insert(line);
            }
        }
        
        return true;
    }
    
    void run() {
        log("HLS Watcher with SFTP started");
        log("Source: " + config.source_dir);
        log("Destination: " + config.dest_dir);
        log("SFTP Enabled: " + std::string(config.sftp_enabled ? "YES" : "NO"));
        if (config.sftp_enabled) {
            log("SFTP: " + config.sftp_username + "@" + config.sftp_host + ":" + 
                std::to_string(config.sftp_port) + config.sftp_remote_path);
        } else {
            log("SFTP is disabled in configuration");
        }
        log("Watching for: " + std::accumulate(config.file_extensions.begin(), 
            config.file_extensions.end(), std::string(),
            [](const std::string& a, const std::string& b) {
                return a.empty() ? b : a + ", " + b;
            }));
        
        while (g_running) {
            try {
                for (const auto& entry : fs::directory_iterator(config.source_dir)) {
                    if (!g_running) break;
                    
                    if (fs::is_regular_file(entry) && hasValidExtension(entry.path())) {
                        std::string filename = entry.path().filename().string();
                        
                        if (processed_files.find(filename) == processed_files.end()) {
                            log("New file detected: " + filename);
                            
                            if (!isFileStable(entry.path())) {
                                log("File is still being written: " + filename);
                                continue;
                            }
                            
                            std::string basename = entry.path().stem().string();
                            fs::path output_dir = fs::path(config.dest_dir) / basename;
                            
                            if (convertToHLS(entry.path(), output_dir)) {
                                processed_files.insert(filename);
                                saveProcessedFiles();
                                
                                // SFTP upload if enabled
                                bool upload_success = true;
                                if (config.sftp_enabled) {
                                    upload_success = uploadToSFTP(output_dir, basename);
                                }
                                
                                // Delete source file if configured and upload successful
                                if (upload_success && config.sftp_delete_source_after_upload && config.sftp_enabled) {
                                    fs::remove(entry.path());
                                    log("Deleted source file: " + filename);
                                }
                                
                                // Delete local HLS files if configured and upload successful
                                if (upload_success && config.sftp_delete_local_after_upload && config.sftp_enabled) {
                                    fs::remove_all(output_dir);
                                    log("Deleted local HLS directory: " + output_dir.string());
                                }
                                
                                // Delete source file if configured (non-SFTP mode)
                                if (config.delete_source && !config.sftp_enabled) {
                                    fs::remove(entry.path());
                                    log("Deleted source file: " + filename);
                                }
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                log("ERROR: " + std::string(e.what()));
            }
            
            if (g_running) {
                std::this_thread::sleep_for(std::chrono::seconds(config.watch_interval));
            }
        }
        
        log("HLS Watcher stopped");
    }
    
private:
    void saveProcessedFiles() {
        std::string processed_file = config.dest_dir + "/.processed_files";
        std::ofstream pf(processed_file);
        for (const auto& file : processed_files) {
            pf << file << "\n";
        }
    }
};

int run_watcher(const std::string& config_file) {
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    HLSWatcherSFTP watcher;
    
    if (!watcher.initialize(config_file)) {
        return 1;
    }
    
    watcher.run();
    return 0;
}