#pragma once
/* ffmpeg media player — software decode (portlibs ffmpeg) into a
   frame queue; audio resampled to 48kHz stereo int16 and pushed
   through an audout ring. The media clock is the audout played-sample
   counter — exactly what the TV hears, no drift. */
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>
#include <vector>

namespace uss {

// std::thread on libnx gets a small default stack; ffmpeg decode wants
// ~1MB. A pthread with an explicit stack size is the safe path.
struct BigThread {
    pthread_t t{};
    bool on = false;
    void start(void* (*fn)(void*), void* arg, size_t stack) {
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setstacksize(&a, stack);
        on = pthread_create(&t, &a, fn, arg) == 0;
        pthread_attr_destroy(&a);
    }
    void join() { if (on) { pthread_join(t, nullptr); on = false; } }
};

class Player {
public:
    ~Player() { close(); }
    bool open(const std::string& path);
    void close();

    // media clock in ms — audout played samples, or wall clock when the
    // file has no audio stream
    double mediaMs() const;
    bool finished() const { return done_ && audEmpty(); }
    bool hasVideo() const { return hasVideo_; }
    int  videoW() const { return vw_; }
    int  videoH() const { return vh_; }

    // latest decoded frame due at/before the clock (RGBx8); returns
    // false until the first frame. The returned pointer is stable —
    // it belongs to a display buffer the decode thread never touches.
    bool frame(uint8_t const** rgba, int* w, int* h);

private:
    void decodeLoop();
    void audioFeeder();
    bool audEmpty() const;

    std::atomic<bool> quit_{false}, done_{false};
    bool hasVideo_ = false, hasAudio_ = false;
    std::string path_;
    int vw_ = 0, vh_ = 0;
    int64_t wall0_ = 0;                    // monotonic start (ns)

    // decoded video: decode fills vback_, publishes by swapping into
    // vfront_ under vmu_; frame() swaps vfront_ into vshow_ — the buffer
    // it returns — so the render thread always reads memory the decode
    // thread can no longer free out from under it.
    std::mutex vmu_;
    std::vector<uint8_t> vback_, vfront_, vshow_;   // rgba
    int64_t vpts_ = -1;                    // ms

    // decoded PCM queue (48k stereo s16) -> audout
    std::mutex amu_;
    std::condition_variable acv_;
    std::deque<int16_t> aq_;
    BigThread tDec_, tAud_;
    bool audioOn_ = false;
};

} // namespace uss
