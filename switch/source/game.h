#pragma once
/* Play session — media playback + mic capture + live scoring + the
   pitch-lane / lyric overlay. Ports the sing-loop semantics of
   ultrapistar/sing.py onto the native Player/Scorer/PitchDetector. */
#include "catalog.h"
#include "chart.h"
#include "fb.h"
#include "img.h"
#include "pitch.h"
#include "player.h"
#include "score.h"
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace uss {

struct TrackResult {
    int score = 0, hits = 0, notes = 0, perfect = 0, maxStreak = 0;
};

class Game {
public:
    ~Game() { stop(); }
    // karaoke=true plays instPath when present (vocals stripped);
    // false plays the original media (vocals kept)
    bool start(const Song& song, bool karaoke = true);
    void stop();

    // one frame: advance scoring to the media clock and draw the overlay
    // onto buf. Returns false once playback is over.
    bool frame(Fb& fb, uint32_t* buf);
    bool finished() const;

    std::unordered_map<std::string, TrackResult> results();
    std::string title() const { return song_.title; }
    std::string artist() const { return song_.artist; }

private:
    void micLoop();

    Song song_;
    Chart chart_;
    Player player_;
    Image art_;
    std::vector<std::unique_ptr<Scorer>> scorers_;
    std::vector<std::string> trackList_;
    int lastBeat_ = -1;
    double beatMs_ = 0, gapMs_ = 0;

    std::atomic<bool> quit_{false}, micOk_{false};
    std::atomic<int> toneAbs_{-1};
    std::thread micThread_;

    // recent sung tones for the lane trail {ms, toneAbs, hit}
    std::deque<std::tuple<double,int,bool>> trail_;
    // transient rating pops {text, color, bornMs}
    struct Pop { std::string text; Color col; double born; };
    std::vector<Pop> pops_;
    bool finDone_ = false;
    std::unordered_map<std::string, TrackResult> res_;
};

} // namespace uss
