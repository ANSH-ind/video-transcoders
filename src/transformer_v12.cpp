#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <thread>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <map>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavutil/channel_layout.h>
}

enum class LogLevel { INFO, WARNING, ERROR, SUCCESS };

class Logger {
private:
    static std::mutex log_mutex;

    static std::string current_time() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    static std::string level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::SUCCESS: return "SUCCESS";
            default: return "UNKNOWN";
        }
    }

public:
    static void log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::ostream& out = (level == LogLevel::ERROR) ? std::cerr : std::cout;
        out << "[" << current_time() << "] [" << level_to_string(level) << "] " << message << "\n";
    }
};

std::mutex Logger::log_mutex; 

struct FFmpegDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            if (ctx->iformat) {
                avformat_close_input(&ctx);
            } else {
                if (!(ctx->oformat->flags & AVFMT_NOFILE) && ctx->pb) {
                    avio_closep(&ctx->pb);
                }
                avformat_free_context(ctx);
            }
        }
    }
    void operator()(AVCodecContext* ctx) const { if (ctx) avcodec_free_context(&ctx); }
    void operator()(AVPacket* pkt) const { if (pkt) av_packet_free(&pkt); }
    void operator()(AVFrame* frame) const { if (frame) av_frame_free(&frame); }
    void operator()(SwsContext* ctx) const { if (ctx) sws_freeContext(ctx); }
    void operator()(AVFilterGraph* graph) const { if (graph) avfilter_graph_free(&graph); }
    void operator()(AVAudioFifo* fifo) const { if (fifo) av_audio_fifo_free(fifo); }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FFmpegDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, FFmpegDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, FFmpegDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FFmpegDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, FFmpegDeleter>;
using FilterGraphPtr = std::unique_ptr<AVFilterGraph, FFmpegDeleter>;
using AudioFifoPtr = std::unique_ptr<AVAudioFifo, FFmpegDeleter>;

bool process_encoder(AVCodecContext* enc_ctx, AVFormatContext* out_ctx, AVStream* out_stream, AVFrame* frame, AVPacket* pkt) {
    int ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) return false;

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        else if (ret < 0) return false;

        av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
        pkt->stream_index = out_stream->index;

        if (av_interleaved_write_frame(out_ctx, pkt) < 0) return false;
        av_packet_unref(pkt);
    }
    return true;
}

