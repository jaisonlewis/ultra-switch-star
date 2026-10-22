#include "pitch.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace uss {

static constexpr double BASE_TONE_FREQ = 65.4064;   // C2
static constexpr double HALFTONE_BASE = 1.0594630943592953; // 2^(1/12)
static constexpr float THRESHOLDS[4] = {0.05f, 0.10f, 0.15f, 0.20f};

double PitchDetector::toneFreq(int toneAbs) {
    return BASE_TONE_FREQ * std::pow(HALFTONE_BASE, toneAbs);
}

int PitchDetector::freqToToneAbs(double freq) {
    if (freq <= 0) return -1;
    int best = -1; double bestD = 1e9;
    for (int i = 0; i < NUM_HALFTONES; ++i) {
        double d = std::fabs(std::log2(freq / toneFreq(i)));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

PitchDetector::PitchDetector(int sampleRate, int thresholdIndex)
    : sampleRate_(sampleRate) {
    threshold_ = THRESHOLDS[std::max(0, std::min(thresholdIndex, 3))];
    buffer_.reserve(ANALYSIS_BUFFER_SIZE);
    lags_.reserve(NUM_HALFTONES);
    for (int i = 0; i < NUM_HALFTONES; ++i)
        lags_.push_back(std::max(1,
            (int)std::lround(sampleRate / toneFreq(i))));
}

void PitchDetector::feed(const int16_t* pcm, size_t samples) {
    if (!samples) return;
    buffer_.insert(buffer_.end(), pcm, pcm + samples);
    if (buffer_.size() > ANALYSIS_BUFFER_SIZE)
        buffer_.erase(buffer_.begin(),
                      buffer_.end() - ANALYSIS_BUFFER_SIZE);
}

PitchResult PitchDetector::analyze() const {
    PitchResult r;
    const size_t n = buffer_.size();
    if (n < VOLUME_WINDOW) return r;

    // volume gate: max amplitude of first 1024 samples
    int peak = 0;
    for (size_t i = 0; i < VOLUME_WINDOW; ++i)
        peak = std::max(peak, (int)std::abs(buffer_[i]));
    r.volume = peak / 32768.0f;
    if (r.volume < threshold_) return r;

    // one-period-lag autocorrelation score per candidate halftone
    float weights[NUM_HALFTONES];
    float bestW = -1;
    for (int t = 0; t < NUM_HALFTONES; ++t) {
        const int lag = lags_[t];
        if ((size_t)lag >= n) { weights[t] = -1; continue; }
        // 64-bit accumulator: worst case ~4095*65535 ~ 2^28
        int64_t acc = 0;
        const int16_t* b = buffer_.data();
        for (size_t i = 0; i + lag < n; ++i)
            acc += std::abs((int)b[i] - b[i + lag]);
        // upstream divides by buffer size, not sample count — kept faithful
        weights[t] = (float)(1.0 - (double)acc / 65535.0 / n);
        if (weights[t] > bestW) bestW = weights[t];
    }
    // subharmonic guard: prefer the highest tone within EPS of the max
    int best = -1;
    for (int t = NUM_HALFTONES - 1; t >= 0; --t)
        if (weights[t] >= bestW - 0.01f) { best = t; break; }
    if (best < 0) return r;
    r.valid = true;
    r.toneAbs = best;
    r.tone = best % 12;
    return r;
}

} // namespace uss
