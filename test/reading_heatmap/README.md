# Reading activity heatmap qualification

`python3 test/crosscub/run_all.py --heatmap-only --output /absolute/evidence/path`
runs the production calendar and extracted, unchanged Activity input methods in normal
and ASan/UBSan modes. Both are also registered in `test/crosscub/CMakeLists.txt`.
No network, dependency installation, migration or firmware build is involved.

The calendar model reads the existing 730-day Boolean bitset. It displays the last
14 Monday-first calendar weeks including the current partial week. Future cells in
that week remain outside the display; dates before 2000-01-01 are also outside.
The window contains 92–98 days depending on today's weekday, not a fixed 98-day
rolling period. Outside-period/future padding is absent;
out-of-retention/missing history is unknown, not a zero-duration reading day.
The anchor is the latest recorded activity, not today's date. There is no daily
minutes/intensity field and the heatmap does not create one.

The first page is Reading activity, followed by Per Book, This Device and optional
All Devices. Both portrait and landscape show exactly one continuous calendar grid.
Only a compact active-day count (or `No history`), small month/weekday labels and the
native header precede the recent-book section; invalid clocks show `Set clock` but
still allow recent-book totals below it. The section snapshots up to three unique,
existing paths from `RecentBooksStore` in its original order on Activity entry.
`RecentReadingStats` uses EPUB/Markdown and XTC cache identities and the real
`BookReadingStats` lifetime decoder with `LoadMode::ReadOnly`. TXT uses its own
reader/index cache and has no lifetime-statistics producer; absent time stays zero.
Cache-helper doubles test selection only, not real TXT tracking. The matching
active book's constructor-supplied lifetime preview replaces the disk value (even
when lower after reset); other books keep persisted totals. Nothing is added,
reordered or saved, and progress/history are not repurposed as time.
Cached thumbnails (including adaptive Home thumbnails, with a 48-file bounded
read-only search in that book's cache) use native Bitmap rendering; missing/corrupt
art uses `BaseTheme::drawCoverPlaceholder`. Both orientations use up to three
vertically stacked full-width rows, each with its cover on the left and a native
ellipsized title plus untruncated lifetime total on the right. The 14 calendar
columns span the available plot width after the weekday-label band; landscape
uses rectangular cells to reserve space for all three rows and button hints.
Insufficient tiny bounds omit the chart/rows instead of drawing negative/out-of-screen rectangles.
Missing/corrupt/zero-duration records show `No recorded time`, not a fabricated
lifetime value. Recovery-only v5 stats are left intact and shown without a duration:
the explicit codec mode disables repair, including when canonical exists but
opening it fails. The ordinary default `load` still recovers `.tmp`/`.bak` files. The existing reader
remains responsible for recovery. `codec_probe.py` executes the actual codec and
loader against in-memory storage and asserts zero mutation calls; its stubs derive
from the existing `test/book_reading_stats_atomic` fixtures. `recent_probe.py`
separately checks ordering, identity, cache-helper routing, bounds and cover fallback.
`store_probe.py` is also registered in the same normal/sanitizer routine. It compiles
actual `RecentBooksStore.cpp`, its exact repository `Serialization.h` and the real
`PersistableStoreBase::readDocFromFile` body with ArduinoJson. It starts a fresh
process for each cold legacy v1/v2/v3 and JSON/recovery/fault scenario, before any
Home/getBooks initialization, checking every storage byte and mutation counter.
The store-owned snapshot reuses these decoders, does not mark the singleton loaded,
and never constructs an EPUB or runs migration. Ordinary getBooks migration remains
separately asserted; its writes are acknowledgement doubles, not physical SD tests.
The snapshot can conservatively omit unreadable/truncated records, leaving originals
and recovery roots intact. The JSON dependency must already exist: set
`READING_HEATMAP_ARDUINOJSON` to its `src` directory if the documented host cache
is absent. No download/install or new CMake suite is needed.
Solid cells mean recorded
activity, hollow cells inactivity, dots unavailable history; an extra outline marks
today. Missing history is anchor zero **with bit zero clear**; a recorded 2000-01-01
is anchor zero **with bit zero set**, and is covered through the production writer.
Left/Right and swipes navigate pages; Confirm or the started-date touch target on
Per Book still opens date editing. No-RTC devices retain the combined stats route.

The portable tests compile the real `ReadingStatsUtils.cpp` with only clock/settings
HAL boundaries stubbed, and compare Gregorian boundaries against Python datetime.
The Activity slice tests execute production loop/navigation/exit functions, with
synthetic input and date-editor seams (they do not emulate the whole firmware).
`render_probe.py` also executes the actual heatmap render function with synthetic
font/SDK draw-call spies to assert one grid, cell bounds, compact non-overlapping
labels and truthful empty/invalid states. It is run by the routine in both modes;
it is not a screenshot or a substitute for the real-font SDL captures.

For full input qualification, supply an **already-built, source/hash-bound heatmap
SDL launch fixture**, not an arbitrary historical simulator:

```
READING_HEATMAP_SIMULATOR=/absolute/path/heatmap-simulator \
READING_HEATMAP_SIM_OUTPUT=/absolute/path/input-evidence \
python3 test/crosscub/run_all.py --heatmap-only --output /absolute/path/suite-evidence
```

The optional simulator suite exercises the real HalGPIO recognizer, MappedInputManager,
Activity loop and render: hardware navigation, swipe, direct page-dot touch, optional
All Devices fourth page, landscape, date-editor entry/Back, hardware Back, touch header
Back and Confirm exit, first-page swipe exit, no-book entry and aggregate dot touch.
It is launched in `unshare -n`; synthetic input/state is labelled. The supplied
binary and candidate source hashes must match its sibling `render-inputs.json`.
The fixture must expose only launch/access/telemetry seams, not replace the UI renderer.
A missing full fixture does not silently imply this integration was exercised.

Current full-width/vertical-row layout evidence, exact compiler/link commands,
input hashes and fresh genuine real-font SDL captures are retained under
`/root/.hermes/task-state/crosscub-owner-next-release/stats/layout-refinement-round1/`.
The earlier `/root/github/.host-data/crosssmudge/reading-stats-heatmap/simplified/`
captures show the superseded three-column cards and are not new-layout proof.
This generation captures the native full panel at 480x800 / 800x480 without image
scaling. Portable geometry and normal/ASan/UBSan regressions pass independently
of native visual acceptance. The existing production `drawIcon` orientation defect
still omits the open-book icon in the one-book missing-cover landscape fixture;
the native placeholder's own size gate omits its icon in smaller rows normally.
No icons, renderer, SDK or assets were patched to conceal this defect. See the
new report for the exact renderer fix proposal requiring separate authorization.
Target compilation, full Activity boot/input and physical-device behavior remain
separate acceptance gates.
