#include "game.h"
#include "img.h"
#include <cmath>
#include <cstring>
#include <malloc.h>

namespace uss {

static const Color TRACK_COLS[] = {
    {0xff, 0x5a, 0x5a, 255},   // P1 red
    {0x4d, 0xa3, 0xff, 255},   // P2 blue
    {0x5a, 0xd2, 0x7e, 255},   // P3 green
    {0xff, 0x9d, 0x4d, 255},   // P4 orange
};
static Color trackColor(int ti) { return TRACK_COLS[ti % 4]; }

// lane geometry (port of tv.js constants)
static const double LANE_WINDOW_MS = 4200, LANE_PAD_MS = 600;
static const char* RATINGS[] =
    {"", "COOL", "GOOD", "GREAT", "EXCELLENT", "SUPERB", "WOW", "PERFECT"};

// ---- mic capture thread: audin -> PitchDetector -> toneAbs_ --------

void Game::micLoop() {
    const int SR = 48000, CH = 2, CHUNK = SR / 30;
    u32 dsz = CHUNK * CH * 2, bsz = (dsz + 0xfff) & ~0xfff;
    u8* data = (u8*)memalign(0x1000, bsz);
    AudioInBuffer buf{};
    PitchDetector pd(SR, 2);
    std::vector<int16_t> mono(CHUNK);
    if (data && audinInitialize() == 0 && audinStartAudioIn() == 0) {
        micOk_ = true;
        memset(data, 0, bsz);
        buf.buffer = data; buf.buffer_size = bsz; buf.data_size = dsz;
        audinAppendAudioInBuffer(&buf);
        while (!quit_) {
            AudioInBuffer* rel = nullptr; u32 n = 0;
            if (audinWaitCaptureFinish(&rel, &n, 100000000) != 0 || !rel)
                continue;
            const int16_t* pcm = (const int16_t*)rel->buffer;
            size_t frames = rel->data_size / (CH * 2);
            if (frames > mono.size()) frames = mono.size();
            for (size_t i = 0; i < frames; ++i) mono[i] = pcm[i * CH];
            pd.feed(mono.data(), frames);
            PitchResult r = pd.analyze();
            toneAbs_ = r.valid ? r.toneAbs : -1;
            audinAppendAudioInBuffer(rel);
        }
        audinStopAudioIn();
        audinExit();
    }
    free(data);
    micOk_ = false;
}

// ---------------- lifecycle ----------------

bool Game::start(const Song& song, bool karaoke, bool rom) {
    song_ = song;
    if (song.chartPath.empty() || !chart_.load(song.chartPath)) return false;
    if (rom)                                   // romaji lyric choice —
        for (auto& kv : chart_.tracks)         // timing/breaks untouched
            for (auto& n : kv.second)
                if (!n.alt.empty()) n.text = n.alt;
    const std::string& media =
        (karaoke && !song.instPath.empty())
        ? song.instPath : song.mediaPath;
    if (!player_.open(media)) return false;

    beatMs_ = chart_.beatMs();
    gapMs_ = chart_.gapMs();
    trackList_ = chart_.trackNames();
    scorers_.clear();
    linesByTrack_.clear();
    res_.clear();
    finDone_ = false;
    trail_.clear();
    pops_.clear();
    for (auto& t : trackList_) {
        scorers_.push_back(std::make_unique<Scorer>(chart_, t, 1, true));
        linesByTrack_.push_back(chart_.lines(t));
    }
    lastBeat_ = (int)((player_.mediaMs() - gapMs_) / beatMs_) - 1;
    if (lastBeat_ < -1) lastBeat_ = -1;

    art_ = song.artPath.empty() ? Image{} : loadImage(song.artPath);
    quit_ = false;
    micThread_.start([](void* p) -> void* {
        ((Game*)p)->micLoop(); return nullptr; }, this, 512 << 10);
    return true;
}

void Game::stop() {
    quit_ = true;
    micThread_.join();
    player_.close();
}

bool Game::finished() const { return finDone_ || player_.finished(); }

std::unordered_map<std::string, TrackResult> Game::results() {
    if (!finDone_) {
        for (size_t i = 0; i < scorers_.size(); ++i) {
            auto st = scorers_[i]->finish();
            int hits = 0, perf = 0;
            for (auto& n : st.notes) { hits += n.hit; perf += n.perfect; }
            res_[trackList_[i]] = {st.total(), hits, (int)st.notes.size(),
                                   perf, st.maxStreak};
        }
        finDone_ = true;
    }
    return res_;
}

// ---------------- frame: score advance + draw ----------------

bool Game::frame(Fb& fb, uint32_t* buf) {
    const int W = fb.width(), H = fb.height();
    double ms = player_.mediaMs();

    // --- advance scoring (catch-up tick loop, sing.py semantics) ------
    int beat = beatMs_ > 0 ? (int)((ms - gapMs_) / beatMs_) : 0;
    while (lastBeat_ < beat) {
        ++lastBeat_;
        for (auto& sc : scorers_) {
            auto ev = sc->tick(lastBeat_, toneAbs_.load());
            if (ev && ev->line) {
                const char* txt = ev->line->rating >= 7 ? "PERFECT LINE"
                    : RATINGS[std::min(7, ev->line->rating)];
                Color col = ev->line->rating >= 5 ? rgb(255,210,77)
                    : ev->line->rating >= 3 ? rgb(90,210,126)
                    : rgb(126,164,255);
                pops_.push_back({txt, col, ms});
            }
        }
    }

    // sung trail point this frame
    int tone = toneAbs_.load();
    bool hit = false;
    if (tone >= 0 && !linesByTrack_.empty()) {
        const Note* cur = nullptr;
        for (auto& l : linesByTrack_[0])
            for (const Note* n : l) {
                double s = chart_.beatToMs(n->start),
                       e = chart_.beatToMs(n->start + n->length);
                if (ms >= s && ms <= e && n->kind != "freestyle") {
                    cur = n;
                    break;
                }
            }
        if (cur)
            hit = std::abs(tone - cur->pitch) <= 1.5;
    }
    if (tone >= 0) trail_.push_back({ms, tone, hit});
    while (!trail_.empty() && std::get<0>(trail_.front()) < ms - LANE_PAD_MS - 200)
        trail_.pop_front();

    // ---------------- draw ----------------
    // video frame (letterboxed)
    const uint8_t* img = nullptr; int iw = 0, ih = 0;
    fb.clear(buf, rgb(0, 0, 0));
    if (player_.frame(&img, &iw, &ih)) {
        int dx = (W - iw) / 2, dy = (H - ih) / 2;
        fb.blit(buf, img, iw, ih, dx, dy, iw, ih);
    } else if (art_.ok()) {
        // audio-only (mp3 etc.) � cover as the stage backdrop
        int s = std::min(H * 3 / 4, W / 2);
        fb.blit(buf, art_.rgba.data(), art_.w, art_.h,
                (W - s) / 2, (H - s) / 2, s, s);
    }

    const double t0 = ms - LANE_PAD_MS, t1 = ms + LANE_WINDOW_MS;
    auto x = [&](double t) { return (t - t0) / (t1 - t0) * W; };
    const int nowX = (int)x(ms);

    // ---- pitch lane ----
    const int lx0 = W / 10, lx1 = W - W / 10;
    const int ly0 = H * 8 / 100, lh = H * 26 / 100;
    for (size_t ti = 0; ti < trackList_.size(); ++ti) {
        Color col = trackColor(ti);
        int bandH = lh / (int)trackList_.size();
        int y0 = ly0 + ti * bandH;
        fb.fill(buf, lx0, y0, lx1 - lx0, bandH - 2, {10, 14, 28, 190});

        // visible notes → tone range
        auto& lines = linesByTrack_[ti];
        int lo = 1 << 20, hi = -1;
        for (auto& l : lines)
            for (const Note* n : l) {
                double s = chart_.beatToMs(n->start),
                       e = chart_.beatToMs(n->start + n->length);
                if (e > t0 && s < t1) {
                    lo = std::min(lo, n->pitch); hi = std::max(hi, n->pitch);
                }
            }
        if (hi < lo) continue;
        lo -= 2; hi += 2;
        auto y = [&](int tone) {
            return y0 + bandH - 8 -
                (int)((tone - lo) / (double)(hi - lo) * (bandH - 16));
        };
        int rowH = std::max(5, (bandH - 16) / (hi - lo));

        for (auto& l : lines) {
            // phrase separator tint
            double ls = chart_.beatToMs(l[0]->start),
                   le = chart_.beatToMs(l.back()->start
                                        + l.back()->length);
            if (le > t0 && ls < t1) {
                int fx = std::max(lx0, (int)x(ls)),
                    fw = std::min((int)x(le), lx1) - fx;
                if (fw > 0)
                    fb.fill(buf, fx, y0 + 4, fw, bandH - 10,
                            {255, 255, 255, 10});
            }
            for (const Note* n : l) {
                double s = chart_.beatToMs(n->start),
                       e = chart_.beatToMs(n->start + n->length);
                if (e <= t0 || s >= t1) continue;
                int nx = std::max(lx0, (int)x(s));
                int nw = std::max(4, std::min((int)x(e), lx1) - nx);
                int ny = y(n->pitch) - rowH / 2;
                bool golden = n->kind == "golden" || n->kind == "rap-golden";
                Color nc = golden ? rgb(255, 210, 77)
                    : n->kind == "freestyle" ? Color{255,255,255,80} : col;
                fb.fill(buf, nx, ny, nw, rowH, nc);
                // elapsed portion brightens as the cursor passes
                if (ms > s && n->kind != "freestyle") {
                    int fw2 = std::min((int)x(std::min(ms, e)), lx1) - nx;
                    if (fw2 > 0)
                        fb.fill(buf, nx, ny, fw2, rowH,
                                {255, 240, 180, 140});
                }
            }
        }

        // sung trail polyline
        int ppx = -1, ppy = -1; double ppms = 0;
        for (auto& p : trail_) {
            double pms = std::get<0>(p); int pt = std::get<1>(p);
            bool phit = std::get<2>(p);
            int px = (int)x(pms), py = y(pt);
            if (ppx >= 0 && pms - ppms < 260)
                fb.hline(buf, ppx, px, py, 3,
                         phit ? col : rgb(255, 90, 90));
            ppx = px; ppy = py; ppms = pms;
        }
        (void)ppy;
    }

    // now cursor
    fb.fill(buf, nowX - 1, ly0, 3, lh, {255, 255, 255, 200});

    // ---- lyrics ----
    if (!linesByTrack_.empty()) {
        const char* cur = nullptr;
        std::string nxt;
        for (auto& l : linesByTrack_[0]) {
            double s = chart_.beatToMs(l[0]->start),
                   e = chart_.beatToMs(l.back()->start
                                       + l.back()->length);
            if (ms >= s - 400 && ms <= e + 400) {
                static thread_local std::string curS;
                curS.clear();
                for (size_t i = 0; i < l.size(); ++i) {
                    const Note* n = l[i];
                    if (i && n->wordBreak) curS += " ";
                    curS += n->text;
                }
                cur = curS.c_str();
            } else if (s > ms && nxt.empty()) {
                for (const Note* n : l) nxt += n->text + " ";
            }
        }
        fb.fill(buf, W / 10, H - H * 14 / 100, W * 8 / 10,
                H * 10 / 100, {6, 9, 20, 230});
        if (cur)
            fb.text(buf, cur, W / 2, H - H * 11 / 100, 30,
                    rgb(255, 255, 255), true);
        if (!nxt.empty())
            fb.text(buf, nxt.c_str(), W / 2, H - H * 5 / 100, 18,
                    rgb(160, 170, 190), true);
    }

    // ---- score chips ----
    {
        int cx = W / 10;
        for (size_t ti = 0; ti < trackList_.size(); ++ti) {
            const ScoreState& st = scorers_[ti]->state();
            std::string s = trackList_[ti] + " " +
                std::to_string(st.total());
            fb.text(buf, s, cx, 14, 26, trackColor(ti));
            cx += fb.textWidth(s, 26) + 40;
            if (st.streak >= 3) {
                std::string b = "x" + std::to_string(st.streak);
                fb.text(buf, b, cx, 16, 22, rgb(255, 210, 77));
                cx += fb.textWidth(b, 22) + 30;
            }
        }
        if (!micOk_)
            fb.text(buf, "no mic", W - 120, 16, 22, rgb(255, 90, 90));
    }

    // ---- rating pops ----
    for (auto it = pops_.begin(); it != pops_.end();) {
        double age = ms - it->born;
        if (age > 2200) { it = pops_.erase(it); continue; }
        float a = age < 200 ? age / 200.0f
                : age > 1700 ? (2200 - age) / 500.0f : 1.0f;
        Color c = it->col; c.a = (uint8_t)(255 * a);
        int rise = age > 1700 ? (int)((age - 1700) / 500.0 * 30) : 0;
        fb.text(buf, it->text, W / 2, H * 38 / 100 - rise, 44, c, true);
        ++it;
    }

    return !finished();
}

} // namespace uss
