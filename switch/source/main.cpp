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
#include "img.h"
#include "usb.h"

static const char* MEDIA_ROOT = "sdmc:/ultrapistar";

enum class State { Browser, Choose, Play, Results };

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
    uss::diag("=== boot (build %s %s) ===", __DATE__, __TIME__);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    uss::diag("pad ok, fb.init next");

    uss::Fb fb;
    bool fbOk = fb.init();
    uss::diag("fb.init -> %d", (int)fbOk);
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
    bool usbOk = uss::usbInit();
    char usbStatus[96];
    if (!usbOk)
        snprintf(usbStatus, sizeof(usbStatus),
                 "usb init failed 0x%x", uss::usbInitRc());
    else
        snprintf(usbStatus, sizeof(usbStatus), "usb: no device mounted");
    int lastMounts = 0, lastSeen = 0;
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
    if (usbOk) uss::usbLogDetail();
    std::vector<uss::Song> songs = rescan();
    uss::diag("scan -> %u songs (%s)", (unsigned)songs.size(),
              rootsNote.c_str());
    std::unique_ptr<uss::Game> game;
    const uss::Song* pending = nullptr;
    State state = State::Browser;
    int sel = 0, scroll = 0, tick = 0;
    std::string artLoaded;
    uss::Image art;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);
        if (k & HidNpadButton_Plus) break;

        uint32_t* buf = fb.begin();
        if (!buf) continue;
        const int W = fb.width(), H = fb.height();

        if (state == State::Browser) {
            // periodic hotplug check (~every 2s at 60fps)
            if (usbOk && tick % 120 == 0) {
                auto [seen, mnt] = uss::usbCounts();
                int n = (int)mnt;
                if (n != lastMounts || (int)seen != lastSeen) {
                    uss::usbLogDetail();
                    lastSeen = (int)seen;
                    lastMounts = n;
                    if (n > 0) {
                        songs = rescan();
                        if (sel >= (int)songs.size()) sel = (int)songs.size() - 1;
                        if (sel < 0) sel = 0;
                    }
                }
                if (mnt)
                    snprintf(usbStatus, sizeof(usbStatus),
                             "usb: %u device(s) mounted", mnt);
                else if (seen)
                    snprintf(usbStatus, sizeof(usbStatus),
                             "usb: %u seen, none mountable - fs?", seen);
                else
                    snprintf(usbStatus, sizeof(usbStatus),
                             "usb: nothing detected - reseat stick?");
            }
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
            fb.text(buf, usbStatus, W / 2, H - 22, 14,
                    uss::rgb(120, 200, 255), true);

            // cover preview for the highlighted song (lazy load)
            if (!songs.empty()) {
                const auto& sp = songs[sel].artPath;
                if (sp != artLoaded) {
                    artLoaded = sp;
                    art = sp.empty() ? uss::Image{} : uss::loadImage(sp);
                }
                if (art.ok()) {
                    int aw = 280, ax = W - aw - 70, ay = 160;
                    fb.fill(buf, ax - 6, ay - 6, aw + 12, aw + 12,
                            {255, 255, 255, 30});
                    fb.blit(buf, art.rgba.data(), art.w, art.h,
                            ax, ay, aw, aw);
                }
            }
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
                    uss::diag("start %s (%s)", s.mediaPath.c_str(),
                              s.instPath.empty() ? "orig" : "has-inst");
                    if (!s.instPath.empty()) {
                        pending = &s;           // has both � ask which mix
                        state = State::Choose;
                    } else {
                        game = std::make_unique<uss::Game>();
                        if (game->start(s, true)) state = State::Play;
                        else game.reset();
                    }
                }
            }
        } else if (state == State::Choose) {
            // karaoke (vocals stripped) vs original video (vocals kept)
            fb.clear(buf, uss::rgb(8, 10, 18));
            fb.text(buf, pending->artist + " - " + pending->title,
                    W / 2, H / 2 - 120, 30, uss::rgb(235, 238, 245), true);
            fb.text(buf, "A: karaoke   (instrumental � you sing)",
                    W / 2, H / 2 - 40, 26, uss::rgb(90, 210, 126), true);
            fb.text(buf, "Y: original   (vocals kept � sing along)",
                    W / 2, H / 2 + 10, 26, uss::rgb(126, 164, 255), true);
            fb.text(buf, "B: back", W / 2, H / 2 + 90, 20,
                    uss::rgb(140, 150, 175), true);
            bool pickKaraoke = k & HidNpadButton_A;
            bool pickOriginal = k & HidNpadButton_Y;
            if (pickKaraoke || pickOriginal) {
                game = std::make_unique<uss::Game>();
                if (game->start(*pending, pickKaraoke))
                    state = State::Play;
                else { game.reset(); state = State::Browser; }
                pending = nullptr;
            } else if (k & HidNpadButton_B) {
                pending = nullptr;
                state = State::Browser;
            }
        } else if (state == State::Play) {
            bool alive = game->frame(fb, buf);
            if ((k & HidNpadButton_B) || !alive) {
                uss::diag("play ended (%s)", alive ? "B abort" : "eof");
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
