# Build reliability and bounded timing

Routine CI keeps firmware, analyzer and portable policy independent. The
`portable-policy` job runs the docs/workflow and synthetic OTA suites without
resolving firmware packages or checking out the SDK. Its result is mandatory in
Test Status, including main pushes. Firmware evidence staging, encryption and
31-day retention still run with `always()` after failed compilation.

## Mutable PlatformIO cache

`pio_cache_identity.py` identifies the patched framework/package state by schema,
Core/Python contract, runner OS/architecture, committed SDK gitlink, parsed INI
configuration, partition map and script/data hashes. Only the three release
`version` values are excluded; other configuration and patcher changes invalidate
the key. There is no prefix fallback and no cached `penv`. Analyzer and firmware
caches stay separate. Cold qualification bypasses this cache.

## Analyzer debt boundary

`scripts/cppcheck-baseline.json` records 32 exact diagnostic tuples from owner run
45418119951 at source aa3ee6b71a6ed8b65f527c3cf5d681724ba47ec4 and SDK
634a3f8c067a3714a6a7c622015138af096ab5d2. Each tuple includes the complete
source-file SHA256. This is explicit existing debt, not a finding repair or a
claim that the analyzer is clean. The single warning tuple in the SDK's
`publication-page.h:201` recurred 51 times in that run.

Baseline matches require authenticated raw parser category/ID/location, exact
path/line/check/severity, and unchanged source bytes inside the project. New
findings, changed files, errors, malformed records, incomplete tools and command
failures remain blocking. The existing two informational coverage notices remain
report-only. Summaries count baseline matches separately; raw diagnostics stay
inside recipient-encrypted evidence. Never add wildcard/check-wide suppression or
automatically regenerate the baseline on CI. Review source and new private
analyzer evidence before any explicit baseline revision.

## Opt-in nonpublishing timing

Manual CI has `build_jobs` choices `1` (default/reference) and `2`. Cold
qualification refuses `2`; stable release remains `-j1`. Compare the same source,
environment and cache state on the same runner class before changing defaults.
`sticky-evidence/build-resources.txt` records worker count, GNU time elapsed
seconds, maximum **individual-process** RSS in KiB and exit status even when the
compiler fails. GNU time does not measure the sum of simultaneous compiler RSS;
use runner/cgroup peak telemetry as well before accepting a parallel memory
budget. Measurements remain encrypted with the build evidence. No speedup or
memory-safety claim is established by adding this control; hosted serial/parallel
measurements and hardware qualification are still outstanding.

## Release identity and recovery

Release admission runs before dependency installation and synthetic regression
work. A confirmed already-published tag is a successful no-op; matching drafts
are detected by bounded authenticated enumeration even if tag lookup returns
404. New release creation retains the API release ID, uploads to that exact ID,
then verifies owner/source/state, asset identity/digest and downloaded bytes.
Draft `untagged-*` URLs are allowed only before publication; stable read-back
requires the canonical tag URL and exact `/latest` ID.

An explicitly authorized CI recovery may pass `--ci-recover-id <positive-id>`
with `--ci-upload <qualified-existing-bin>` and owner `--publish` (or the existing
owner publication input). It verifies the existing draft and newer version,
then publishes that ID only: no rebuild, asset upload/replacement, or tag repair.
The ordinary workflow intentionally does not enable automatic recovery. Existing
owner/source/main/version gates still apply. Mocked transport tests do not
qualify live GitHub publication or device boot.
