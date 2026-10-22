# Ultra Switch Star — native client

Nintendo Switch homebrew rebuild of the Ultra Pi Star karaoke loop:
prepared songs play off the SD card, mic input is pitch-tracked, and
singing is scored against the shared `.upsong` chart format.

## Architecture

- `source/json.*` — minimal JSON parser (charts + manifest)
- `source/chart.*` — `.upsong` v2 loader, beat→ms mapping, line
  splitting (faithful to `ultrapistar/charts.py`)
- `source/score.*` — Scorer port of `ultrapistar/scoring.py`
  (WorldParty sing loop: per-beat hits, ±(2−difficulty) semitone
  tolerance, golden ×2, 1000-pt line bonus, streaks)
- `source/pitch.*` — PitchDetector port of `ultrapistar/pitch.py`
  (46-halftone lagged autocorrelation, C2–A5)
- `source/catalog.*` — prepared-folder scanner: `sdmc:/ultrapistar/
  <Artist - Title>/` + `manifest.json` + flat-root fallback +
  `.upsong` resolution (charts/, chart-library/ hunt)
- `source/player.*` — ffmpeg demux/decode thread: video frames
  software-scaled (swscale) to fit 1280×720, audio resampled
  (swresample) to 48 kHz stereo int16 and pushed through an audout
  buffer ring. Media clock = `audoutGetAudioOutPlayedSampleCount`.
- `source/fb.*` — linear framebuffer + FreeType text against the
  Horizon system font (pl service); alpha-blended primitives.
- `source/game.*` — play session: audin capture thread → PitchDetector
  → per-beat Scorer ticks; renders the pitch lane (note targets,
  sung trail, cursor), current/next lyric line, score chips, streaks,
  and PERFECT LINE / rating pops.
- `source/main.cpp` — state machine: Browser → Play → Results.

## Media layout

```
sdmc:/ultrapistar/
  manifest.json                 (station bundle index — optional)
  Adele - Hello/
    yt-<id>.mp4                 media
    yt-<id>.inst.mp4            karaoke backing (preferred when present)
    yt-<id>.upsong              chart
```

Exactly what the Windows Prep Station writes — one stick serves the
Pi and the Switch. Flat roots also scan; `.inst.mp4` files are picked
up automatically as the karaoke backing track.

## Controls

- D-pad up/down — browse; L/R — page
- A — sing the selected song
- B — abort playback → results; back from results
- + — quit

## Build

```bash
# Docker (this machine — devkitPro apt is Cloudflare-blocked here)
bash build.sh

# Or native devkitPro: pacman -S switch-dev switch-ffmpeg switch-freetype
# then `make` in switch/
```

Output: `ultraswitchstar.nro` — copy to `sdmc:/switch/ultraswitchstar/`
and run from the Homebrew Menu. Put song folders under
`sdmc:/ultrapistar/`.

## Host verification

`test/host_test.cpp` compiles the chart/score/pitch/json ports with
plain g++ and validates them against real generated charts — perfect
runs match the Python scorer's totals exactly (e.g. 9730 on the Arctic
Monkeys chart, 9680 on +44).

## Known limits / honest risks

- **Mic input** uses `audin` (default input device, 48 kHz stereo).
  Works with USB mics/dongles and 3.5mm TRRS headsets on current
  firmware; if nothing enumerates the game still plays — "no mic"
  shows in the corner and scoring reads silence.
- Decode is pure software (no NVDEC from homebrew) — 720p H.264 +
  overlay is the target; 1080p will stutter.
- No phone/LAN queueing yet — song pick is the on-device browser.
