#pragma once
/* Scoring engine — C++ port of ultrapistar/scoring.py, itself a GPL-3.0
   port of UltraStar WorldParty (UNote.pas / UScreenSingController.pas).
   Faithful: per-beat hits, octave-normalized tolerance, golden/line
   complementary rounding, streak per note with >=1 hit beat. */
#include "chart.h"
#include <optional>
#include <unordered_map>
#include <vector>

namespace uss {

constexpr int MAX_SONG_SCORE = 10000;
constexpr int MAX_SONG_LINE_BONUS = 1000;
constexpr int MAX_LINE_RATING = 8;

struct PlayerNote {
    int start, length, tone;
    bool hit;
    std::string kind = "normal";
    bool perfect = false;
};

struct ScoreState {
    double score = 0, scoreGolden = 0, scoreLine = 0, scoreLast = 0;
    std::vector<PlayerNote> notes;
    int streak = 0, maxStreak = 0;
    bool lastSentencePerfect = false;

    int scoreInt() const;        // round(score/10)*10
    int scoreGoldenInt() const;  // complementary rounding to scoreInt
    int scoreLineInt() const;    // floor(scoreLine/10)*10
    int total() const { return scoreInt() + scoreGoldenInt() + scoreLineInt(); }
};

struct LineEvent {
    int line;
    double perfection;
    int rating;                  // round(perfection * MAX_LINE_RATING)
    int score;                   // state.total() after applying
};

struct TickEvent {
    int beat;
    bool hit = false;
    int tone = -1;
    std::string kind;
    int score = 0;
    std::optional<LineEvent> line;
};

class Scorer {
public:
    Scorer(const Chart& chart, const std::string& track = "P1",
           int difficulty = 0, bool lineBonus = true);

    // Port of UNote.pas sing loop for one player/beat.
    // toneAbs: detector tone (0..45) or -1 when no valid pitch.
    std::optional<TickEvent> tick(int beat, int toneAbs);
    double pointsPerBeat(const Note& n) const;

    const ScoreState& state() const { return state_; }
    const std::vector<std::vector<const Note*>>& lines() const { return lines_; }

    // flush trailing phrase bonus; final per-track state
    ScoreState finish();

private:
    int scoreFactor(const std::string& kind) const;
    void evalNotes(int beat);
    LineEvent applyLineBonus(int lineIdx);

    std::vector<std::vector<const Note*>> lines_;
    int range_, numLines_ = 0;
    bool lineBonus_;
    double maxSongPoints_, scoreValue_ = 0;
    std::unordered_map<int, std::pair<const Note*, int>> beatMap_;
    std::vector<double> lineMax_;
    std::vector<int> lineEnd_;
    ScoreState state_;
    int pendingLine_ = -1;
    std::vector<std::pair<const Note*, bool>> noteEval_;  // oldest first
};

} // namespace uss