void create_rendition(std::string input_file, std::string output_file, int width, int height, int bitrate, std::string keyinfo) {
    AVFormatContext* raw_fmt_ctx = nullptr;
    if (avformat_open_input(&raw_fmt_ctx, input_file.c_str(), nullptr, nullptr) < 0) return;
    FormatContextPtr in_ctx(raw_fmt_ctx);

    if (avformat_find_stream_info(in_ctx.get(), nullptr) < 0) return;

    AVCodec* v_dec = nullptr;
    AVCodec* a_dec = nullptr;
    const int v_idx = av_find_best_stream(in_ctx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &v_dec, 0);
    const int a_idx = av_find_best_stream(in_ctx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &a_dec, 0);

    if (v_idx < 0 || a_idx < 0) return;

    AVStream* v_in_stream = in_ctx->streams[v_idx];
    AVStream* a_in_stream = in_ctx->streams[a_idx];

    CodecContextPtr v_dec_ctx(avcodec_alloc_context3(v_dec));
    if (avcodec_parameters_to_context(v_dec_ctx.get(), v_in_stream->codecpar) < 0 || avcodec_open2(v_dec_ctx.get(), v_dec, nullptr) < 0) return;

    CodecContextPtr a_dec_ctx(avcodec_alloc_context3(a_dec));
    if (avcodec_parameters_to_context(a_dec_ctx.get(), a_in_stream->codecpar) < 0 || avcodec_open2(a_dec_ctx.get(), a_dec, nullptr) < 0) return;

    const AVCodec* v_enc = avcodec_find_encoder(AV_CODEC_ID_H264);
    const AVCodec* a_enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!v_enc || !a_enc) return;

    CodecContextPtr v_enc_ctx(avcodec_alloc_context3(v_enc));
    v_enc_ctx->width = width;
    v_enc_ctx->height = height;
    v_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    v_enc_ctx->time_base = AVRational{1, 30};
    v_enc_ctx->framerate = AVRational{30, 1};
    v_enc_ctx->bit_rate = bitrate;
    v_enc_ctx->thread_count = 0;
    v_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set(v_enc_ctx->priv_data, "preset", "fast", 0);
    v_enc_ctx->gop_size = 60;
    v_enc_ctx->keyint_min = 60;
    av_opt_set(v_enc_ctx->priv_data, "sc_threshold", "0", 0);

    if (avcodec_open2(v_enc_ctx.get(), v_enc, nullptr) < 0) return;

    CodecContextPtr a_enc_ctx(avcodec_alloc_context3(a_enc));
    a_enc_ctx->sample_rate = 44100;
    a_enc_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    a_enc_ctx->channels = 2;
    a_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    a_enc_ctx->bit_rate = 128000;
    a_enc_ctx->time_base = AVRational{1, a_enc_ctx->sample_rate};
    a_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(a_enc_ctx.get(), a_enc, nullptr) < 0) return;

    AVFormatContext* raw_out_ctx = nullptr;
    if (avformat_alloc_output_context2(&raw_out_ctx, nullptr, "hls", output_file.c_str()) < 0) return;
    FormatContextPtr out_ctx(raw_out_ctx);

    AVStream* v_out_stream = avformat_new_stream(out_ctx.get(), v_enc);
    if (avcodec_parameters_from_context(v_out_stream->codecpar, v_enc_ctx.get()) < 0) return;
    v_out_stream->time_base = v_enc_ctx->time_base;

    AVStream* a_out_stream = avformat_new_stream(out_ctx.get(), a_enc);
    if (avcodec_parameters_from_context(a_out_stream->codecpar, a_enc_ctx.get()) < 0) return;
    a_out_stream->time_base = a_enc_ctx->time_base;

    std::string prefix = output_file.substr(0, output_file.find_last_of('.'));
    std::string segment_filename = prefix + "_%03d.ts";

    av_opt_set(out_ctx->priv_data, "hls_time", "6", 0);
    av_opt_set(out_ctx->priv_data, "hls_segment_filename", segment_filename.c_str(), 0);
    av_opt_set(out_ctx->priv_data, "hls_list_size", "0", 0);
    av_opt_set(out_ctx->priv_data, "hls_key_info_file", keyinfo.c_str(), 0);

    if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_ctx->pb, output_file.c_str(), AVIO_FLAG_WRITE) < 0) return;
    }

    if (avformat_write_header(out_ctx.get(), nullptr) < 0) return;

    SwsContextPtr sws(sws_getContext(
        v_dec_ctx->width, v_dec_ctx->height, v_dec_ctx->pix_fmt,
        v_enc_ctx->width, v_enc_ctx->height, v_enc_ctx->pix_fmt,
        SWS_BICUBIC, nullptr, nullptr, nullptr));

    FilterGraphPtr filter_graph(avfilter_graph_alloc());
    const AVFilter* abuffersrc = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    AVFilterContext* buffersrc_ctx = nullptr;
    AVFilterContext* buffersink_ctx = nullptr;

    char ch_layout_str[64] = {0};
    av_get_channel_layout_string(ch_layout_str, sizeof(ch_layout_str), a_dec_ctx->channels, a_dec_ctx->channel_layout);
    if (ch_layout_str[0] == '\0') snprintf(ch_layout_str, sizeof(ch_layout_str), "stereo");

    char args[512];
    snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
             a_in_stream->time_base.num, a_in_stream->time_base.den, a_dec_ctx->sample_rate,
             av_get_sample_fmt_name(a_dec_ctx->sample_fmt), ch_layout_str);

    if (avfilter_graph_create_filter(&buffersrc_ctx, abuffersrc, "in", args, nullptr, filter_graph.get()) < 0) return;
    if (avfilter_graph_create_filter(&buffersink_ctx, abuffersink, "out", nullptr, nullptr, filter_graph.get()) < 0) return;

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    if (avfilter_graph_parse_ptr(filter_graph.get(), "loudnorm=I=-14:LRA=11:TP=-1.5,aformat=sample_fmts=fltp:sample_rates=44100:channel_layouts=stereo", &inputs, &outputs, nullptr) < 0) return;
    if (avfilter_graph_config(filter_graph.get(), nullptr) < 0) return;

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    AudioFifoPtr fifo(av_audio_fifo_alloc(a_enc_ctx->sample_fmt, a_enc_ctx->channels, 1));
    PacketPtr in_pkt(av_packet_alloc());
    PacketPtr out_pkt(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    FramePtr scale_frame(av_frame_alloc());

    scale_frame->format = v_enc_ctx->pix_fmt;
    scale_frame->width = v_enc_ctx->width;
    scale_frame->height = v_enc_ctx->height;
    if (av_frame_get_buffer(scale_frame.get(), 32) < 0) return;

    int64_t audio_pts = 0;

    while (av_read_frame(in_ctx.get(), in_pkt.get()) >= 0) {
        if (in_pkt->stream_index == v_idx) {
            if (avcodec_send_packet(v_dec_ctx.get(), in_pkt.get()) >= 0) {
                while (avcodec_receive_frame(v_dec_ctx.get(), frame.get()) >= 0) {
                    sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, scale_frame->data, scale_frame->linesize);
                    if (frame->pts != AV_NOPTS_VALUE) scale_frame->pts = av_rescale_q(frame->pts, v_in_stream->time_base, v_enc_ctx->time_base);
                    else scale_frame->pts = AV_NOPTS_VALUE;
                    if (!process_encoder(v_enc_ctx.get(), out_ctx.get(), v_out_stream, scale_frame.get(), out_pkt.get())) return;
                    av_frame_unref(frame.get());
                }
            }
        } else if (in_pkt->stream_index == a_idx) {
            if (avcodec_send_packet(a_dec_ctx.get(), in_pkt.get()) >= 0) {
                while (avcodec_receive_frame(a_dec_ctx.get(), frame.get()) >= 0) {
                    if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame.get(), 0) < 0) return;
                    while (true) {
                        FramePtr filt_frame(av_frame_alloc());
                        int ret = av_buffersink_get_frame(buffersink_ctx, filt_frame.get());
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                        if (ret < 0) return;

                        if (av_audio_fifo_realloc(fifo.get(), av_audio_fifo_size(fifo.get()) + filt_frame->nb_samples) < 0) return;
                        if (av_audio_fifo_write(fifo.get(), (void**)filt_frame->data, filt_frame->nb_samples) < filt_frame->nb_samples) return;

                        while (av_audio_fifo_size(fifo.get()) >= a_enc_ctx->frame_size) {
                            FramePtr a_out_frame(av_frame_alloc());
                            a_out_frame->nb_samples = a_enc_ctx->frame_size;
                            a_out_frame->channel_layout = a_enc_ctx->channel_layout;
                            a_out_frame->channels = a_enc_ctx->channels;
                            a_out_frame->format = a_enc_ctx->sample_fmt;
                            a_out_frame->sample_rate = a_enc_ctx->sample_rate;
                            av_frame_get_buffer(a_out_frame.get(), 0);

                            if (av_audio_fifo_read(fifo.get(), (void**)a_out_frame->data, a_enc_ctx->frame_size) < a_enc_ctx->frame_size) return;
                            a_out_frame->pts = audio_pts;
                            audio_pts += a_out_frame->nb_samples;

                            if (!process_encoder(a_enc_ctx.get(), out_ctx.get(), a_out_stream, a_out_frame.get(), out_pkt.get())) return;
                        }
                    }
                    av_frame_unref(frame.get());
                }
            }
        }
        av_packet_unref(in_pkt.get());
    }

    avcodec_send_packet(v_dec_ctx.get(), nullptr);
    while (avcodec_receive_frame(v_dec_ctx.get(), frame.get()) >= 0) {
        sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, scale_frame->data, scale_frame->linesize);
        if (frame->pts != AV_NOPTS_VALUE) scale_frame->pts = av_rescale_q(frame->pts, v_in_stream->time_base, v_enc_ctx->time_base);
        process_encoder(v_enc_ctx.get(), out_ctx.get(), v_out_stream, scale_frame.get(), out_pkt.get());
        av_frame_unref(frame.get());
    }
    process_encoder(v_enc_ctx.get(), out_ctx.get(), v_out_stream, nullptr, out_pkt.get());

    avcodec_send_packet(a_dec_ctx.get(), nullptr);
    while (avcodec_receive_frame(a_dec_ctx.get(), frame.get()) >= 0) {
        if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame.get(), 0) >= 0) {
            while (true) {
                FramePtr filt_frame(av_frame_alloc());
                int ret = av_buffersink_get_frame(buffersink_ctx, filt_frame.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) return;
                if (av_audio_fifo_realloc(fifo.get(), av_audio_fifo_size(fifo.get()) + filt_frame->nb_samples) >= 0) {
                    av_audio_fifo_write(fifo.get(), (void**)filt_frame->data, filt_frame->nb_samples);
                }
            }
        }
        av_frame_unref(frame.get());
    }

    if (av_buffersrc_add_frame_flags(buffersrc_ctx, nullptr, 0) < 0) return;
    
    while (true) {
        FramePtr filt_frame(av_frame_alloc());
        int ret = av_buffersink_get_frame(buffersink_ctx, filt_frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return;
        if (av_audio_fifo_realloc(fifo.get(), av_audio_fifo_size(fifo.get()) + filt_frame->nb_samples) >= 0) {
            av_audio_fifo_write(fifo.get(), (void**)filt_frame->data, filt_frame->nb_samples);
        }
    }

    while (av_audio_fifo_size(fifo.get()) > 0) {
        int out_samples = FFMIN(av_audio_fifo_size(fifo.get()), a_enc_ctx->frame_size);
        FramePtr a_out_frame(av_frame_alloc());
        a_out_frame->nb_samples = out_samples;
        a_out_frame->channel_layout = a_enc_ctx->channel_layout;
        a_out_frame->channels = a_enc_ctx->channels;
        a_out_frame->format = a_enc_ctx->sample_fmt;
        a_out_frame->sample_rate = a_enc_ctx->sample_rate;
        av_frame_get_buffer(a_out_frame.get(), 0);

        av_audio_fifo_read(fifo.get(), (void**)a_out_frame->data, out_samples);
        a_out_frame->pts = audio_pts;
        audio_pts += a_out_frame->nb_samples;
        process_encoder(a_enc_ctx.get(), out_ctx.get(), a_out_stream, a_out_frame.get(), out_pkt.get());
    }
    process_encoder(a_enc_ctx.get(), out_ctx.get(), a_out_stream, nullptr, out_pkt.get());

    av_write_trailer(out_ctx.get());
}

