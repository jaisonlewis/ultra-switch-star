/* Mic capture → pitch readout screen. Pattern follows libnx's
   examples/switch/audio/echo: default input device, 48kHz stereo int16,
   0x1000-aligned buffers, append → wait-finish → re-append loop.
   Downmix to mono by taking channel 0. */
#include <switch.h>
#include <malloc.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mictest.h"
#include "pitch.h"

#define SAMPLERATE   48000
#define CHANNELCOUNT 2
#define FRAMERATE    (1000 / 30)              // ~33Hz capture ticks
#define SAMPLECOUNT  (SAMPLERATE / FRAMERATE) // samples per buffer
#define BYTESPERSAMPLE 2

static const char* TONE_NAMES[12] =
    {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void runMicTest() {
    Result rc;
    AudioInBuffer in_buf;
    AudioInBuffer* released = NULL;
    u32 released_count = 0;

    u32 data_size = SAMPLECOUNT * CHANNELCOUNT * BYTESPERSAMPLE;
    u32 buffer_size = (data_size + 0xfff) & ~0xfff;
    u8* data = (u8*)memalign(0x1000, buffer_size);

    printf("\x1b[2J");
    printf("\x1b[1;1HUltra Switch Star — mic test\n\n");
    if (!data) {
        printf("out of memory\nB: back\n");
        goto wait_out;
    }

    rc = audinInitialize();
    printf("audinInitialize: 0x%x\n", rc);
    if (R_FAILED(rc)) goto wait_out;
    rc = audinStartAudioIn();
    printf("audinStartAudioIn: 0x%x\n", rc);
    if (R_FAILED(rc)) { audinExit(); goto wait_out; }

    memset(data, 0, buffer_size);
    memset(&in_buf, 0, sizeof(in_buf));
    in_buf.buffer = data;
    in_buf.buffer_size = buffer_size;
    in_buf.data_size = data_size;
    audinAppendAudioInBuffer(&in_buf);

    {
        uss::PitchDetector pd(SAMPLERATE, /*thresholdIndex*/2);
        std::vector<int16_t> mono(SAMPLECOUNT);
        printf("\nSpeak/sing into a USB or headset mic.\n");
        printf("B: back   +: quit\n\n");

        while (appletMainLoop()) {
            // pad is updated by the caller's main loop — this screen owns
            // its own PadState copy
            PadState pad;
            padInitializeDefault(&pad);
            padUpdate(&pad);
            u64 k = padGetButtonsDown(&pad);
            if (k & (HidNpadButton_Plus | HidNpadButton_B)) break;

            if (audinWaitCaptureFinish(&released, &released_count,
                                       100000000 /*100ms*/) != 0
                || !released)
                continue;

            // stereo interleaved -> take channel 0
            const int16_t* pcm = (const int16_t*)released->buffer;
            size_t frames = released->data_size /
                            (CHANNELCOUNT * BYTESPERSAMPLE);
            for (size_t i = 0; i < frames && i < mono.size(); ++i)
                mono[i] = pcm[i * CHANNELCOUNT];
            pd.feed(mono.data(), frames);

            uss::PitchResult r = pd.analyze();
            printf("\x1b[20;1Hvol: %-40.40s %5.1f%%\n",
                   std::string((int)(r.volume * 40), '#').c_str(),
                   r.volume * 100);
            if (r.valid)
                printf("note: %s%d   (tone_abs %d, %.1f Hz)      \n",
                       TONE_NAMES[r.tone], r.toneAbs / 12 + 2,
                       r.toneAbs, uss::PitchDetector::toneFreq(r.toneAbs));
            else
                printf("note: --                                              \n");
            consoleUpdate(NULL);

            audinAppendAudioInBuffer(released);
            released = NULL;
        }
        audinStopAudioIn();
        audinExit();
        free(data);
        printf("\x1b[2J");
        return;
    }

wait_out:
    printf("\nB: back\n");
    {
        PadState pad; padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad)
                & (HidNpadButton_B | HidNpadButton_Plus)) break;
            consoleUpdate(NULL);
        }
    }
    free(data);
    printf("\x1b[2J");
}
