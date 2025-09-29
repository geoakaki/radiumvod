#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <cstdint>

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

class VideoConverter {
private:
    std::string input_file;
    std::string output_file;
    AVFormatContext* input_ctx = nullptr;
    AVFormatContext* output_ctx = nullptr;
    
    struct StreamContext {
        AVCodecContext* decoder_ctx = nullptr;
        AVCodecContext* encoder_ctx = nullptr;
        AVStream* input_stream = nullptr;
        AVStream* output_stream = nullptr;
        int stream_index = -1;
        int64_t next_pts = 0;
        int64_t frame_count = 0;
    };
    
    StreamContext video_stream;
    StreamContext audio_stream;
    SwsContext* sws_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    
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
        
        if (audio_stream.stream_index >= 0 && !setupAudioEncoder()) {
            std::cerr << "Warning: Failed to setup audio encoder, continuing without audio\n";
            audio_stream.stream_index = -1;
        }
        
        if (!writeHeader()) {
            std::cerr << "Failed to write header\n";
            return false;
        }
        
        if (!transcodeStreams()) {
            std::cerr << "Failed to transcode streams\n";
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
                video_stream.input_stream = stream;
                if (!setupDecoder(stream, video_stream)) {
                    return false;
                }
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream.stream_index < 0) {
                audio_stream.stream_index = i;
                audio_stream.input_stream = stream;
                if (!setupDecoder(stream, audio_stream)) {
                    // Continue without audio if decoder fails
                    audio_stream.stream_index = -1;
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
        
        ctx.decoder_ctx->time_base = stream->time_base;
        
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
        video_stream.encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        video_stream.encoder_ctx->bit_rate = 4000000; // 4 Mbps
        video_stream.encoder_ctx->gop_size = 250;
        video_stream.encoder_ctx->max_b_frames = 2;
        
        // Set framerate and timebase
        AVRational input_framerate = av_guess_frame_rate(input_ctx, video_stream.input_stream, nullptr);
        if (input_framerate.num == 0 || input_framerate.den == 0) {
            input_framerate = AVRational{25, 1}; // Default to 25 fps
        }
        
        video_stream.encoder_ctx->framerate = input_framerate;
        video_stream.encoder_ctx->time_base = av_inv_q(input_framerate);
        video_stream.output_stream->time_base = video_stream.encoder_ctx->time_base;
        
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
            return true; // No audio stream
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
        
        // Setup audio parameters
        audio_stream.encoder_ctx->sample_rate = audio_stream.decoder_ctx->sample_rate;
        audio_stream.encoder_ctx->ch_layout = audio_stream.decoder_ctx->ch_layout;
        audio_stream.encoder_ctx->sample_fmt = encoder->sample_fmts[0];
        audio_stream.encoder_ctx->bit_rate = 128000; // 128 kbps
        audio_stream.encoder_ctx->time_base = AVRational{1, audio_stream.encoder_ctx->sample_rate};
        
        audio_stream.output_stream->time_base = audio_stream.encoder_ctx->time_base;
        
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
        
        // Setup audio resampler if needed
        if (audio_stream.decoder_ctx->sample_fmt != audio_stream.encoder_ctx->sample_fmt ||
            audio_stream.decoder_ctx->sample_rate != audio_stream.encoder_ctx->sample_rate ||
            av_channel_layout_compare(&audio_stream.decoder_ctx->ch_layout, 
                                     &audio_stream.encoder_ctx->ch_layout) != 0) {
            
            swr_ctx = swr_alloc();
            if (!swr_ctx) {
                std::cerr << "Failed to allocate resampler\n";
                return false;
            }
            
            av_opt_set_chlayout(swr_ctx, "in_chlayout", &audio_stream.decoder_ctx->ch_layout, 0);
            av_opt_set_int(swr_ctx, "in_sample_rate", audio_stream.decoder_ctx->sample_rate, 0);
            av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", audio_stream.decoder_ctx->sample_fmt, 0);
            
            av_opt_set_chlayout(swr_ctx, "out_chlayout", &audio_stream.encoder_ctx->ch_layout, 0);
            av_opt_set_int(swr_ctx, "out_sample_rate", audio_stream.encoder_ctx->sample_rate, 0);
            av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", audio_stream.encoder_ctx->sample_fmt, 0);
            
            if (swr_init(swr_ctx) < 0) {
                std::cerr << "Failed to initialize resampler\n";
                return false;
            }
        }
        
        return true;
    }
    
    bool writeHeader() {
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&output_ctx->pb, output_file.c_str(), AVIO_FLAG_WRITE) < 0) {
                std::cerr << "Could not open output file\n";
                return false;
            }
        }
        
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        
        int ret = avformat_write_header(output_ctx, &opts);
        av_dict_free(&opts);
        
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Error occurred when writing header: " << errbuf << "\n";
            return false;
        }
        
        return true;
    }
    
    bool transcodeStreams() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVFrame* scaled_frame = nullptr;
        AVFrame* resampled_frame = nullptr;
        
        // Allocate scaled frame for video
        if (video_stream.stream_index >= 0) {
            scaled_frame = av_frame_alloc();
            scaled_frame->format = AV_PIX_FMT_YUV420P;
            scaled_frame->width = 1920;
            scaled_frame->height = 1080;
            av_frame_get_buffer(scaled_frame, 0);
        }
        
        // Allocate resampled frame for audio if needed
        if (audio_stream.stream_index >= 0 && swr_ctx) {
            resampled_frame = av_frame_alloc();
        }
        
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
            
            // Send packet to decoder
            int ret = avcodec_send_packet(ctx->decoder_ctx, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }
            
            // Receive frames from decoder
            while (ret >= 0) {
                ret = avcodec_receive_frame(ctx->decoder_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error during decoding\n";
                    break;
                }
                
                if (packet->stream_index == video_stream.stream_index) {
                    // Process video frame
                    if (!processVideoFrame(frame, scaled_frame)) {
                        std::cerr << "Failed to process video frame\n";
                    }
                } else if (packet->stream_index == audio_stream.stream_index) {
                    // Process audio frame
                    if (!processAudioFrame(frame, resampled_frame)) {
                        std::cerr << "Failed to process audio frame\n";
                    }
                }
            }
            
            av_packet_unref(packet);
        }
        
        // Flush decoders and encoders
        flushDecoder(&video_stream, scaled_frame);
        if (audio_stream.stream_index >= 0) {
            flushDecoder(&audio_stream, resampled_frame);
        }
        
        flushEncoder(&video_stream);
        if (audio_stream.stream_index >= 0) {
            flushEncoder(&audio_stream);
        }
        
        // Cleanup
        av_frame_free(&frame);
        if (scaled_frame) av_frame_free(&scaled_frame);
        if (resampled_frame) av_frame_free(&resampled_frame);
        av_packet_free(&packet);
        
        return true;
    }
    
    bool processVideoFrame(AVFrame* input_frame, AVFrame* output_frame) {
        // Scale the frame
        sws_scale(sws_ctx, input_frame->data, input_frame->linesize, 0,
                 input_frame->height, output_frame->data, output_frame->linesize);
        
        // Set proper PTS for the frame
        output_frame->pts = video_stream.next_pts;
        video_stream.next_pts++;
        
        // Send frame to encoder
        int ret = avcodec_send_frame(video_stream.encoder_ctx, output_frame);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Error sending video frame: " << errbuf << "\n";
            return false;
        }
        
        // Receive packets from encoder
        return receiveAndWritePackets(&video_stream);
    }
    
    bool processAudioFrame(AVFrame* input_frame, AVFrame* output_frame) {
        AVFrame* frame_to_encode = input_frame;
        
        // Resample if needed
        if (swr_ctx) {
            output_frame->nb_samples = av_rescale_rnd(
                swr_get_delay(swr_ctx, audio_stream.decoder_ctx->sample_rate) + input_frame->nb_samples,
                audio_stream.encoder_ctx->sample_rate,
                audio_stream.decoder_ctx->sample_rate,
                AV_ROUND_UP
            );
            
            output_frame->ch_layout = audio_stream.encoder_ctx->ch_layout;
            output_frame->format = audio_stream.encoder_ctx->sample_fmt;
            output_frame->sample_rate = audio_stream.encoder_ctx->sample_rate;
            
            av_frame_get_buffer(output_frame, 0);
            
            int ret = swr_convert(swr_ctx,
                                output_frame->data, output_frame->nb_samples,
                                (const uint8_t**)input_frame->data, input_frame->nb_samples);
            
            if (ret < 0) {
                std::cerr << "Error resampling audio\n";
                return false;
            }
            
            output_frame->pts = audio_stream.next_pts;
            audio_stream.next_pts += output_frame->nb_samples;
            
            frame_to_encode = output_frame;
        } else {
            // Set PTS for non-resampled frame
            input_frame->pts = audio_stream.next_pts;
            audio_stream.next_pts += input_frame->nb_samples;
        }
        
        // Send frame to encoder
        int ret = avcodec_send_frame(audio_stream.encoder_ctx, frame_to_encode);
        if (ret < 0) {
            std::cerr << "Error sending audio frame for encoding\n";
            return false;
        }
        
        // Receive packets from encoder
        return receiveAndWritePackets(&audio_stream);
    }
    
    bool receiveAndWritePackets(StreamContext* ctx) {
        AVPacket* packet = av_packet_alloc();
        int ret;
        
        while (true) {
            ret = avcodec_receive_packet(ctx->encoder_ctx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                std::cerr << "Error receiving packet from encoder\n";
                av_packet_free(&packet);
                return false;
            }
            
            // Set stream index
            packet->stream_index = ctx->output_stream->index;
            
            // Rescale timestamps
            av_packet_rescale_ts(packet, ctx->encoder_ctx->time_base, ctx->output_stream->time_base);
            
            // Write packet
            ret = av_interleaved_write_frame(output_ctx, packet);
            if (ret < 0) {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                std::cerr << "Error writing packet: " << errbuf << "\n";
            }
            
            av_packet_unref(packet);
        }
        
        av_packet_free(&packet);
        return true;
    }
    
    void flushDecoder(StreamContext* ctx, AVFrame* output_frame) {
        if (!ctx->decoder_ctx) return;
        
        // Send flush signal to decoder
        avcodec_send_packet(ctx->decoder_ctx, nullptr);
        
        AVFrame* frame = av_frame_alloc();
        int ret;
        
        while ((ret = avcodec_receive_frame(ctx->decoder_ctx, frame)) == 0) {
            if (ctx == &video_stream && output_frame) {
                processVideoFrame(frame, output_frame);
            } else if (ctx == &audio_stream && output_frame) {
                processAudioFrame(frame, output_frame);
            }
        }
        
        av_frame_free(&frame);
    }
    
    void flushEncoder(StreamContext* ctx) {
        if (!ctx->encoder_ctx) return;
        
        // Send flush signal to encoder
        avcodec_send_frame(ctx->encoder_ctx, nullptr);
        
        // Receive remaining packets
        receiveAndWritePackets(ctx);
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
        if (swr_ctx) {
            swr_free(&swr_ctx);
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