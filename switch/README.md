# Ultra Switch Star — native client

Nintendo Switch homebrew rebuild of the Ultra Pi Star karaoke loop:
prepared songs play off the SD card, mic input is pitch-tracked, and
singing is scored against the shared `.upsong` chart format.

## What already works (in tree, unit-testable on PC)

- `source/json.*` — minimal JSON parser (charts + manifest)
- `source/chart.*` — `.upsong` loader, beat→ms mapping, line splitting
  (faithful to `ultrapistar/charts.py`)
- `source/score.*` — Scorer port of `ultrapistar/scoring.py`
  (WorldParty sing loop: per-beat hits, ±(2−difficulty) semitone
  tolerance, golden ×2 factor, 1000-pt line bonus, streaks)
- `source/pitch.*` — PitchDetector port of `ultrapistar/pitch.py`
  (46-halftone lagged autocorrelation, C2–A5)
- `source/catalog.*` — prepared-folder scanner: `sdmc:/ultrapistar/
  <Artist - Title>/` + `manifest.json` + `.upsong` resolution
- `source/main.cpp` — console milestone-0 shell (browse/inspect songs)

## Media layout it expects

```
sdmc:/ultrapistar/
  manifest.json                 (station bundle index — optional)
  Adele - Hello/
    yt-<id>.mp4                 media
    yt-<id>.inst.mp4            karaoke backing (optional)
    yt-<id>.upsong              chart (or under charts/ / chart-library/)
```

That is exactly what the Windows Prep Station writes, so a prepared
512 GB stick works on the Pi *and* the Switch with zero conversion.

## Milestones

- [ ] M0 — boot, scan `sdmc:/ultrapistar`, list songs, load/validate
      charts (console UI — this tree now)
- [ ] M1 — ffmpeg demux/decode → video on EGL/deko3d + audio on audren
- [ ] M2 — pitch lane + karaoke lyric fill rendered natively (SDL2)
- [ ] M3 — USB mic capture (`audin:a`) → PitchDetector → Scorer
- [ ] M4 — results ceremony, streaks, rating pops (port of tv.js juice)
- [ ] M5 — LAN HTTP server for phone queue/join page (BSD sockets)

## Build

```bash
# Docker (this machine — devkitPro apt is Cloudflare-blocked here)
docker run --rm -v /i/AI/ultraswitchstar:/src -w /src/switch \
  devkitpro/devkita64 make

# Or native devkitPro: pacman -S switch-dev switch-sdl2 switch-ffmpeg
# then `make` in switch/
```

Output: `switch/ultraswitchstar.nro` — copy to `sdmc:/switch/` and run
from the Homebrew Menu. Media read from `sdmc:/ultrapistar/` (or the
mounted prepared USB stick at the same mount point via HOS USB).

## The honest risks

- **Mic input** is the least-proven piece: `audin` exists in libnx for
  USB-headset capture, but homebrew examples are rare. M3 is where we
  find out; the scoring code is ready either way.
- **ffmpeg on Switch** is proven by NXMP (Switch media player), but
  720p+ with a soft subtitle/overlay pipeline needs tuning — start with
  480p/720p and scale.
- Scoring math, chart parsing, and catalog layout carry over verbatim —
  the risky parts are all platform I/O.
