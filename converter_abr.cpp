#include "converter_abr.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <cstdint>
#include <map>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace fs = std::filesystem;

// ABR Profile definitions
struct ABRProfile {
    std::string name;
    int width;
    int height;
    int video_bitrate;
    int audio_bitrate;
    std::string h264_profile;
    std::string h264_level;
    int keyframe_interval;
    std::string preset;
};

// Define 3 ABR profiles based on specifications
std::vector<ABRProfile> ABR_PROFILES = {
    // High Quality - 1080p
    {
        "high",
        1920, 1080,           // Resolution: Full HD
        4000000,              // Video: 4 Mbps
        128000,               // Audio: 128 kbps
        "high", "4.1",        // H.264 High Profile, Level 4.1
        120,                  // Keyframe interval (2 seconds at 60fps)
        "slow"                // Better quality encoding
    },
    
    // Medium Quality - 720p
    {
        "medium", 
        1280, 720,            // Resolution: HD
        2500000,              // Video: 2.5 Mbps
        96000,                // Audio: 96 kbps
        "main", "3.1",        // H.264 Main Profile, Level 3.1
        120,                  // Keyframe interval
        "medium"              // Balanced speed/quality
    },
    
    // Low Quality - 480p
    {
        "low",
        854, 480,             // Resolution: SD (16:9)
        1200000,              // Video: 1.2 Mbps
        64000,                // Audio: 64 kbps
        "baseline", "3.0",    // H.264 Baseline Profile, Level 3.0
        120,                  // Keyframe interval
        "faster"              // Faster encoding
    }
};

class VideoConverterABR {
private:
    std::string input_file;
    std::string output_base;
    std::vector<ABRProfile> profiles_to_encode;
    AVFormatContext* input_ctx = nullptr;
    
    struct StreamContext {
        AVCodecContext* decoder_ctx = nullptr;
        AVStream* input_stream = nullptr;
        int stream_index = -1;
    };
    
    struct EncoderContext {
        AVFormatContext* output_ctx = nullptr;
        AVCodecContext* video_encoder_ctx = nullptr;
        AVCodecContext* audio_encoder_ctx = nullptr;
        AVStream* video_stream = nullptr;
        AVStream* audio_stream = nullptr;
        SwsContext* sws_ctx = nullptr;
        SwrContext* swr_ctx = nullptr;
        int64_t video_next_pts = 0;
        int64_t audio_next_pts = 0;
        ABRProfile profile;
        std::string output_file;
    };
    
    StreamContext video_decoder;
    StreamContext audio_decoder;
    std::vector<EncoderContext*> encoders;
    
public:
    VideoConverterABR(const std::string& in, const std::string& out_base, const std::string& profile_arg) 
        : input_file(in), output_base(out_base) {
        
        // Parse profile argument
        if (profile_arg == "all") {
            profiles_to_encode = ABR_PROFILES;
        } else {
            for (const auto& profile : ABR_PROFILES) {
                if (profile.name == profile_arg) {
                    profiles_to_encode.push_back(profile);
                    break;
                }
            }
            if (profiles_to_encode.empty()) {
                std::cerr << "Unknown profile: " << profile_arg << "\n";
                std::cerr << "Available profiles: high, medium, low, all\n";
                exit(1);
            }
        }
    }
    
    ~VideoConverterABR() {
        cleanup();
    }
    
    bool convert() {
        std::cout << "Starting ABR conversion with " << profiles_to_encode.size() << " profile(s)\n";
        
        if (!openInputFile()) {
            std::cerr << "Failed to open input file\n";
            return false;
        }
        
        // Setup encoders for each profile
        for (const auto& profile : profiles_to_encode) {
            std::cout << "\nSetting up " << profile.name << " profile:\n";
            std::cout << "  Resolution: " << profile.width << "x" << profile.height << "\n";
            std::cout << "  Video bitrate: " << (profile.video_bitrate/1000) << " kbps\n";
            std::cout << "  Audio bitrate: " << (profile.audio_bitrate/1000) << " kbps\n";
            
            if (!setupEncoder(profile)) {
                std::cerr << "Failed to setup encoder for profile: " << profile.name << "\n";
                return false;
            }
        }
        
        // Write headers for all outputs
        for (auto* encoder : encoders) {
            if (!writeHeader(encoder)) {
                std::cerr << "Failed to write header for: " << encoder->output_file << "\n";
                return false;
            }
        }
        
        // Transcode to all profiles simultaneously
        if (!transcodeAllProfiles()) {
            std::cerr << "Failed to transcode streams\n";
            return false;
        }
        
        // Write trailers for all outputs
        for (auto* encoder : encoders) {
            av_write_trailer(encoder->output_ctx);
            std::cout << "Completed: " << encoder->output_file << "\n";
        }
        
        return true;
    }
    
private:
    bool openInputFile() {
        int ret = avformat_open_input(&input_ctx, input_file.c_str(), nullptr, nullptr);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Cannot open input file: " << errbuf << "\n";
            return false;
        }
        
