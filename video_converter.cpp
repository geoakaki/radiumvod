#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <regex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace fs = std::filesystem;

class VideoConverter {
private:
    std::string input_file;
    std::string output_file;
    AVFormatContext* input_ctx = nullptr;
    AVFormatContext* output_ctx = nullptr;
    
    struct StreamContext {
        AVCodecContext* decoder_ctx = nullptr;
        AVCodecContext* encoder_ctx = nullptr;
        AVStream* output_stream = nullptr;
        int stream_index = -1;
    };
    
    StreamContext video_stream;
    StreamContext audio_stream;
    SwsContext* sws_ctx = nullptr;
    
public:
    VideoConverter(const std::string& in, const std::string& out) 
        : input_file(in), output_file(out) {}
    
    ~VideoConverter() {
        cleanup();
    }
    
    bool convert() {
        if (!openInputFile()) {
            std::cerr << "Failed to open input file\n";
            return false;
        }
        
        if (!openOutputFile()) {
            std::cerr << "Failed to open output file\n";
            return false;
        }
        
        if (!setupVideoEncoder()) {
            std::cerr << "Failed to setup video encoder\n";
            return false;
        }
        
        if (!setupAudioEncoder()) {
            std::cerr << "Failed to setup audio encoder\n";
            return false;
        }
        
        if (!writeHeader()) {
            std::cerr << "Failed to write header\n";
            return false;
        }
        
        if (!transcodeVideo()) {
            std::cerr << "Failed to transcode video\n";
            return false;
        }
        
        if (!writeTrailer()) {
            std::cerr << "Failed to write trailer\n";
            return false;
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
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream.stream_index < 0) {
                video_stream.stream_index = i;
                if (!setupDecoder(stream, video_stream)) {
                    return false;
                }
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream.stream_index < 0) {
                audio_stream.stream_index = i;
                if (!setupDecoder(stream, audio_stream)) {
                    return false;
                }
            }
        }
        
        if (video_stream.stream_index < 0) {
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
        
        if (avcodec_open2(ctx.decoder_ctx, decoder, nullptr) < 0) {
            std::cerr << "Failed to open decoder\n";
            return false;
        }
        
        return true;
    }
    
    bool openOutputFile() {
        avformat_alloc_output_context2(&output_ctx, nullptr, nullptr, output_file.c_str());
        if (!output_ctx) {
            std::cerr << "Could not create output context\n";
            return false;
        }
        
        return true;
    }
    
    bool setupVideoEncoder() {
        const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
        if (!encoder) {
            std::cerr << "x264 encoder not found. Make sure FFmpeg is built with x264 support\n";
            return false;
        }
        
        video_stream.output_stream = avformat_new_stream(output_ctx, nullptr);
        if (!video_stream.output_stream) {
            std::cerr << "Failed to allocate output stream\n";
            return false;
        }
        
        video_stream.encoder_ctx = avcodec_alloc_context3(encoder);
        if (!video_stream.encoder_ctx) {
            std::cerr << "Failed to allocate encoder context\n";
            return false;
        }
        
        // Set up Full HD output parameters
        video_stream.encoder_ctx->width = 1920;
        video_stream.encoder_ctx->height = 1080;
        video_stream.encoder_ctx->time_base = av_inv_q(av_d2q(25, 1));
        video_stream.encoder_ctx->framerate = av_d2q(25, 1);
        video_stream.encoder_ctx->gop_size = 250;
        video_stream.encoder_ctx->max_b_frames = 2;
        video_stream.encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        video_stream.encoder_ctx->bit_rate = 4000000; // 4 Mbps
        
        // Copy time base from decoder
        if (video_stream.decoder_ctx) {
            video_stream.encoder_ctx->time_base = video_stream.decoder_ctx->time_base;
            video_stream.encoder_ctx->framerate = video_stream.decoder_ctx->framerate;
        }
        
        // x264 specific options
        av_opt_set(video_stream.encoder_ctx->priv_data, "preset", "medium", 0);
        av_opt_set(video_stream.encoder_ctx->priv_data, "tune", "film", 0);
        av_opt_set(video_stream.encoder_ctx->priv_data, "crf", "23", 0);
        
        if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            video_stream.encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        if (avcodec_open2(video_stream.encoder_ctx, encoder, nullptr) < 0) {
            std::cerr << "Failed to open encoder\n";
            return false;
        }
        
        if (avcodec_parameters_from_context(video_stream.output_stream->codecpar, 
                                           video_stream.encoder_ctx) < 0) {
            std::cerr << "Failed to copy codec parameters\n";
            return false;
        }
        
        video_stream.output_stream->time_base = video_stream.encoder_ctx->time_base;
        
        // Setup scaler for resolution conversion
        sws_ctx = sws_getContext(
            video_stream.decoder_ctx->width, video_stream.decoder_ctx->height,
            video_stream.decoder_ctx->pix_fmt,
            1920, 1080, AV_PIX_FMT_YUV420P,
            SWS_BICUBIC, nullptr, nullptr, nullptr
        );
        
        if (!sws_ctx) {
            std::cerr << "Failed to create scaler context\n";
            return false;
        }
        
        return true;
    }
    
