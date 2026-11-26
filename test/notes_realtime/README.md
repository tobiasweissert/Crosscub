# Private Sticky Notes dictation test

The existing eight Notes cards, import files, checklist operations and two-slot
journals are retained. On Sticky, **Options → Edit / dictate** opens the existing
native keyboard paint/input surface. Editing and Save work offline. Start sends
onboard microphone audio directly to OpenAI; Stop lowers the microphone rail and
commits the turn. Text appears in the editable draft as provider deltas arrive.
There is no audio file, recording upload, chat Send action or review screen.
Save is explicit and uses the existing Notes journal. Updating firmware alone
cannot replace old Lua files already installed on SD: the private test package
must also copy `apps/cp-notes/sticky_enhanced.lua` and `dictation_editor.lua` into
the installed `cp-notes` folder, leaving all other files/journals/imports intact.
Back discards the editor
without replacing the card. Provider errors keep text already received.

## Private configuration

Connect Wi-Fi with the existing Control Center before Start. On the physically
mounted owner SD card, provision `/.crosspoint/notes-openai.key` containing only
the owner's OpenAI API key (one optional trailing newline). Never commit this
file, put it inside an app package, paste it into logs, or send it in chat.
The key file and temporary siblings are denied by existing web/plugin credential
path policy. The private SD file is **not encrypted at rest**; removing it disables
new dictation sessions. This is a deliberately small private test provisioning
path, not a public multi-user credential UI. No test needs an actual key.

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

If the user edits while streaming, later deltas append at their cursor. Final
provider corrections replace an untouched dictated span; they never overwrite
manual edits. A differing final after manual edits leaves those edits intact
and displays that fact. Card limits remain 3072 bytes / 97 lines.

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

Target compile/link, on-glass keyboard layout/touch/eInk scheduling, acoustic
quality, physical rail measurement, actual network TLS negatives and a real
OpenAI account/session remain separate qualification gates. No provider request,
physical test, public push, CI dispatch, release or firmware flash is performed
by these tests.

Protocol: https://developers.openai.com/api/docs/guides/realtime-transcription
Hardware: https://www.seeedstudio.com/sticky/docs/en/device-guide/arduino-interaction