        if (avformat_find_stream_info(input_ctx, nullptr) < 0) {
            std::cerr << "Cannot find stream information\n";
            return false;
        }
        
        // Find video and audio streams
        for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
            AVStream* stream = input_ctx->streams[i];
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_decoder.stream_index < 0) {
                video_decoder.stream_index = i;
                video_decoder.input_stream = stream;
                if (!setupDecoder(stream, video_decoder)) {
                    return false;
                }
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_decoder.stream_index < 0) {
                audio_decoder.stream_index = i;
                audio_decoder.input_stream = stream;
                if (!setupDecoder(stream, audio_decoder)) {
                    audio_decoder.stream_index = -1;
                }
            }
        }
        
        if (video_decoder.stream_index < 0) {
            std::cerr << "No video stream found\n";
            return false;
        }
        
        return true;
    }
    
    bool setupDecoder(AVStream* stream, StreamContext& ctx) {
        const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) {
            std::cerr << "Failed to find decoder\n";
            return false;
        }
        
        ctx.decoder_ctx = avcodec_alloc_context3(decoder);
        if (!ctx.decoder_ctx) {
            std::cerr << "Failed to allocate decoder context\n";
            return false;
        }
        
        if (avcodec_parameters_to_context(ctx.decoder_ctx, stream->codecpar) < 0) {
            std::cerr << "Failed to copy codec parameters\n";
            return false;
        }
        
        ctx.decoder_ctx->time_base = stream->time_base;
        
        if (avcodec_open2(ctx.decoder_ctx, decoder, nullptr) < 0) {
            std::cerr << "Failed to open decoder\n";
            return false;
        }
        
        return true;
    }
    
    bool setupEncoder(const ABRProfile& profile) {
        EncoderContext* encoder = new EncoderContext();
        encoder->profile = profile;
        
        // Generate output filename
        std::string extension = ".mp4";
        std::string base = output_base;
        if (base.find('.') != std::string::npos) {
            base = base.substr(0, base.find_last_of('.'));
        }
        encoder->output_file = base + "_" + profile.name + extension;
        
        // Create output context
        avformat_alloc_output_context2(&encoder->output_ctx, nullptr, nullptr, encoder->output_file.c_str());
        if (!encoder->output_ctx) {
            std::cerr << "Could not create output context\n";
            delete encoder;
            return false;
        }
        
        // Setup video encoder
        if (!setupVideoEncoder(encoder)) {
            delete encoder;
            return false;
        }
        
        // Setup audio encoder if audio stream exists
        if (audio_decoder.stream_index >= 0) {
            if (!setupAudioEncoder(encoder)) {
                std::cerr << "Warning: Failed to setup audio encoder for " << profile.name << "\n";
            }
        }
        
        encoders.push_back(encoder);
        return true;
    }
    
    bool setupVideoEncoder(EncoderContext* encoder) {
        const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
        if (!codec) {
            std::cerr << "x264 encoder not found\n";
            return false;
        }
        
        encoder->video_stream = avformat_new_stream(encoder->output_ctx, nullptr);
        if (!encoder->video_stream) {
            std::cerr << "Failed to allocate video stream\n";
            return false;
        }
        
        encoder->video_encoder_ctx = avcodec_alloc_context3(codec);
        if (!encoder->video_encoder_ctx) {
            std::cerr << "Failed to allocate video encoder context\n";
            return false;
        }
        
        // Set encoding parameters from profile
        encoder->video_encoder_ctx->width = encoder->profile.width;
        encoder->video_encoder_ctx->height = encoder->profile.height;
        encoder->video_encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        encoder->video_encoder_ctx->bit_rate = encoder->profile.video_bitrate;
        encoder->video_encoder_ctx->gop_size = encoder->profile.keyframe_interval;
        encoder->video_encoder_ctx->max_b_frames = 2;
        
        // Set framerate and timebase
        AVRational input_framerate = av_guess_frame_rate(input_ctx, video_decoder.input_stream, nullptr);
        if (input_framerate.num == 0 || input_framerate.den == 0) {
            input_framerate = AVRational{30, 1};
        }
        
        encoder->video_encoder_ctx->framerate = input_framerate;
        encoder->video_encoder_ctx->time_base = av_inv_q(input_framerate);
        encoder->video_stream->time_base = encoder->video_encoder_ctx->time_base;
        
        // x264 specific options
        av_opt_set(encoder->video_encoder_ctx->priv_data, "preset", encoder->profile.preset.c_str(), 0);
        av_opt_set(encoder->video_encoder_ctx->priv_data, "profile", encoder->profile.h264_profile.c_str(), 0);
        av_opt_set(encoder->video_encoder_ctx->priv_data, "level", encoder->profile.h264_level.c_str(), 0);
        av_opt_set(encoder->video_encoder_ctx->priv_data, "tune", "film", 0);
        
        // Use CBR for consistent streaming
        av_opt_set(encoder->video_encoder_ctx->priv_data, "nal-hrd", "cbr", 0);
        std::string x264opts = "keyint=" + std::to_string(encoder->profile.keyframe_interval) + 
                              ":min-keyint=" + std::to_string(encoder->profile.keyframe_interval/2) + 
                              ":no-scenecut";
        av_opt_set(encoder->video_encoder_ctx->priv_data, "x264opts", x264opts.c_str(), 0);
        
        if (encoder->output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            encoder->video_encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        if (avcodec_open2(encoder->video_encoder_ctx, codec, nullptr) < 0) {
            std::cerr << "Failed to open video encoder\n";
            return false;
        }
        
        if (avcodec_parameters_from_context(encoder->video_stream->codecpar, 
                                           encoder->video_encoder_ctx) < 0) {
            std::cerr << "Failed to copy video codec parameters\n";
            return false;
        }
        
        // Setup scaler
        encoder->sws_ctx = sws_getContext(
            video_decoder.decoder_ctx->width, video_decoder.decoder_ctx->height,
            video_decoder.decoder_ctx->pix_fmt,
            encoder->profile.width, encoder->profile.height, AV_PIX_FMT_YUV420P,
            SWS_BICUBIC, nullptr, nullptr, nullptr
        );
        
        if (!encoder->sws_ctx) {
            std::cerr << "Failed to create scaler context\n";
            return false;
        }
        
        return true;
    }
    
    bool setupAudioEncoder(EncoderContext* encoder) {
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec) {
            std::cerr << "AAC encoder not found\n";
            return false;
        }
        
        encoder->audio_stream = avformat_new_stream(encoder->output_ctx, nullptr);
        if (!encoder->audio_stream) {
            return false;
        }
        
        encoder->audio_encoder_ctx = avcodec_alloc_context3(codec);
        if (!encoder->audio_encoder_ctx) {
            return false;
        }
        
        // Setup audio parameters
        encoder->audio_encoder_ctx->sample_rate = audio_decoder.decoder_ctx->sample_rate;
        encoder->audio_encoder_ctx->ch_layout = audio_decoder.decoder_ctx->ch_layout;
        encoder->audio_encoder_ctx->sample_fmt = codec->sample_fmts[0];
        encoder->audio_encoder_ctx->bit_rate = encoder->profile.audio_bitrate;
        encoder->audio_encoder_ctx->time_base = AVRational{1, encoder->audio_encoder_ctx->sample_rate};
        
        encoder->audio_stream->time_base = encoder->audio_encoder_ctx->time_base;
        
        if (encoder->output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            encoder->audio_encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        if (avcodec_open2(encoder->audio_encoder_ctx, codec, nullptr) < 0) {
            return false;
        }
        
        if (avcodec_parameters_from_context(encoder->audio_stream->codecpar,
                                           encoder->audio_encoder_ctx) < 0) {
            return false;
        }
        
        // Setup audio resampler if needed
        if (audio_decoder.decoder_ctx->sample_fmt != encoder->audio_encoder_ctx->sample_fmt ||
            audio_decoder.decoder_ctx->sample_rate != encoder->audio_encoder_ctx->sample_rate ||
            av_channel_layout_compare(&audio_decoder.decoder_ctx->ch_layout, 
                                     &encoder->audio_encoder_ctx->ch_layout) != 0) {
            
            encoder->swr_ctx = swr_alloc();
            if (!encoder->swr_ctx) {
                return false;
            }
            
            av_opt_set_chlayout(encoder->swr_ctx, "in_chlayout", &audio_decoder.decoder_ctx->ch_layout, 0);
            av_opt_set_int(encoder->swr_ctx, "in_sample_rate", audio_decoder.decoder_ctx->sample_rate, 0);
            av_opt_set_sample_fmt(encoder->swr_ctx, "in_sample_fmt", audio_decoder.decoder_ctx->sample_fmt, 0);
            
            av_opt_set_chlayout(encoder->swr_ctx, "out_chlayout", &encoder->audio_encoder_ctx->ch_layout, 0);
            av_opt_set_int(encoder->swr_ctx, "out_sample_rate", encoder->audio_encoder_ctx->sample_rate, 0);
            av_opt_set_sample_fmt(encoder->swr_ctx, "out_sample_fmt", encoder->audio_encoder_ctx->sample_fmt, 0);
            
            if (swr_init(encoder->swr_ctx) < 0) {
                return false;
            }
        }
        
        return true;
    }
    
    bool writeHeader(EncoderContext* encoder) {
        if (!(encoder->output_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&encoder->output_ctx->pb, encoder->output_file.c_str(), AVIO_FLAG_WRITE) < 0) {
                std::cerr << "Could not open output file: " << encoder->output_file << "\n";
                return false;
            }
        }
        
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        
        int ret = avformat_write_header(encoder->output_ctx, &opts);
        av_dict_free(&opts);
        
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Error writing header: " << errbuf << "\n";
            return false;
        }
        
        return true;
    }
    
    bool transcodeAllProfiles() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        
        // Allocate scaled frames for each encoder
        std::vector<AVFrame*> scaled_frames;
        std::vector<AVFrame*> resampled_frames;
        
        for (auto* encoder : encoders) {
            AVFrame* scaled = av_frame_alloc();
            scaled->format = AV_PIX_FMT_YUV420P;
            scaled->width = encoder->profile.width;
            scaled->height = encoder->profile.height;
            av_frame_get_buffer(scaled, 0);
            scaled_frames.push_back(scaled);
            
            if (encoder->swr_ctx) {
                AVFrame* resampled = av_frame_alloc();
                resampled_frames.push_back(resampled);
            } else {
                resampled_frames.push_back(nullptr);
            }
        }
        
        while (av_read_frame(input_ctx, packet) >= 0) {
            StreamContext* decoder = nullptr;
            
            if (packet->stream_index == video_decoder.stream_index) {
                decoder = &video_decoder;
            } else if (packet->stream_index == audio_decoder.stream_index) {
                decoder = &audio_decoder;
            } else {
                av_packet_unref(packet);
                continue;
            }
            
            // Send packet to decoder
            int ret = avcodec_send_packet(decoder->decoder_ctx, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }
            
            // Receive frames from decoder
            while (ret >= 0) {
                ret = avcodec_receive_frame(decoder->decoder_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    break;
                }
                
                // Process frame for each encoder
                for (size_t i = 0; i < encoders.size(); i++) {
                    if (packet->stream_index == video_decoder.stream_index) {
                        processVideoFrame(encoders[i], frame, scaled_frames[i]);
                    } else if (packet->stream_index == audio_decoder.stream_index && encoders[i]->audio_encoder_ctx) {
                        processAudioFrame(encoders[i], frame, resampled_frames[i]);
                    }
                }
            }
            
            av_packet_unref(packet);
        }
        
        // Flush all encoders
        for (size_t i = 0; i < encoders.size(); i++) {
            flushEncoder(encoders[i]);
        }
        
        // Cleanup
        av_frame_free(&frame);
        for (auto* scaled : scaled_frames) {
            av_frame_free(&scaled);
        }
        for (auto* resampled : resampled_frames) {
            if (resampled) av_frame_free(&resampled);
        }
        av_packet_free(&packet);
        
        return true;
    }
    
    void processVideoFrame(EncoderContext* encoder, AVFrame* input_frame, AVFrame* scaled_frame) {
        // Scale the frame
        sws_scale(encoder->sws_ctx, input_frame->data, input_frame->linesize, 0,
                 input_frame->height, scaled_frame->data, scaled_frame->linesize);
        
        // Set PTS
        scaled_frame->pts = encoder->video_next_pts++;
        
        // Send frame to encoder
        int ret = avcodec_send_frame(encoder->video_encoder_ctx, scaled_frame);
        if (ret < 0) {
            return;
        }
        
        // Receive packets
        receiveAndWritePackets(encoder, encoder->video_encoder_ctx, encoder->video_stream);
    }
    
    void processAudioFrame(EncoderContext* encoder, AVFrame* input_frame, AVFrame* resampled_frame) {
        AVFrame* frame_to_encode = input_frame;
        
        // Resample if needed
        if (encoder->swr_ctx && resampled_frame) {
            resampled_frame->nb_samples = av_rescale_rnd(
                swr_get_delay(encoder->swr_ctx, audio_decoder.decoder_ctx->sample_rate) + input_frame->nb_samples,
                encoder->audio_encoder_ctx->sample_rate,
                audio_decoder.decoder_ctx->sample_rate,
                AV_ROUND_UP
            );
            
            resampled_frame->ch_layout = encoder->audio_encoder_ctx->ch_layout;
            resampled_frame->format = encoder->audio_encoder_ctx->sample_fmt;
            resampled_frame->sample_rate = encoder->audio_encoder_ctx->sample_rate;
            
            av_frame_get_buffer(resampled_frame, 0);
            
            int ret = swr_convert(encoder->swr_ctx,
                                resampled_frame->data, resampled_frame->nb_samples,
                                (const uint8_t**)input_frame->data, input_frame->nb_samples);
            
            if (ret < 0) {
                return;
            }
            
            resampled_frame->pts = encoder->audio_next_pts;
            encoder->audio_next_pts += resampled_frame->nb_samples;
            
            frame_to_encode = resampled_frame;
        } else {
            input_frame->pts = encoder->audio_next_pts;
            encoder->audio_next_pts += input_frame->nb_samples;
        }
        
        // Send frame to encoder
        int ret = avcodec_send_frame(encoder->audio_encoder_ctx, frame_to_encode);
        if (ret < 0) {
            return;
        }
        
        // Receive packets
        receiveAndWritePackets(encoder, encoder->audio_encoder_ctx, encoder->audio_stream);
    }
    
    void receiveAndWritePackets(EncoderContext* encoder, AVCodecContext* codec_ctx, AVStream* stream) {
        AVPacket* packet = av_packet_alloc();
        int ret;
        
        while (true) {
            ret = avcodec_receive_packet(codec_ctx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                break;
            }
            
            packet->stream_index = stream->index;
            av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);
            
            av_interleaved_write_frame(encoder->output_ctx, packet);
            av_packet_unref(packet);
        }
        
        av_packet_free(&packet);
    }
    
    void flushEncoder(EncoderContext* encoder) {
        if (encoder->video_encoder_ctx) {
            avcodec_send_frame(encoder->video_encoder_ctx, nullptr);
            receiveAndWritePackets(encoder, encoder->video_encoder_ctx, encoder->video_stream);
        }
        if (encoder->audio_encoder_ctx) {
            avcodec_send_frame(encoder->audio_encoder_ctx, nullptr);
            receiveAndWritePackets(encoder, encoder->audio_encoder_ctx, encoder->audio_stream);
        }
    }
    
    void cleanup() {
        for (auto* encoder : encoders) {
            if (encoder->video_encoder_ctx) {
                avcodec_free_context(&encoder->video_encoder_ctx);
            }
            if (encoder->audio_encoder_ctx) {
                avcodec_free_context(&encoder->audio_encoder_ctx);
            }
            if (encoder->sws_ctx) {
                sws_freeContext(encoder->sws_ctx);
            }
            if (encoder->swr_ctx) {
                swr_free(&encoder->swr_ctx);
            }
            if (encoder->output_ctx) {
                if (!(encoder->output_ctx->oformat->flags & AVFMT_NOFILE)) {
                    avio_closep(&encoder->output_ctx->pb);
                }
                avformat_free_context(encoder->output_ctx);
            }
            delete encoder;
        }
        
        if (video_decoder.decoder_ctx) {
            avcodec_free_context(&video_decoder.decoder_ctx);
        }
        if (audio_decoder.decoder_ctx) {
            avcodec_free_context(&audio_decoder.decoder_ctx);
        }
        if (input_ctx) {
            avformat_close_input(&input_ctx);
        }
    }
};

int convert_abr(const std::string& input_file, const std::string& output_base, const std::string& profile) {
    // Check if input file exists
    if (!fs::exists(input_file)) {
        std::cerr << "Error: Input file does not exist: " << input_file << "\n";
        return 1;
    }
    
    std::cout << "ABR Video Converter\n";
    std::cout << "==================\n";
    std::cout << "Input: " << input_file << "\n";
    std::cout << "Profile: " << profile << "\n\n";
    
    VideoConverterABR converter(input_file, output_base, profile);
    
    if (converter.convert()) {
        std::cout << "\nConversion successful!\n";
        return 0;
    } else {
        std::cerr << "\nConversion failed!\n";
        return 1;
    }
}