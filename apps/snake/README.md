# Snake for Crosscub

A native Lua app for the Crosscub e-ink reader's existing application host, not a website or a companion service. No firmware modification or network access is required.

## Controls

- Swipe in a direction to start and steer.
- Tap anywhere to pause or resume.
- Tap after game over to return to the start screen, then swipe to play.
- Direction buttons also steer; Confirm starts/pauses/resumes; Back exits.
- Page Back / Page Forward steer left / right.

Eat the outlined rings. Walls and your own body end the run. A reverse direction into the body is rejected. Best score is saved in the app's existing per-ID save directory with two validated copies and write readback; a failed SD write is logged, not reported as saved. Each launch starts a fresh run.

## E-ink design

The supplied screenshots define the white background, rounded segments, black head, outlined ring food, top-right score and large outlined state panel. Body segments and secondary lettering use stable monochrome dither instead of pretending that the panel has smooth LCD gray.

Movement starts at one step per 350 ms and is capped at one step per painted Lua frame. Slow refreshes do not trigger a burst of unseen moves. Speed increases with scoring but never goes below 220 ms per step. Pause/ready screens do not continuously request redraws. Automatic sleep is prevented only while playing and released on pause, game over and exit. Actual panel latency and ghosting still require testing on the reader.

Portrait is 480 x 800; landscape scales the same 16 x 24 board without relocating its contents. Typography uses app-local, licensed Inter-derived bitmap glyphs called Snake Paper. See `FONT-SOURCE.json` and `OFL-Inter.txt`.

## Installation

Copy the **snake** folder, including its manifest, Lua files, font binary assets and icon, into:

```
/.crosssmudge/applications/snake/
```

In the Crosscub App Store, select **Snake** and Install. For a manual SD installation, use merge-copy and do not delete the reader's saved-data folder. Reopen Applications and select **Snake**. App Store publication does not establish physical installation or physical-panel qualification.

## Verification

- 12 native VM runs passed: core, glyph/raster and app-lifecycle suites, each in portrait and landscape under 220 KiB and 2 MiB limits.
- Core suite has 13 rule tests, including all board dimensions from 3–32 columns and 1–32 rows.
- App callbacks additionally exercise 32-bit millisecond rollover, no unseen catch-up movement, failed SD writes, full-board victory and sleep-override cleanup.
- All 380 shipped glyph masks were compared with FreeType output; the production bitmap renderer's secondary-text ink ratio is approximately 75%.
- Captures are production-method host renderer fixtures, not physical reader or full SDL screenshots. Full-repository policy testing remains blocked by the host's missing Node executable; six other policy checks passed.
- This is an app-only Store release on `integration/sticky-test-all`. No firmware or main-branch changes are included.

## Tests

The repository test entrypoint is `test/snake/run.py`. It executes the original Lua core plus the real app callbacks in the production int32/float32 VM and the existing production-method shape/bitmap capture runner. Clock, input and storage are controlled adapters. This establishes host callback/raster behavior, not physical hardware qualification.

```sh
python3 test/snake/run.py --runner /path/to/capture-runner --output /fresh/output/directory
```

To build the runner when required:

```sh
python3 test/crossplay_ui_parity/build_capture.py --output /owned/tooling/directory
```

App font assets are generated with `test/snake/generate_fonts.py` using Pillow/FreeType and the source/font license paths explicitly supplied. Icons use the repository's production pre-rotation converter.
