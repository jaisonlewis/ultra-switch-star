/* Ultra Switch Star — native karaoke for prepared Ultra Pi Star
   libraries. Browser → play → results state machine on the raw
   framebuffer; ffmpeg decodes media, audout plays audio, audin feeds
   the pitch detector, and the ported Scorer grades live. */
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "catalog.h"
#include "chart.h"
#include "fb.h"
#include "game.h"
#include "usb.h"

static const char* MEDIA_ROOT = "sdmc:/ultrapistar";

enum class State { Browser, Play, Results };

static const char* gradeFor(const uss::TrackResult& r) {
    if (!r.notes) return "--";
    double pct = r.score / (double)(r.notes * 100 + 1000) * 100.0;
    if (pct >= 95) return "S";
    if (pct >= 85) return "A";
    if (pct >= 70) return "B";
    if (pct >= 50) return "C";
    return "D";
}

int main(int argc, char** argv) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    uss::Fb fb;
    bool fbOk = fb.init();
    if (!fbOk) {                        // last-ditch console fallback
        consoleInit(NULL);
        printf("fb init failed\n+: quit\n");
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        return 0;
    }

    // USB mass storage � stick in the dock's USB-A ports mounts as
    // ums0:/ etc. Poll briefly for the async mount to land.
    uss::usbInit();
    std::string rootsNote;
    auto rescan = [&]() {
        std::vector<uss::Song> out;
        std::vector<std::string> roots = uss::usbLibraryRoots("ultrapistar");
        roots.push_back(MEDIA_ROOT);           // sdmc fallback
        rootsNote.clear();
        for (auto& r : roots) {
            auto part = uss::scanCatalog(r);
            if (!part.empty()) {
                out.insert(out.end(), part.begin(), part.end());
                if (!rootsNote.empty()) rootsNote += " + ";
                rootsNote += r;
            }
        }
        return out;
    };
    // give the async UMS mount a moment before the first scan
    for (int i = 0; i < 60 && uss::usbMounts().empty(); ++i)
        svcSleepThread(50e6);
    std::vector<uss::Song> songs = rescan();
    std::unique_ptr<uss::Game> game;
    State state = State::Browser;
    int sel = 0, scroll = 0, tick = 0;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);
        if (k & HidNpadButton_Plus) break;

        uint32_t* buf = fb.begin();
        if (!buf) continue;
        const int W = fb.width(), H = fb.height();

        if (state == State::Browser) {
            fb.clear(buf, uss::rgb(8, 10, 18));
            fb.text(buf, "ULTRA SWITCH STAR", W / 2, 40, 48,
                    uss::rgb(255, 255, 255), true);
            std::string sub = std::to_string(songs.size()) +
                " songs — " + MEDIA_ROOT;
            fb.text(buf, sub, W / 2, 100, 20, uss::rgb(140, 150, 175),
                    true);

            const int VISIBLE = 18, ROW = 30;
            if (sel < scroll) scroll = sel;
            if (sel >= scroll + VISIBLE) scroll = sel - VISIBLE + 1;
            for (int i = scroll;
                 i < (int)songs.size() && i < scroll + VISIBLE; ++i) {
                const auto& s = songs[i];
                int y = 160 + (i - scroll) * ROW;
                bool play = !s.chartPath.empty() && !s.mediaPath.empty();
                uss::Color c = play ? uss::rgb(235, 238, 245)
                                    : uss::rgb(110, 116, 135);
                if (i == sel)
                    fb.fill(buf, W / 8, y - 4, W * 3 / 4, ROW,
                            {40, 60, 120, 255});
                std::string line = s.artist + " - " + s.title +
                    (play ? "" : "  (no chart)");
                fb.text(buf, line, W / 8 + 12, y, 20, c);
            }
            if (songs.empty()) {
                fb.text(buf, "No prepared library found.",
                        W / 2, H / 2 - 30, 26,
                        uss::rgb(255, 200, 90), true);
                fb.text(buf,
                        "copy <Artist - Title>/ folders to "
                        "sdmc:/ultrapistar/",
                        W / 2, H / 2 + 10, 20,
                        uss::rgb(150, 160, 180), true);
            }
            fb.text(buf,
                    "D-pad: browse   A: sing   X: rescan   +: quit",
                    W / 2, H - 44, 20, uss::rgb(140, 150, 175), true);

            if (k & HidNpadButton_Down)
                sel = std::min(sel + 1, (int)songs.size() - 1);
            if (k & HidNpadButton_Up)
                sel = std::max(sel - 1, 0);
            if (k & HidNpadButton_L)
                sel = std::max(sel - VISIBLE, 0);
            if (k & HidNpadButton_R)
                sel = std::min(sel + VISIBLE, (int)songs.size() - 1);
            if (k & HidNpadButton_X) {
                songs = rescan();
                sel = std::min(sel, (int)songs.size() - 1);
                if (sel < 0) sel = 0;
            }
            if ((k & HidNpadButton_A) && !songs.empty()) {
                const auto& s = songs[sel];
                if (!s.chartPath.empty() && !s.mediaPath.empty()) {
                    game = std::make_unique<uss::Game>();
                    if (game->start(s)) state = State::Play;
                    else game.reset();
                }
            }
        } else if (state == State::Play) {
            bool alive = game->frame(fb, buf);
            if ((k & HidNpadButton_B) || !alive) {
                game->results();           // finalize even on abort
                state = State::Results;
            }
        } else {                           // Results
            fb.clear(buf, uss::rgb(6, 8, 16));
            fb.text(buf, "RESULTS", W / 2, 60, 52,
                    uss::rgb(255, 255, 255), true);
            std::string st = game->artist() + " - " + game->title();
            fb.text(buf, st, W / 2, 130, 22, uss::rgb(160, 170, 190),
                    true);

            auto res = game->results();
            int y = 210;
            for (auto& kv : res) {
                const auto& r = kv.second;
                char line[160];
                snprintf(line, sizeof(line),
                         "%s   %d pts   grade %s   hits %d/%d   "
                         "perfect %d   best streak %d",
                         kv.first.c_str(), r.score, gradeFor(r),
                         r.hits, r.notes, r.perfect, r.maxStreak);
                fb.text(buf, line, W / 2, y, 24,
                        uss::rgb(230, 234, 245), true);
                y += 46;
            }
            fb.text(buf, "A: back to library   +: quit",
                    W / 2, H - 60, 20, uss::rgb(140, 150, 175), true);
            if (k & (HidNpadButton_A | HidNpadButton_B)) {
                game.reset();
                state = State::Browser;
            }
        }

        fb.end();
        ++tick;
    }

    game.reset();
    uss::usbExit();
    fb.close();
    return 0;
}
