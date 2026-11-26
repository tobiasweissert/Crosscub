# Private Sticky Notes dictation test

The existing eight Notes cards, import files, checklist operations and two-slot
journals are retained. On Sticky, **Options → Edit / dictate** opens the existing
native keyboard paint/input surface. Editing and Save work offline. Start sends
onboard microphone audio directly to OpenAI; Stop lowers the microphone rail and
commits the turn. Text appears in the editable draft as provider deltas arrive.
There is no audio file, recording upload, chat Send action or review screen.
Save is explicit and uses the existing Notes journal. Updating firmware alone
cannot replace old Lua files already installed on SD: the private test package
must also copy `apps/cp-notes/sticky_enhanced.lua`, `dictation_editor.lua` and
`key_settings.lua` into
the installed `cp-notes` folder, leaving all other files/journals/imports intact.
Back discards the editor
without replacing the card. Provider errors keep text already received.

## Private configuration

Use **Options → OpenAI key → Enter new key** for a masked, initially empty native
keyboard. Save/keyboard OK opens a separate **SAVE KEY** confirmation; Cancel
does not write. Clear key has its own **CLEAR KEY** confirmation. Only configured
status is readable; the persisted value is never loaded into Lua. New input is
necessarily present in the Lua draft until saved/cancelled; this is not an
encrypted Lua runtime or encrypted storage design.

Connect Wi-Fi with the existing Control Center before Start. Physical provisioning
of `/.crosspoint/notes-openai.key` remains compatible (one optional trailing newline).
On-device writes use the existing provider-header allowlist: 1–512 ASCII letters,
digits, hyphens or underscores, excluding injection/NUL/control bytes. Native code writes/syncs/closes/read-checks a temporary
file, preserves the prior file via backup during publication, and checks the
published bytes. Failed publication attempts restore the previous file or retain
its backup for recovery. This is checked FAT publication, not power-loss atomicity.
Never commit this
file, put it inside an app package, paste it into logs, or send it in chat.
The key file and temporary siblings are denied by existing web/plugin credential
path policy. The private SD file is **not encrypted at rest**; clearing the setting
disables new dictation sessions. This is a device-owner setting, not a multi-user credential
vault. Clear removes canonical, temp and backup files, not forensic SD remnants.
No test needs an actual key.

Only the `cp-notes` runner receives the native API. Neither keys nor endpoint URLs
are returned to Lua. The fixed connection is
`wss://api.openai.com/v1/realtime?intent=transcription`, with Bearer authentication,
`session.update`, transcription type, `gpt-live-transcribe`, 24 kHz mono PCM16,
manual turn commits and no VAD. No redirects or automatic retries are followed.
TLS reuses PluginHttp's ESP bundle/name/SNI/TLS1.2/explicit chain-date adapter;
no insecure client or verification override is introduced.

## Capture and lifecycle

The HAL uses Sticky GPIO19 CLK / GPIO20 DATA / active-high GPIO38, a 10 ms rail
settle, and zero-wait IDF PDM reads at **24 kHz** (not mislabeled 16 kHz audio).
One checked 32 KiB PSRAM ring bounds streaming backlog; overflow fails visibly
rather than growing RAM or spilling audio to SD. Capture independently stops at
30 seconds or 1,440,000 PCM bytes, even while its network worker/editor is stalled.
Stop/cancel/Home/input lock/sleep/exit lower the rail; capture owns I2S teardown.
A failed disable/delete quarantines microphone ownership until reboot. Shutdown
drains owned tasks without killing them. Shared DNS/NTP calls are synchronous;
network teardown cannot promise a hard cancellation latency while those SDK calls
are blocked, but no microphone is powered during connection setup.

The pinned SDK `Microphone` wrapper was inspected: it owns the same IDF PDM
infrastructure but has no revoke gate during begin, discards partial reads on
IDF timeout, and returns no cleanup status. The narrow Notes HAL therefore uses
that same IDF driver directly to implement the required out-of-band rail revoke
and checked worker-only teardown, without modifying the pinned SDK.

If the user edits while streaming, later deltas follow the tracked dictation
insertion endpoint, not an unrelated caret that the user has moved elsewhere. Final
provider corrections replace an untouched dictated span; they never overwrite
manual edits. A differing final after manual edits leaves those edits intact
and displays that fact. Card limits remain 3072 bytes / 97 lines.

## Manual cursor and selection controls

The keyboard works for empty/existing cards without a key or Wi-Fi. Tap native
text to position the visible caret. `<`/`>` move one UTF-8 code point; **Select**
anchors a selection, then taps/arrows extend it; **All** selects all text. Typing
replaces a selection and Del deletes it. **More** switches the navigation row to
Up/Down/Home/End/All; Up/Down use the native wrapped-line geometry. **Cursor**
switches physical arrows from keyboard focus navigation to text movement;
Confirm returns to key focus without changing the draft. Page buttons navigate
focus independently. No invented drag callback is used. Selection is visibly
inverted in the real native text renderer and scrolling follows the caret.
`+ line` inserts a newline; Save retains the existing journal path. Physical Back
still discards an unsaved editor draft, as before.

## Regression entrypoints

Set `ARDUINOJSON_INCLUDE` to the installed pinned `ArduinoJson/src` directory.
Host tests additionally need a C/C++ compiler, Python, CMake and OpenSSL libcrypto
(the latter is used only by the scripted ESP transport fixture's SHA1 oracle).

```
python3 test/notes_realtime/run.py
python3 test/notes_realtime/run.py --sanitize
cmake -S test/notes_realtime -B "$TMPDIR/notes-ctest"
ctest --test-dir "$TMPDIR/notes-ctest" --output-on-failure
```

Both modes are registered in the ordinary `test/CMakeLists.txt`. The runner
executes actual production WebSocket framing, protected paths, bounded PCM queue,
JSON event parsing, fixed-origin adapter, Sticky PDM HAL, worker session, Lua API,
and the real installed Notes Lua app/controller in both orientations. ESP/IDF
hardware/TLS scheduling is scripted; keyboard snapshots in Lua controller tests
are controlled fixtures, not visual layout or device evidence. Existing browser
activity tests also cover ordinary-runner Home/lock/sleep/overlay cancellation.
Both modes additionally execute exact extracted production UTF-8/wrap/newline/
viewport/touch/vertical-movement/selection-region methods with deterministic font
metrics, native key-store write/sync/close/read/publication fault fixtures, masked
settings/consent controller tests, and existing parser/import/journal/capacity
regressions. The geometry adapter is not a real-font renderer or SDK execution.

Target compile/link, on-glass keyboard layout/touch/eInk scheduling, acoustic
quality, physical rail measurement, actual network TLS negatives and a real
OpenAI account/session remain separate qualification gates. No provider request,
physical test, public push, CI dispatch, release or firmware flash is performed
by these tests.

Protocol: https://developers.openai.com/api/docs/guides/realtime-transcription
Hardware: https://www.seeedstudio.com/sticky/docs/en/device-guide/arduino-interaction
