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

## Installation candidate

Copy the **snake** folder, including its manifest, Lua files, font binary assets and icon, into:

```
/.crosssmudge/applications/snake/
```

Use merge-copy; do not delete the reader's saved-data folder. Reopen Applications and select **Snake**. The existing App Store installation can be upgraded in place to 0.1.1; never uninstall it to repair input. Publication/readback receipts establish Store availability separately from physical installation and panel qualification.

## Verified candidate

- 16 native VM runs passed: core, glyph/raster, app-lifecycle and touch-dispatch suites, each in portrait and landscape under 220 KiB and 2 MiB limits.
- Version 0.1.1 preserves steering and pause taps while movement redraws are pending. It also gives swipe intent priority when one release is delivered as tap followed by swipe, preserving the movement timer. Both regression sequences fail on the predecessor and pass through the unchanged firmware 2.0.3 `onTap`/`onTouch`/`onSwipe` callback bodies and touch dispatcher after this fix.
- Full SDL runtime captures additionally exercise real scripted swipe start, steering, tap pause and tap resume. The archived simulator runtime is not a 2.0.3 firmware build or a physical-reader test.
- Core suite has 13 rule tests, including all board dimensions from 3–32 columns and 1–32 rows.
- App callbacks additionally exercise 32-bit millisecond rollover, no unseen catch-up movement, failed SD writes, full-board victory and sleep-override cleanup.
- All 380 shipped glyph masks were compared with FreeType output; the production bitmap renderer's secondary-text ink ratio is approximately 75%.
- Native-suite captures are production-method host renderer fixtures; separate SDL screenshots use the full archived simulator. Neither establishes physical reader qualification. Full-repository policy testing remains blocked by the host's missing Node executable; six other policy checks passed.
- No firmware or main-branch change is part of this app-only fix. Store publication is tracked separately; it does not imply physical installation.

## Tests

The repository test entrypoint is `test/snake/run.py`. It executes the original Lua core plus the real app callbacks in the production int32/float32 VM and the existing production-method shape/bitmap capture runner. Clock, input and storage are controlled adapters. This establishes host callback/raster behavior, not physical hardware qualification.

```sh
python3 test/snake/run.py --runner /path/to/capture-runner --output /fresh/output/directory
```

The current runner must also expose the released touch callbacks. Build the base raster tooling first, then use `test/snake/build_touch_runner.py` with pinned firmware 2.0.3 `LuaRunner.cpp` and `LuaTouchDispatch.h`, the base tooling directory, an owned output directory and the host sysroot. Do not run the touch suite with a direct-handler-only runner.

To build the base raster tooling when required:

```sh
python3 test/crossplay_ui_parity/build_capture.py --output /owned/tooling/directory
```

App font assets are generated with `test/snake/generate_fonts.py` using Pillow/FreeType and the source/font license paths explicitly supplied. Icons use the repository's production pre-rotation converter.
