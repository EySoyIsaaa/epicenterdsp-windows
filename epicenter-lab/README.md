# Epicenter Lab

A tiny offline sandbox to test bass-effect changes on the PC **before** touching the app.
It runs your song through the real Epicenter DSP and writes files you can A/B, so we can
tune the new "smoother / deeper" bass (saturation + compression, à la Soundgoodizer) without
risking the sound already shipping in the app.

## What's here

- `EpicenterDSPCore.hpp` / `.cpp` — **exact copy** of the app's Epicenter DSP (baseline).
- `BassGlue.hpp` — the **new** stage we're testing: band-split → soft saturation (harmonics
  = body) → compression (sustain = depth) → parallel blend (smoothness).
- `main.cpp` — CLI that writes three WAVs to compare.
- `wav.hpp` — minimal WAV read/write.
- `build.sh` / `process.sh` — build and run helpers.

## One-time build

```bash
./build.sh
```

(Needs g++. If missing: `winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT`, then
open a new terminal.)

## Test a song

```bash
./process.sh "C:/path/to/song.mp3"
```

Produces three mp3s next to it:

| File | What it is |
|------|-----------|
| `song_epicenter.mp3` | current app sound (baseline) |
| `song_enhanced.mp3`  | baseline **+ new BassGlue** (this is the goal) |
| `song_glue.mp3`      | only the new stage (to hear it isolated) |

## Tune it

Pass `key=value` overrides:

```bash
./process.sh "song.mp3" depth=0.8 drive=3.0 mix=0.85 split=140
```

**BassGlue (the new stage):**
- `drive` [2.2] — saturation / harmonics. Higher = warmer, grittier, more "present" on headphones.
- `depth` [0.6] — compression amount (0–1). Higher = more sustain/density = deeper, fuller.
- `mix`   [0.75] — parallel blend (0–1). Higher = more effect; lower keeps more punch.
- `split` [160] — crossover Hz between "bass" and the rest.
- `outgain` [0] — final trim in dB.

**Epicenter core:** `intensity` [60] · `sweep` [45] · `width` [50] · `balance` [100] · `volume` [100]

Tell me which settings sound best and I'll port that exact configuration into the app's DSP.
