# Ultra Switch Star

Nintendo Switch karaoke — a homebrew port of the Ultra Pi Star
karaoke loop: prepared songs play off the SD card, mic input is
pitch-tracked, and singing is scored against the shared `.upsong`
chart format. One prepared USB stick serves an Ultra Pi Star, an
Ultra Switch Star, and every desktop build — same folders, same
charts, same scoring.

This repo holds two things:

- `switch/` — the native Switch client (libnx, ffmpeg, audin mic,
  framebuffer renderer). Builds to `ultraswitchstar.nro`.
  Architecture and controls live in `switch/README.md`.
- `ultrapistar/` + `station/` + `run.py` — the Python reference app
  this project shares its chart/scoring logic with (the Ultra Pi
  Star codebase the ports track). See "The shared library" below for
  why it's vendored here.

## The shared library

```
sdmc:/ultrapistar/                    (or a USB stick's root)
  manifest.json                       station bundle index — optional
  adele/hello/
    yt-<id>.mp4                       media
    yt-<id>.inst.mp4                  karaoke backing (preferred)
    yt-<id>.vocals.wav                vocal stem
    yt-<id>.analysis.json             ASR/pitch/alignment evidence
    yt-<id>.jpg                       artwork
    Adele - Hello.upsong              chart
```

`<artist_slug>/<title_slug>/` is `mediastore.song_reldir()` — the one
layout convention every consumer (Pi, Switch, desktop, prep stations)
shares. Manifest paths are root-relative so Pi and Switch resolve
them identically. A stick prepared on a PC plugs straight into
either device.

## Charts: CTC forced alignment

`.upsong` v2 is the canonical chart format — every consumer loads it
natively. Modern charts are made by **CTC forced alignment**, not by
text-matching an ASR transcript: the lyric text is Viterbi-aligned
against frame log-posteriors a CTC acoustic model emits for the
vocal stem (Parakeet-CTC-110M for English, Omnilingual-300M-CTC for
other languages), with skip states between lines so instrumentals
and ad-libs can't drag the alignment. Word onsets snap to
spectral-flux vocal attacks; ends are voicing-bounded and capped.
Charts carry `lyrics: "ctc-<backend>"` provenance in their source
block; imported human-timed charts always win over generated ones.

Producers: `ups-prep` (Rust, `I:\AI\ultra-pi-star-rust`) and the
Python pipeline (`ultrapistar/ctcalign.py` + `genchart.py`) emit the
same format — one algorithm, two ports. Batch recharting a library:

```bash
# Rust (fast, sharded, resumable — aligned charts self-skip)
ups-prep batch --library <root> --shard 0 --shards 8 [--force]

# Python
python -m station.rechart <root> --models <dir>
```

## The Switch client (switch/)

devkitPro + libnx native app:

```bash
export DEVKITPRO=/opt/devkitpro
cd switch && make        # -> ultraswitchstar.nro
```

Copies to `sdmc:/switch/ultraswitchstar/ultraswitchstar.nro`. Scans
`sdmc:/ultrapistar/` (and USB volumes via usbhsfs) for the shared
layout above; plays the `.inst.mp4` karaoke backing when present,
renders lyrics + pitch lane + sung-note trail on a linear
framebuffer (FreeType + Horizon system font), captures the mic via
audin and scores against the chart — a C++ port of
`ultrapistar/pitch.py` + `ultrapistar/scoring.py` (WorldParty
sing-loop: per-beat hits, semitone tolerance, golden ×2, line
bonuses, streaks). Media clock is the audout played-sample counter,
so video and scoring can't drift.

Full module map and controls: `switch/README.md`.

## Python app (reference fork)

`run.py` runs the same Flask + Socket.IO karaoke server as Ultra Pi
Star — `ultrapistar/` is kept vendored so the C++ ports have their
reference implementation in-tree and `station/` can prep libraries
without a second checkout.

```bash
pip install -r requirements.txt
python run.py           # phones join over LAN; /tv is the screen
python -m pytest tests -q
```

Prep station (offload the whole pipeline to a PC):

```bash
pip install -r requirements-station.txt
python -m station       # Tkinter window → prepared stick / LAN push
```

Model dirs for the CTC aligner (`--models`/`UPS_MODELS_DIR`):

```
models/
  sherpa-onnx-nemo-parakeet_tdt_ctc_110m-en-36000-int8/   English
  sherpa-onnx-omnilingual-asr-1600-languages-300M-ctc-*/  multilingual
  rmvpe.onnx                                            pitch
```

Without them the Python pipeline falls back to faster-whisper ASR
word-timing + fuzzy lyric matching.

## Tests

```bash
python -m pytest tests -q     # Python half
cd switch/test                # C++ side (devkitPro toolchain)
```

## License

GPL-3.0 — see `LICENSE`. `ultrapistar/pitch.py` and
`ultrapistar/scoring.py` (and their C++ ports in `switch/source/`)
are ports of GPL-3.0 WorldParty code; inventory in `NOTICE.md`.
