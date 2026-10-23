#pragma once
/* .upsong chart model — C++ port of ultrapistar/charts.py semantics.
   Notes live in chart beats; beat_ms = 15000/BPM (UltraStar convention),
   media_ms = gap_ms + beat * beat_ms. */
#include <string>
#include <unordered_map>
#include <vector>

namespace uss {

struct Note {
    int start = 0;           // first beat
    int length = 0;          // beats
    int pitch = 0;           // tone_abs scale (C2=0)
    std::string kind = "normal";  // normal|golden|freestyle|rap|rap-golden
    std::string text;        // syllable (leading ~ = word join)
    std::string alt;         // romanized syllable for non-latin charts ("at")
    bool wordBreak = false;
};

struct Chart {
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::vector<Note>> tracks;
    std::unordered_map<std::string, std::vector<int>> breaks;

    double bpm() const;                    // raw BPM header
    double beatMs() const;                 // 15000/bpm — ms per beat
    double gapMs() const;                  // GAP header (ms)
    bool isDuet() const { return tracks.size() > 1; }
    // any note carrying a romanized alternate → the player may offer
    // a native/romaji lyric choice at song select
    bool hasAlt() const {
        for (auto& kv : tracks)
            for (auto& n : kv.second)
                if (!n.alt.empty()) return true;
        return false;
    }

    std::vector<std::string> trackNames() const;
    // notes grouped into lyric lines split at break beats
    std::vector<std::vector<const Note*>> lines(
        const std::string& track) const;
    // media_ms for a chart beat
    double beatToMs(int beat) const {
        return gapMs() + beat * beatMs();
    }
    bool load(const std::string& path);
};

} // namespace uss
