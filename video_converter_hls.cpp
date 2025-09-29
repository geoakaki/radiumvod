#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <sstream>

namespace fs = std::filesystem;

// HLS Profile definitions based on your requirements
struct HLSProfile {
    std::string name;
    int width;
    int height;
    int video_bitrate;
    int audio_bitrate;
    int bandwidth;  // Total bandwidth for playlist
    std::string folder_name;
};

// Define HLS profiles matching your specifications
std::vector<HLSProfile> HLS_PROFILES = {
    {
        "720p",
        1280, 720,
        3200000,      // 3.2 Mbps video
        128000,       // 128 kbps audio  
        3500000,      // 3.5 Mbps total bandwidth
        "stream_3500"
    },
    {
        "432p",
        768, 432,
        1300000,      // 1.3 Mbps video
        96000,        // 96 kbps audio
        1500000,      // 1.5 Mbps total bandwidth
        "stream_1500"
    },
    {
        "288p",
        512, 288,
        400000,       // 400 kbps video
        64000,        // 64 kbps audio
        500000,       // 500 kbps total bandwidth
        "stream_500"
    }
};

class VideoConverterHLS {
private:
    std::string input_file;
    std::string output_dir;
    std::vector<HLSProfile> profiles;
    int segment_duration = 10;  // 10 second segments
    
public:
    VideoConverterHLS(const std::string& in, const std::string& out_dir) 
        : input_file(in), output_dir(out_dir), profiles(HLS_PROFILES) {
        // Remove trailing slash if present
        if (output_dir.back() == '/' || output_dir.back() == '\\') {
            output_dir.pop_back();
        }
    }
    
    bool convert() {
        std::cout << "Starting HLS conversion with " << profiles.size() << " profiles\n";
        std::cout << "Output directory: " << output_dir << "\n\n";
        
        // Create output directory structure
        if (!createDirectoryStructure()) {
            std::cerr << "Failed to create directory structure\n";
            return false;
        }
        
        // Process each profile
        bool all_success = true;
        for (const auto& profile : profiles) {
            std::cout << "\nProcessing " << profile.name << " profile:\n";
            std::cout << "  Resolution: " << profile.width << "x" << profile.height << "\n";
            std::cout << "  Video bitrate: " << (profile.video_bitrate/1000) << " kbps\n";
            std::cout << "  Audio bitrate: " << (profile.audio_bitrate/1000) << " kbps\n";
            std::cout << "  Total bandwidth: " << (profile.bandwidth/1000) << " kbps\n";
            
            if (!processProfile(profile)) {
                std::cerr << "Failed to process profile: " << profile.name << "\n";
                all_success = false;
            }
        }
        
        // Generate master playlist
        if (all_success) {
            if (!generateMasterPlaylist()) {
                std::cerr << "Failed to generate master playlist\n";
                return false;
            }
            std::cout << "\n✅ HLS conversion completed successfully!\n";
            std::cout << "Master playlist: " << output_dir << "/playlist.m3u8\n";
        }
        
        return all_success;
    }
    
private:
    bool createDirectoryStructure() {
        try {
            // Create main output directory
            fs::create_directories(output_dir);
            
            // Create subdirectories for each profile
            for (const auto& profile : profiles) {
                fs::create_directories(output_dir + "/" + profile.folder_name);
            }
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error creating directories: " << e.what() << "\n";
            return false;
        }
    }
    
    bool processProfile(const HLSProfile& profile) {
        std::string profile_dir = output_dir + "/" + profile.folder_name;
        
        // Build FFmpeg command for HLS segmentation
        std::stringstream cmd;
        cmd << "ffmpeg -i \"" << input_file << "\" ";
        
        // Video encoding settings
        cmd << "-c:v libx264 ";
        cmd << "-b:v " << profile.video_bitrate << " ";
        cmd << "-maxrate " << profile.video_bitrate << " ";
        cmd << "-bufsize " << (profile.video_bitrate * 2) << " ";
        cmd << "-vf scale=" << profile.width << ":" << profile.height << " ";
        cmd << "-preset fast ";
        cmd << "-profile:v high ";
        cmd << "-level 4.1 ";
        
        // Force keyframe every segment_duration seconds
        cmd << "-g " << (30 * segment_duration) << " ";  // GOP size (30 fps * segment duration)
        cmd << "-keyint_min " << (30 * segment_duration) << " ";
        cmd << "-sc_threshold 0 ";  // Disable scene cut detection
        
        // Audio encoding settings
        cmd << "-c:a aac ";
        cmd << "-b:a " << profile.audio_bitrate << " ";
        cmd << "-ar 44100 ";
        cmd << "-ac 2 ";
        
        // HLS specific settings
        cmd << "-f hls ";
        cmd << "-hls_time " << segment_duration << " ";
        cmd << "-hls_list_size 0 ";  // Keep all segments in playlist
        cmd << "-hls_segment_filename \"" << profile_dir << "/segment_%03d.ts\" ";
        cmd << "-hls_flags independent_segments ";
        cmd << "-master_pl_name playlist.m3u8 ";
        
        // Output playlist
        cmd << "\"" << profile_dir << "/index.m3u8\" ";
        
        // Add overwrite flag and hide banner
        cmd << "-y -hide_banner -loglevel warning 2>&1";
        
        std::cout << "  Executing: Segmenting video into HLS format...\n";
        
        // Execute FFmpeg command
        int result = system(cmd.str().c_str());
        
        if (result != 0) {
            std::cerr << "  ❌ FFmpeg failed for profile: " << profile.name << "\n";
            return false;
        }
        
        // Verify output files exist
        if (!fs::exists(profile_dir + "/index.m3u8")) {
            std::cerr << "  ❌ Playlist not created for profile: " << profile.name << "\n";
            return false;
        }
        
        // Count segments created
        int segment_count = 0;
        for (const auto& entry : fs::directory_iterator(profile_dir)) {
            if (entry.path().extension() == ".ts") {
                segment_count++;
            }
        }
        
        std::cout << "  ✅ Created " << segment_count << " segments\n";
        
        return true;
    }
    
