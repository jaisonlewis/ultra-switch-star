/* Ultra Switch Star — milestone 0: catalog + chart smoke test.
   Console UI for now; SDL2/ffmpeg playback layers on next. */
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <vector>

#include "catalog.h"
#include "chart.h"

static const char* MEDIA_ROOT = "sdmc:/ultrapistar";

int main(int argc, char** argv) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    std::vector<uss::Song> songs = uss::scanCatalog(MEDIA_ROOT);
    printf("Ultra Switch Star\n\n");
    printf("%s: %zu songs\n\n", MEDIA_ROOT, songs.size());
    if (songs.empty()) {
        printf("No prepared library found.\n");
        printf("Expected sdmc:/ultrapistar/<Artist - Title>/*.mp4\n\n");
    }

    int sel = 0, scroll = 0;
    const int VISIBLE = 20;
    char status[256] = "D-pad: browse   A: inspect   +: quit";

    auto redraw = [&]() {
        printf("\x1b[2J");          // clear
        printf("\x1b[1;1HUltra Switch Star  (%zu songs)\n\n", songs.size());
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + VISIBLE) scroll = sel - VISIBLE + 1;
        for (int i = scroll;
             i < (int)songs.size() && i < scroll + VISIBLE; ++i) {
            const auto& s = songs[i];
            printf("%s %-52.52s %s\n",
                   i == sel ? ">" : " ",
                   (s.artist + " - " + s.title).c_str(),
                   s.chartPath.empty() ? "(no chart)" : "[chart]");
        }
        printf("\x1b[26;1H%s\n", status);
        consoleUpdate(NULL);
    };
    redraw();

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);
        if (k & HidNpadButton_Plus) break;
        if (songs.empty()) continue;
        if (k & HidNpadButton_Down) { sel = std::min(sel + 1, (int)songs.size() - 1); redraw(); }
        if (k & HidNpadButton_Up)   { sel = std::max(sel - 1, 0); redraw(); }
        if (k & HidNpadButton_A) {
            const auto& s = songs[sel];
            if (s.chartPath.empty()) {
                snprintf(status, sizeof(status), "no chart for %s",
                         s.title.c_str());
            } else {
                uss::Chart ch;
                if (!ch.load(s.chartPath)) {
                    snprintf(status, sizeof(status), "chart load failed");
                } else {
                    int notes = 0;
                    for (auto& t : ch.trackNames())
                        for (auto& l : ch.lines(t)) notes += (int)l.size();
                    snprintf(status, sizeof(status),
                             "%s | %.0f bpm | %d notes | %s",
                             ch.isDuet() ? "duet" : "solo",
                             15000.0 / ch.beatMs() / 4, notes,
                             s.instPath.empty() ? "no inst" : "inst ok");
                }
            }
            redraw();
        }
    }

    consoleExit(NULL);
    return 0;
}