    bool setupAudioEncoder() {
        if (audio_stream.stream_index < 0) {
            return true; // No audio stream, skip
        }
        
        const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!encoder) {
            std::cerr << "AAC encoder not found\n";
            return false;
        }
        
        audio_stream.output_stream = avformat_new_stream(output_ctx, nullptr);
        if (!audio_stream.output_stream) {
            std::cerr << "Failed to allocate audio output stream\n";
            return false;
        }
        
        audio_stream.encoder_ctx = avcodec_alloc_context3(encoder);
        if (!audio_stream.encoder_ctx) {
            std::cerr << "Failed to allocate audio encoder context\n";
            return false;
        }
        
        // Copy audio parameters
        audio_stream.encoder_ctx->sample_rate = audio_stream.decoder_ctx->sample_rate;
        audio_stream.encoder_ctx->ch_layout = audio_stream.decoder_ctx->ch_layout;
        audio_stream.encoder_ctx->sample_fmt = encoder->sample_fmts[0];
        audio_stream.encoder_ctx->bit_rate = 128000; // 128 kbps
        audio_stream.encoder_ctx->time_base = (AVRational){1, audio_stream.encoder_ctx->sample_rate};
        
        if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            audio_stream.encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        if (avcodec_open2(audio_stream.encoder_ctx, encoder, nullptr) < 0) {
            std::cerr << "Failed to open audio encoder\n";
            return false;
        }
        
        if (avcodec_parameters_from_context(audio_stream.output_stream->codecpar,
                                           audio_stream.encoder_ctx) < 0) {
            std::cerr << "Failed to copy audio codec parameters\n";
            return false;
        }
        
        audio_stream.output_stream->time_base = audio_stream.encoder_ctx->time_base;
        
