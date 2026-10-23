#include "player.h"
#include <switch.h>
#include <cstring>
#include <malloc.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

namespace uss {

static int64_t mono_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

bool Player::open(const std::string& path) {
    quit_ = false; done_ = false;
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    hasVideo_ = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1,
                                    nullptr, 0) >= 0;
    hasAudio_ = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1,
                                    nullptr, 0) >= 0;
    avformat_close_input(&fmt);
    if (!hasVideo_ && !hasAudio_) return false;

    path_ = path;
    wall0_ = mono_ns();
    tDec_.start([](void* p) -> void* {
        ((Player*)p)->decodeLoop(); return nullptr; }, this, 1 << 20);
    return true;
}

void Player::close() {
    quit_ = true;
    acv_.notify_all();
    tDec_.join();
    tAud_.join();
    if (audioOn_) { audoutStopAudioOut(); audoutExit(); audioOn_ = false; }
}

bool Player::audEmpty() const {
    std::lock_guard<std::mutex> l(*(std::mutex*)&amu_);
    return aq_.empty();
}

double Player::mediaMs() const {
    if (hasAudio_ && audioOn_) {
        u64 n = 0;
        audoutGetAudioOutPlayedSampleCount(&n);
        return n / 48.0;                 // 48000 samples/s -> ms
    }
    return (mono_ns() - wall0_) / 1e6;   // wall clock for audio-less files
}

bool Player::frame(uint8_t const** rgba, int* w, int* h) {
    {
        std::lock_guard<std::mutex> l(vmu_);
        if (!vfront_.empty() && vpts_ >= 0 && vpts_ <= mediaMs() + 150)
            vshow_.swap(vfront_);        // new frame due — take ownership;
    }                                    // vshow_ is ours to draw from
    if (vshow_.empty()) return false;    // hold the last frame otherwise
    *rgba = vshow_.data(); *w = vw_; *h = vh_;
    return true;
}

// ---------------- audio out ring ----------------

void Player::audioFeeder() {
    const int CHUNK = 48000 / 20;          // 50ms of stereo int16
    const int NBUF = 4;
    AudioOutBuffer buf[NBUF];
    u8* mem[NBUF];
    for (int i = 0; i < NBUF; ++i) {
        u32 ds = CHUNK * 2 * 2;
        mem[i] = (u8*)memalign(0x1000, (ds + 0xfff) & ~0xfff);
        memset(mem[i], 0, (ds + 0xfff) & ~0xfff);
        memset(&buf[i], 0, sizeof(buf[i]));
        buf[i].buffer = mem[i];
        buf[i].buffer_size = (ds + 0xfff) & ~0xfff;
        buf[i].data_size = 0;
        buf[i].data_offset = 0;
    }

    while (!quit_) {
        // fill + queue every released/dry buffer
        AudioOutBuffer* rel = nullptr;
        u32 relN = 0;
        audoutGetReleasedAudioOutBuffer(&rel, &relN);
        for (u32 i = 0; i < relN; ++i) {
            AudioOutBuffer* b = rel + i;
            std::unique_lock<std::mutex> l(amu_);
            size_t need = CHUNK * 2;
            size_t have = std::min(need, aq_.size());
            int16_t* dst = (int16_t*)b->buffer;
            for (size_t k = 0; k < have; ++k) { dst[k] = aq_.front(); aq_.pop_front(); }
            for (size_t k = have; k < need; ++k) dst[k] = 0;
            b->data_size = need * 2;     // underrun -> silence, not a
                                         // zero-length buffer
            l.unlock();
            audoutAppendAudioOutBuffer(b);
        }
        // wait for any buffer to release or brief sleep
        svcSleepThread(3e6);               // ~3ms
    }
    for (int i = 0; i < NBUF; ++i) free(mem[i]);
}

// ---------------- decode loop ----------------

