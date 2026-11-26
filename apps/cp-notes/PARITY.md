# Notes: stock v1.6.5.3 local port

Status: installable local implementation, **not a claim of full CrossPlay feature parity**.

Target firmware source: `bae8322b7933fc46007682d2b2d13db7c157f729`.
Original rules: `CrossPlay/src/apps_local/notes/NotesCore.h and NotesCore.cpp`. Original code is MIT; notices are included.

Supported: Read local text cards; strict Markdown checkbox recognition; one-byte toggle; remove checked lines; list/page conversion; card selection, vertical paging and horizontal text scrolling; checked app-local save/resume.

Gaps and changes: Stock has no phone-transfer server or sleep-screen note hook. This port has eight fixed imports (note1.md through note8.md), max 3072 bytes and 97 lines per card, not the original arbitrary library. Original blank/empty-marker row filtering is not reproduced. A terminal CR is preserved intentionally where original conversion removes it. UTF-8 bytes are preserved and display slices avoid splitting code points; stock font glyph coverage still applies. There is no on-device text keyboard (the original also receives text externally).

Controls: Choose a card; Up/Down chooses a row; Confirm toggles a checkbox. Left/Right scrolls long text. Back opens the card menu. Bulk clear and re-import require confirmation. A labeled Menu touch target is near the top right in both orientations. Physical Back also opens the menu. Page buttons are mapped to Up/Down where the firmware sends those names.

Persistence: fixed app-relative two-slot text journals with sequence number, bounded read, checksum, semantic validation and write/readback check. Saves occur on menu/exit paths, including stock activity exit before deep sleep. No claim of surviving power removal before a save. A failed save is shown; menu exit stays available for retry. Corrupted newest saves fall back to the older valid slot. Original apps, books and imports are never written. Journal files are not part of the package.

Validation: vendored **stock Lua 5.4 with int32/float32**, all 71 stock API keys represented, real compilation and logic under a hard 76,800-byte Lua allocation cap, plus 2 MiB runs; native adapter input/draw-call/file tests in 480x800 and 800x480; repeated VM launch/close checks. Drawing is instrumented, **not the production renderer**. Neither a physical Sticky test nor a simulator/HAL run was possible here: PlatformIO is absent and the pinned SDK submodule is uninitialized. Lua allocation peaks exclude native renderer/HAL/system memory.

Install: extract the per-app ZIP into SD `/.crosssmudge/applications/`, producing `cp-notes/manifest.json` and `main.lua`, or upload that individual ZIP in stock Web Transfer /applications. If that app ID already exists, stop and back it up; do not overwrite its journals. Do not upload the combined collection ZIP to the one-app web uploader.

Hardware check: on the Sticky running the delivered v1.6.5.3 firmware, launch CP Notes; test both touch and buttons, Menu/Cancel/New as applicable, rotate using firmware settings and relaunch, save/exit/reopen, then sleep/wake and reopen. Expect the same saved state, no Application Error, and no changes to other app saves. No EPUB cache reset is required. Physical e-ink redraw latency, long AI turns and heap/PSRAM availability still need observation.

## Original-screen presentation on PSRAM hosts

At 512 KiB or more, the frontend ports the original Jersey masks, black header, paged deck with square text/list badges, true completion bars, progress strip, 40px outlined/filled checkbox marks, strikes, variable-height wrapped rows, menu sheet, confirmation and footer buttons. Original core/storage modules are unchanged. ASCII uses the original masks; imported Unicode falls back to host text so stored text and code points remain intact. Compact hosts retain the prior controller.

Differences: eight fixed note cards, original blank-row filtering and phone/sleep/editor integration remain the limitations above. No inert ADD/rename/phone/sleep controls are drawn: OPTIONS, HOW TO IMPORT and the existing import/conversion actions occupy the applicable footer/sheet controls. Library names remain Note 1–8 because the current core has no editable title metadata. A selected row has a narrow focus bar for physical buttons. Very long paragraphs continue over pages; swipes/Page buttons turn them, while Up/Down select logical rows and Left/Right retain horizontal scrolling. Glyph rounding and host Unicode metrics can change wrapping compared with FreeInkUI. This does not establish pixel-identical firmware rendering.

Run `python3 test/utility_ui/run.py` for import/toggle/confirmation/read-only-source/full-capacity checks under both memory limits and `python3 test/utility_ui/capture.py` for native bitmap previews. Provenance is in `art/sources.json`, with the original OFL notice. On hardware, import UTF-8 and long cards, page via touch/buttons, tick/clear/cancel/re-import, convert both ways, then exit/reopen and verify the original `.md` file remains unchanged.

## Current Sticky source candidate: keyboard, selection and key setting

The stock-firmware limitations above are historical. With the matching new native
firmware API, Options → Edit / dictate supports offline typing, native text taps,
UTF-8 code-point arrows, Select + tap/arrows, Select All, selection replacement/
deletion, wrapped-line movement and caret-following scrolling. More reveals
Up/Down/Home/End. Cursor switches physical arrows from key focus to text movement;
Confirm returns to key focus. Save uses the existing journal; physical Back still
discards the unsaved draft. Manual entry needs neither Wi-Fi nor an OpenAI key.

Options → OpenAI key provides masked new-key entry with explicit Save/Cancel and
separate Save/Clear confirmations. The existing key is never shown or returned
to Lua. Storage is plaintext on SD at `/.crosspoint/notes-openai.key`, protected
from app/web file access, not encrypted at rest. Key changes do not send requests;
only explicit Start begins dictation. New input necessarily exists in the Lua
draft until dismissed. Include `key_settings.lua` with the updated controller;
firmware 2.0.4 alone does not provide the new API. No release/package version or
Store publication is implied by this source candidate.

The production host regressions and deterministic native geometry tests do not
replace on-device checks for both orientations, real-font selection/caret
contrast, touch positioning, physical buttons, SD fault/power-loss behavior,
eInk refresh, recording timing/acoustics or paid-provider execution.
