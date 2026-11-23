# Sticky keyboard qualification

Run the registered ordinary and UBSan entrypoints with an existing Pillow Python:

```sh
KEYBOARD_APP_FIXTURES=/path/to/keyboard-donor-export/apps \
  /usr/bin/python3 test/crosscub/run_all.py --keyboard-only --output ROOT_OUTPUT
```

The default runs both modes. On a tool runner with a short outer deadline, run
`--keyboard-mode normal` and `--keyboard-mode ubsan` separately through that same
entrypoint, using distinct output roots; neither mode filters any runtime cases.
The fixture directory should be a read-only export of the eight selected app
trees from `be632757b99e340fbeabde296ced46fce47e24d7`. Those app packages and
their Store checks are separate from this firmware source. The general host
dispatcher adds the keyboard native commands only when this fixture is set.

No firmware build, download, SDK copy, publication or owner-data access occurs.
`KEYBOARD_CACHE_ROOT` selects the retained, hash-verified Stats native cache;
`KEYBOARD_LUA_ROOT` selects source byte-identical to this checkout's Lua VM.
The sole compiler lock is `/root/.hermes/cache/scratch/crosscub-review-probes.lock`.
Every command checks the 128 MiB root reserve. Filtered development probes accept
`--filter SUBSTRING`; the routine entrypoint never filters. An empty runtime set
or a missing final SDL capture fails qualification.

## Real execution boundaries

- Complete production KeyboardEntryActivity, KeyboardLayoutSet, percent/stable-page
  dialog, FrontlightTimePickerActivity and KeyboardLayoutsActivity compile and run.
  Input goes through the retained SDL HAL and MappedInputManager. Activity-stack
  completion and render mutex are explicit host seams, not firmware navigation.
- Complete changed LuaRunner compiles. Runtime uses its exact extracted drawing
  callbacks, the actual production 32-bit-numeric Lua VM and native Gfx/UI fonts.
  Browser additionally uses the real BrowserWork coroutine admission/resumer.
- `keyboard_snapshot` is a bounded, synchronous **paint-only** binding to the same
  native header, text viewport and SDK keyboard renderer. It does not call
  onEnter/loop, push an Activity, acquire another render lock or flush the display.
  Lua keeps text, UTF8 byte cursor, caps, cancel and Done/save ownership. A regression
  rejects re-entrant render-lock construction from Lua painting.
- Five Lua editors (Flashcards, Habits, Pomodoro, Weather, Browser) assert complete
  framebuffer equality with the native opening screen in all four orientations:
  white/back header, number row, staggered letters, Shift/Delete and mode/Space/OK.
  This replaces the insufficient former single-key-only visual contract. Seven
  generic callers and four Browser route closures use real editor handlers.
  Browser route-state upvalues and generic storage are neutral isolated fixtures;
  neither Browser startup/cache restore nor providers run.
- Wordle enhanced/compact, Sudoku digit pad and Calculator original/compact retain
  their semantic layouts/actions and use the SDK key family. Actual Lua touch
  handlers exercise letters/delete/short submit, Sudoku digit selection and
  Calculator 1+2=3/clear. Wordle feedback captures retain absent/present/correct
  distinctions. This does not qualify an external game or external word bank.
- Native qualification includes ten layouts in ordinary/shift/symbol layers;
  UTF8 caps/delete, min length, cancel, URL/password, alternate/delete holds and
  swallowed releases; long multibyte and RTL draft cursor traversal; numeric
  decimal/page behavior and frontlight hour/minute input; layout mask toggling and
  last-Latin protection. Language-layer captures are landscape; other owners use
  all four orientations. No physical-device acceptance is implied.

## Captures and compatibility

SDL-exported final PNGs and additional per-caller/feedback PBM snapshots come from
that same real HAL backing framebuffer. PBM files use physical panel orientation,
not a second renderer; PNG conversion preserves their pixels. `captures.json`
binds each to its actual runtime exit, and `bindings.json` binds imported Lua/art,
complete native dependencies, cached objects and the executable.

The four generic old-firmware fallbacks remain unchanged; non-Sticky opening RGB
hashes must match the retained baseline. The three legacy game files and all 23
unified_ui copies are unchanged. Package-local helper additions need inclusion by
the parent-owned Store packager; no versions or publication are changed here.

UBSan covers newly compiled owner/Lua/binding/harness code. Reused native
renderer/theme/HAL objects remain **uninstrumented** and hash-verified. The fixture
header aliases Lexend16 to genuine UI12; keyboard UI12 assets are not replaced.
All runtime processes are in denied-egress Linux network namespaces.

## Not whole-initiative approval

The task-state continuation reconciliation is authoritative for the 51 case IDs.
A shared owner's successful renderer/input tests do not verify every caller's
activity transition. Round 2 therefore executes each exact production initiating
handler (or its keyboard branch) and captured result closure around the whole
real keyboard/numeric class. Neutral settings/credentials/bookmark/persistence,
filter and activity-stack seams are explicit: this is **bounded keyboard caller
coverage**, not full native application navigation or successful persistence.

The reader drawer uses exact extracted production sheet sizing, tab/header,
percent/page readout, keypad paint and input/result methods; unrelated pane
builders fail fast if reached. The complete real drawer TU also compiles. No
reader dictionary/font discovery/lifecycle initialization is claimed. Sticky-only
key font/radius properties have an actual pixel-equality RED then GREEN.

LCP executes the original initiating/result callback and the real pinned wolfSSL
SHA-256 implementation. The exact production enrollment gate rejects isolated
empty identity state with an empty release key. All subsequent HTTP/enrollment/
unwrap/store branches are UNREACHED fail-fast tripwires. This proves keyboard
submit/cancel and truthful unsigned-build failure, **not successful LCP unlock**.

KB-R1-01 regression uses the actual C-compiled Lua VM and production binding:
128 caught invalid-coordinate calls with an 8192-byte draft and table/string OOM
faults after native owner allocation must leave C++ new allocations balanced.
Successful calls after recoverable OOM are required. Result construction is under
nested lua_pcall; argument and protected-call stack preparation precede the native
owner, and error propagation follows its destruction. Final standalone input
rehashing includes the outer routine dispatcher and exact extracted sources.

Existing RED frames/logs and the previous 366 captures are preserved. Store
delivery, independent delta review, target build and device acceptance remain
parent-owned.
