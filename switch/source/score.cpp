#include "score.h"
#include <algorithm>
#include <cmath>

namespace uss {

int ScoreState::scoreInt() const {
    return (int)std::lround(score / 10.0) * 10;
}
int ScoreState::scoreGoldenInt() const {
    double g = scoreGolden / 10.0;
    return (scoreInt() < score ? (int)std::ceil(g) : (int)std::floor(g)) * 10;
}
int ScoreState::scoreLineInt() const {
    return (int)std::floor(scoreLine / 10.0) * 10;
}

Scorer::Scorer(const Chart& chart, const std::string& track,
               int difficulty, bool lineBonus)
    : lines_(chart.lines(track)),
      lineBonus_(lineBonus) {
    range_ = 2 - std::max(0, std::min(difficulty, 2));
    maxSongPoints_ = lineBonus ? MAX_SONG_SCORE - MAX_SONG_LINE_BONUS
                             : MAX_SONG_SCORE;
    for (size_t li = 0; li < lines_.size(); ++li) {
        double lm = 0;
        int le = -1;
        for (const Note* n : lines_[li]) {
            lm += n->length * scoreFactor(n->kind);
            if (n->kind == "freestyle" || n->length <= 0) continue;
            for (int b = n->start; b < n->start + n->length; ++b)
                beatMap_[b] = {n, (int)li};
        }
        if (!lines_[li].empty()) {
            le = lines_[li].back()->start + lines_[li].back()->length - 1;
            ++numLines_;
        }
        lineMax_.push_back(lm);
        lineEnd_.push_back(le);
        scoreValue_ += lm;
    }
}

int Scorer::scoreFactor(const std::string& kind) const {
    if (kind == "golden" || kind == "rap-golden") return 2;
    if (kind == "freestyle") return 0;
    return 1;
}

double Scorer::pointsPerBeat(const Note& n) const {
    if (!scoreValue_) return 0;
    return maxSongPoints_ / scoreValue_ * scoreFactor(n.kind);
}

void Scorer::evalNotes(int beat) {
    while (!noteEval_.empty()
           && noteEval_.front().first->start
              + noteEval_.front().first->length <= beat) {
        bool hit = noteEval_.front().second;
        noteEval_.erase(noteEval_.begin());
        state_.streak = hit ? state_.streak + 1 : 0;
        state_.maxStreak = std::max(state_.maxStreak, state_.streak);
    }
}

LineEvent Scorer::applyLineBonus(int lineIdx) {
    double current = state_.score + state_.scoreGolden;
    double lineScore = current - state_.scoreLast;
    double maxLine = scoreValue_
        ? maxSongPoints_ / scoreValue_ * lineMax_[lineIdx] : 0;
    double perfection = maxLine <= 2 ? 1.0 : lineScore / (maxLine - 2);
    perfection = std::max(0.0, std::min(1.0, perfection));
    state_.scoreLine +=
        (double)MAX_SONG_LINE_BONUS / std::max(1, numLines_) * perfection;
    state_.scoreLast = current;
    state_.lastSentencePerfect = perfection >= 1;
    return {lineIdx, perfection,
            (int)std::lround(perfection * MAX_LINE_RATING),
            state_.total()};
}

std::optional<TickEvent> Scorer::tick(int beat, int toneAbs) {
    std::optional<LineEvent> lineEv;
    evalNotes(beat);
    auto it = beatMap_.find(beat);
    if (it == beatMap_.end()) {
        if (pendingLine_ >= 0 && beat > lineEnd_[pendingLine_]) {
            lineEv = applyLineBonus(pendingLine_);
            pendingLine_ = -1;
        }
        if (!lineEv) return std::nullopt;
        TickEvent ev;
        ev.beat = beat; ev.line = *lineEv;
        return ev;
    }
    const Note* note = it->second.first;
    int lineIdx = it->second.second;
    if (noteEval_.empty() || noteEval_.back().first != note)
        noteEval_.push_back({note, false});

    if (pendingLine_ >= 0 && beat > lineEnd_[pendingLine_]) {
        lineEv = applyLineBonus(pendingLine_);
        pendingLine_ = -1;
    }
    if (beat == lineEnd_[lineIdx]) pendingLine_ = lineIdx;

    if (toneAbs < 0) {
        TickEvent ev; ev.beat = beat;
        if (lineEv) ev.line = *lineEv;
        return ev;
    }

    // octave-normalize sung tone within +-6 of the target
    int tone = toneAbs;
    while (tone - note->pitch > 6) tone -= 12;
    while (tone - note->pitch < -6) tone += 12;

    bool isHit = std::abs(note->pitch - tone) <= range_
                 || note->kind == "rap" || note->kind == "rap-golden";
    int displayTone = isHit ? note->pitch : tone;
    noteEval_.back().second = noteEval_.back().second || isHit;

    if (isHit) {
        double pts = pointsPerBeat(*note);
        if (note->kind == "golden" || note->kind == "rap-golden")
            state_.scoreGolden += pts;
        else
            state_.score += pts;
    }

    PlayerNote* last = state_.notes.empty() ? nullptr : &state_.notes.back();
    if (last && last->tone == displayTone
        && last->start + last->length == beat) {
        last->length += 1;
        last->hit = last->hit || isHit;
    } else {
        state_.notes.push_back({beat, 1, displayTone, isHit,
                                note->kind, isHit && note->start == beat});
    }

    TickEvent ev;
    ev.beat = beat; ev.hit = isHit; ev.tone = displayTone;
    ev.kind = note->kind; ev.score = state_.total();
    if (lineEv) ev.line = *lineEv;
    return ev;
}

ScoreState Scorer::finish() {
    if (pendingLine_ >= 0) {
        applyLineBonus(pendingLine_);
        pendingLine_ = -1;
    }
    while (!noteEval_.empty()) {
        bool hit = noteEval_.front().second;
        noteEval_.erase(noteEval_.begin());
        state_.streak = hit ? state_.streak + 1 : 0;
        state_.maxStreak = std::max(state_.maxStreak, state_.streak);
    }
    return state_;
}

} // namespace uss