struct PlaylistEntry {
    std::string name;
    int width;
    int height;
    int bitrate;
};

void generate_master_playlist(const std::string& filename, const std::vector<PlaylistEntry>& entries) {
    std::ofstream master(filename);
    master << "#EXTM3U\n";
    master << "#EXT-X-VERSION:3\n"; 
    
    for (const auto& entry : entries) {
        master << "#EXT-X-STREAM-INF:BANDWIDTH=" << entry.bitrate << ",RESOLUTION=" << entry.width << "x" << entry.height << "\n";
        master << entry.name << ".m3u8\n";
    }
    
    master.close();
}

void hls_encoder(std::string input, std::string output, std::vector<std::string> resolution, std::vector<int> bitrate, std::string keyinfo) {
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, input.c_str(), nullptr, nullptr) < 0) return;
    avformat_find_stream_info(fmt_ctx, nullptr);
    
    AVCodec* decoder = nullptr;
    int v_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (v_idx < 0) {
        avformat_close_input(&fmt_ctx);
        return;
    }

    int original_width = fmt_ctx->streams[v_idx]->codecpar->width;
    int original_height = fmt_ctx->streams[v_idx]->codecpar->height;
    avformat_close_input(&fmt_ctx); 

    bool is_vertical = original_height > original_width;

    std::map<std::string, int> default_bitrates = {
        {"1080p", 5000000}, {"720p", 2500000}, {"480p", 1000000}, {"360p", 400000}
    };

    while (bitrate.size() < resolution.size()) {
        std::string current_res = resolution[bitrate.size()];
        if (default_bitrates.count(current_res)) {
            bitrate.push_back(default_bitrates[current_res]);
        } else {
            bitrate.push_back(1000000); 
        }
    }

    std::vector<std::thread> threads;
    std::vector<PlaylistEntry> entries;

    for (size_t i = 0; i < resolution.size(); ++i) {
        std::string res_name = resolution[i];
        int target_w = 0, target_h = 0;
        
        if (res_name == "1080p") { target_w = 1920; target_h = 1080; }
        else if (res_name == "720p") { target_w = 1280; target_h = 720; }
        else if (res_name == "480p") { target_w = 854; target_h = 480; }
        else if (res_name == "360p") { target_w = 640; target_h = 360; }
        else continue;

        if (is_vertical) std::swap(target_w, target_h);

        std::string out_m3u8 = res_name + ".m3u8";
        entries.push_back({res_name, target_w, target_h, bitrate[i]});
        threads.emplace_back(create_rendition, input, out_m3u8, target_w, target_h, bitrate[i], keyinfo);
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    generate_master_playlist(output, entries);
}
