#pragma once
/* Pitch detector — C++ port of ultrapistar/pitch.py (GPL-3.0 port of
   WorldParty URecord.pas). Streaming int16 mono PCM; lagged
   autocorrelation over 46 halftone candidates C2..A5. */
#include <cstdint>
#include <vector>

namespace uss {

constexpr int NUM_HALFTONES = 46;          // C2..A5
constexpr int ANALYSIS_BUFFER_SIZE = 4096;
constexpr int VOLUME_WINDOW = 1024;

struct PitchResult {
    bool valid = false;
    int tone = -1;        // semitone within octave 0..11
    int toneAbs = -1;     // 0..45 (C2..A5)
    float volume = 0;
};

class PitchDetector {
public:
    PitchDetector(int sampleRate = 48000, int thresholdIndex = 2);
    // append int16 mono PCM; keeps the newest 4096 samples
    void feed(const int16_t* pcm, size_t samples);
    PitchResult analyze() const;

    static double toneFreq(int toneAbs);
    static int freqToToneAbs(double freq);

private:
    int sampleRate_;
    float threshold_;
    std::vector<int16_t> buffer_;
    std::vector<int> lags_;   // samples per period per candidate tone
};

} // namespace uss