void Player::decodeLoop() {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path_.c_str(), nullptr, nullptr) < 0)
        { done_ = true; return; }
    avformat_find_stream_info(fmt, nullptr);

    int vidx = -1, aidx = -1;
    AVCodecContext* vctx = nullptr;
    AVCodecContext* actx = nullptr;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;

    if (hasVideo_) {
        vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1,
                                   nullptr, 0);
        const AVCodec* cd = avcodec_find_decoder(
            fmt->streams[vidx]->codecpar->codec_id);
        if (cd) {
            vctx = avcodec_alloc_context3(cd);
            avcodec_parameters_to_context(vctx, fmt->streams[vidx]->codecpar);
            vctx->thread_count = 4;
            if (avcodec_open2(vctx, cd, nullptr) < 0) { vctx = nullptr; }
        }
        if (!vctx) hasVideo_ = false;
    }
    if (hasAudio_) {
        aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1,
                                   nullptr, 0);
        const AVCodec* cd = avcodec_find_decoder(
            fmt->streams[aidx]->codecpar->codec_id);
        if (cd) {
            actx = avcodec_alloc_context3(cd);
            avcodec_parameters_to_context(actx, fmt->streams[aidx]->codecpar);
            if (avcodec_open2(actx, cd, nullptr) < 0) actx = nullptr;
        }
        if (actx) {
            AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
            swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_S16, 48000,
                                &actx->ch_layout, actx->sample_fmt,
                                actx->sample_rate, 0, nullptr);
            if (!swr || swr_init(swr) < 0) { swr_free(&swr); actx = nullptr; }
            else {
                // start audout ring only once we can actually decode audio
                if (audoutInitialize() == 0) {
                    audoutStartAudioOut();
                    audioOn_ = true;
                    tAud_.start([](void* p) -> void* {
                        ((Player*)p)->audioFeeder();
                        return nullptr; }, this, 256 << 10);
                }
            }
        }
        if (!actx) hasAudio_ = false;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    while (!quit_ && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vidx && vctx) {
            if (avcodec_send_packet(vctx, pkt) >= 0) {
                while (avcodec_receive_frame(vctx, fr) >= 0) {
                    // scale to fit 1280x720 preserving aspect
                    double sc = std::min(1280.0 / fr->width,
                                         720.0 / fr->height);
                    int dw = fr->width * sc, dh = fr->height * sc;
                    sws = sws_getCachedContext(sws,
                        fr->width, fr->height, (AVPixelFormat)fr->format,
                        dw, dh, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                        nullptr, nullptr);
                    vback_.resize((size_t)dw * dh * 4);
                    uint8_t* dst[4] = {vback_.data(), nullptr, nullptr, nullptr};
                    int stride[4] = {dw * 4, 0, 0, 0};
                    sws_scale(sws, fr->data, fr->linesize, 0, fr->height,
                              dst, stride);
                    int64_t pts = fr->best_effort_timestamp == AV_NOPTS_VALUE
                        ? -1 : (int64_t)(fr->best_effort_timestamp
                             * av_q2d(fmt->streams[vidx]->time_base) * 1000);
                    {
                        std::lock_guard<std::mutex> l(vmu_);
                        vfront_.swap(vback_);          // publish
                        vw_ = dw; vh_ = dh;
                        if (pts >= 0) vpts_ = pts;
                    }
                    // coarse A/V sync — OUTSIDE the lock so frame()
                    // never blocks on a decode-side nap
                    while (!quit_ && vpts_ > mediaMs() + 500)
                        svcSleepThread(10e6);
                }
            }
        } else if (pkt->stream_index == aidx && actx) {
            if (avcodec_send_packet(actx, pkt) >= 0) {
                while (avcodec_receive_frame(actx, fr) >= 0) {
                    int outN = (int)av_rescale_rnd(
                        fr->nb_samples, 48000, actx->sample_rate,
                        AV_ROUND_UP);
                    std::vector<int16_t> tmp(outN * 2);
                    uint8_t* out[1] = {(uint8_t*)tmp.data()};
                    int got = swr_convert(swr, out, outN,
                        (const uint8_t**)fr->data, fr->nb_samples);
                    std::lock_guard<std::mutex> l(amu_);
                    aq_.insert(aq_.end(), tmp.begin(), tmp.begin() + got * 2);
                    // backpressure: cap queue at ~1s
                    while (aq_.size() > 48000 * 2)
                        aq_.erase(aq_.begin(), aq_.begin() + 48000 * 2);
                }
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    av_frame_free(&fr);
    if (sws) sws_freeContext(sws);
    if (swr) swr_free(&swr);
    if (vctx) avcodec_free_context(&vctx);
    if (actx) avcodec_free_context(&actx);
    avformat_close_input(&fmt);
    done_ = true;
}

} // namespace uss
