# Clock for Crosscub

An offline, full-screen Lua clock for the 480×800 reTerminal Sticky running Crosscub. Oversized black hours above minutes, a thin rule, and an English date below. Uses the reader's configured local clock and UTC offset; it does not contact a server or change time settings.

- 24-hour time, including leading zeroes.
- No seconds or decorative UI; redraw only when the displayed minute/date changes.
- Clean full refresh on entry and at each quarter-hour boundary.
- **Back** exits. Normal automatic sleep is suppressed while the clock is open and released on exit. This is an active app, not an RTC-waking low-power sleep screen. It will use more power than a sleeping reader.
- Portrait is the reference layout; landscape fits the same stacked composition in the centre. The app respects the current orientation and does not change global orientation settings.
- No saves, network access, permissions, or firmware changes are required.

## Installation

Unzip the supplied SD overlay at the SD card root, preserving hidden `.crosssmudge` directories. The runtime files must end up in:

`/.crosssmudge/applications/stacked-clock/`

Open **Clock** from Crosscub's installed apps. Do not uninstall other apps or replace the entire applications directory. Set the reader's clock and UTC offset first if necessary.

Version 0.1.0 is distributed through Crosscub's owner-controlled App Store catalog. Physical-panel verification is still outstanding.

## Typography and verification

Stacked Sans is a renamed, monochrome bitmap subset of Inter (SIL OFL 1.1). See `FONT-SOURCE.json` and `OFL-Inter.txt`. The layout follows the supplied reference; font outlines are an approximation, not a pixel-identical recreation. All digits, weekdays and months are included.

Source and portable tests: `apps/stacked-clock/` and `test/stacked_clock/`. Run `python3 test/stacked_clock/run.py --runner <production-profile-capture-runner>` from the repository root. The test runner captures the actual app callbacks through reused production rendering bodies and controlled clock/storage/lifecycle adapters. Those captures are host-renderer fixtures, not physical-device screenshots or a full SDL firmware simulation.