    bool generateMasterPlaylist() {
        std::string playlist_path = output_dir + "/playlist.m3u8";
        std::ofstream playlist(playlist_path);
        
        if (!playlist.is_open()) {
            std::cerr << "Failed to create master playlist\n";
            return false;
        }
        
        // Write HLS header
        playlist << "#EXTM3U\n";
        playlist << "#EXT-X-VERSION:3\n\n";
        
        // Write stream info for each profile
        for (const auto& profile : profiles) {
            playlist << "#EXT-X-STREAM-INF:BANDWIDTH=" << profile.bandwidth;
            playlist << ",RESOLUTION=" << profile.width << "x" << profile.height;
            playlist << "\n";
            playlist << profile.folder_name << "/index.m3u8\n\n";
        }
        
        playlist.close();
        
        std::cout << "\n✅ Master playlist created: " << playlist_path << "\n";
        
        // Display the playlist content
        std::cout << "\n--- Master Playlist Content ---\n";
        std::ifstream display(playlist_path);
        std::string line;
        while (std::getline(display, line)) {
            std::cout << line << "\n";
        }
        std::cout << "--- End of Playlist ---\n";
        
        return true;
    }
};

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <input_file> <output_directory>\n";
    std::cout << "\nConverts video to HLS format with adaptive bitrate streaming\n";
    std::cout << "\nCreates three quality profiles:\n";
    std::cout << "  - 1280x720 @ 3.5Mbps (stream_3500)\n";
    std::cout << "  - 768x432 @ 1.5Mbps (stream_1500)\n";
    std::cout << "  - 512x288 @ 500kbps (stream_500)\n";
    std::cout << "\nOutput structure:\n";
    std::cout << "  output_dir/\n";
    std::cout << "    ├── playlist.m3u8           (master playlist)\n";
    std::cout << "    ├── stream_3500/\n";
    std::cout << "    │   ├── index.m3u8          (variant playlist)\n";
    std::cout << "    │   └── segment_*.ts        (video segments)\n";
    std::cout << "    ├── stream_1500/\n";
    std::cout << "    │   ├── index.m3u8\n";
    std::cout << "    │   └── segment_*.ts\n";
    std::cout << "    └── stream_500/\n";
    std::cout << "        ├── index.m3u8\n";
    std::cout << "        └── segment_*.ts\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << program << " input.mp4 output_hls\n";
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string input_file = argv[1];
    std::string output_dir = argv[2];
    
    // Check if input file exists
    if (!fs::exists(input_file)) {
        std::cerr << "Error: Input file does not exist: " << input_file << "\n";
        return 1;
    }
    
    // Check if FFmpeg is available
    if (system("ffmpeg -version > /dev/null 2>&1") != 0) {
        std::cerr << "Error: FFmpeg is not installed or not in PATH\n";
        std::cerr << "Please install FFmpeg first\n";
        return 1;
    }
    
    std::cout << "=================================\n";
    std::cout << "HLS Video Converter\n";
    std::cout << "=================================\n";
    std::cout << "Input: " << input_file << "\n";
    std::cout << "Output: " << output_dir << "\n";
    std::cout << "=================================\n\n";
    
    VideoConverterHLS converter(input_file, output_dir);
    
    if (converter.convert()) {
        std::cout << "\n✨ HLS conversion successful!\n";
        std::cout << "You can now serve the " << output_dir << " directory with any HTTP server\n";
        std::cout << "and play the stream using the playlist.m3u8 file\n";
        return 0;
    } else {
        std::cerr << "\n❌ HLS conversion failed!\n";
        return 1;
    }
}