        return true;
    }
    
    bool writeHeader() {
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&output_ctx->pb, output_file.c_str(), AVIO_FLAG_WRITE) < 0) {
                std::cerr << "Could not open output file\n";
                return false;
            }
        }
        
        if (avformat_write_header(output_ctx, nullptr) < 0) {
            std::cerr << "Error occurred when writing header\n";
            return false;
        }
        
        return true;
    }
    
    bool transcodeVideo() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVFrame* scaled_frame = av_frame_alloc();
        
        scaled_frame->format = AV_PIX_FMT_YUV420P;
        scaled_frame->width = 1920;
        scaled_frame->height = 1080;
        av_frame_get_buffer(scaled_frame, 0);
        
        while (av_read_frame(input_ctx, packet) >= 0) {
            StreamContext* ctx = nullptr;
            
            if (packet->stream_index == video_stream.stream_index) {
                ctx = &video_stream;
            } else if (packet->stream_index == audio_stream.stream_index) {
                ctx = &audio_stream;
            } else {
                av_packet_unref(packet);
                continue;
            }
            
            // Decode
            int ret = avcodec_send_packet(ctx->decoder_ctx, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }
            
            while (ret >= 0) {
                ret = avcodec_receive_frame(ctx->decoder_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error during decoding\n";
                    av_packet_unref(packet);
                    av_frame_free(&frame);
                    av_frame_free(&scaled_frame);
                    av_packet_free(&packet);
                    return false;
                }
                
                // Process frame
                if (packet->stream_index == video_stream.stream_index) {
                    // Scale video frame to 1920x1080
                    sws_scale(sws_ctx, frame->data, frame->linesize, 0,
                             frame->height, scaled_frame->data, scaled_frame->linesize);
                    
                    scaled_frame->pts = frame->pts;
                    
                    // Encode scaled frame
                    ret = avcodec_send_frame(video_stream.encoder_ctx, scaled_frame);
                } else {
                    // Encode audio frame as-is
                    ret = avcodec_send_frame(ctx->encoder_ctx, frame);
                }
                
                if (ret < 0) {
                    std::cerr << "Error sending frame for encoding\n";
                    continue;
                }
                
                AVPacket* enc_packet = av_packet_alloc();
                while (ret >= 0) {
                    ret = avcodec_receive_packet(ctx->encoder_ctx, enc_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        std::cerr << "Error during encoding\n";
                        break;
                    }
                    
                    // Rescale timestamps
                    av_packet_rescale_ts(enc_packet, ctx->encoder_ctx->time_base,
                                        ctx->output_stream->time_base);
                    enc_packet->stream_index = ctx->output_stream->index;
                    
                    // Write packet
                    if (av_interleaved_write_frame(output_ctx, enc_packet) < 0) {
                        std::cerr << "Error writing frame\n";
                    }
                }
                av_packet_free(&enc_packet);
            }
            av_packet_unref(packet);
        }
        
        // Flush encoders
        flushEncoder(&video_stream);
        if (audio_stream.encoder_ctx) {
            flushEncoder(&audio_stream);
        }
        
        av_frame_free(&frame);
        av_frame_free(&scaled_frame);
        av_packet_free(&packet);
        
        return true;
    }
    
    void flushEncoder(StreamContext* ctx) {
        if (!ctx->encoder_ctx) return;
        
        avcodec_send_frame(ctx->encoder_ctx, nullptr);
        
        AVPacket* packet = av_packet_alloc();
        int ret;
        
        while ((ret = avcodec_receive_packet(ctx->encoder_ctx, packet)) == 0) {
            av_packet_rescale_ts(packet, ctx->encoder_ctx->time_base,
                                ctx->output_stream->time_base);
            packet->stream_index = ctx->output_stream->index;
            av_interleaved_write_frame(output_ctx, packet);
            av_packet_unref(packet);
        }
        
        av_packet_free(&packet);
    }
    
    bool writeTrailer() {
        av_write_trailer(output_ctx);
        return true;
    }
    
    void cleanup() {
        if (video_stream.decoder_ctx) {
            avcodec_free_context(&video_stream.decoder_ctx);
        }
        if (video_stream.encoder_ctx) {
            avcodec_free_context(&video_stream.encoder_ctx);
        }
        if (audio_stream.decoder_ctx) {
            avcodec_free_context(&audio_stream.decoder_ctx);
        }
        if (audio_stream.encoder_ctx) {
            avcodec_free_context(&audio_stream.encoder_ctx);
        }
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
        }
        if (input_ctx) {
            avformat_close_input(&input_ctx);
        }
        if (output_ctx) {
            if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&output_ctx->pb);
            }
            avformat_free_context(output_ctx);
        }
    }
};

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <input_file> <output_file>\n";
    std::cout << "Converts any video format to x264 Full HD (1920x1080)\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << program << " input.avi output.mp4\n";
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string input_file = argv[1];
    std::string output_file = argv[2];
    
    // Check if input file exists
    if (!fs::exists(input_file)) {
        std::cerr << "Error: Input file does not exist: " << input_file << "\n";
        return 1;
    }
    
    // Add .mp4 extension if no extension provided
    if (output_file.find('.') == std::string::npos) {
        output_file += ".mp4";
    }
    
    std::cout << "Converting: " << input_file << " -> " << output_file << "\n";
    std::cout << "Output: x264 Full HD (1920x1080)\n";
    
    VideoConverter converter(input_file, output_file);
    
    if (converter.convert()) {
        std::cout << "Conversion successful!\n";
        return 0;
    } else {
        std::cerr << "Conversion failed!\n";
        return 1;
    }
}