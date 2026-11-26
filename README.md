# Crosscub

**An e-reader and SD-installable Lua app platform for Seeed Studio reTerminal Sticky.**

Crosscub is a [CrossInk](https://github.com/uxjulia/CrossInk)-based e-reader firmware and Lua app platform adapted for the **Seeed Studio reTerminal Sticky**. CrossInk is the project upstream; CrossSmudge is part of its earlier history, not the current upstream. Crosscub preserves the reader, typography, application runtime and existing settings/save locations while developing an owner-controlled firmware and app distribution route. The current development branch also integrates [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) directly, as detailed below, and uses the [FreeInk SDK](https://freeink.org/) for hardware support.

- **Hardware:** reTerminal Sticky, ESP32-S3, 16 MiB flash, 8 MiB PSRAM, touchscreen e-paper.
- **Reading:** EPUB library and reading progress, configurable typography, reading statistics, bookmarks, reading aids and inherited transfer/sync features.
- **Applications:** standalone Lua 5.4 packages, on-device App Store and browser-based application management.
- **Release status:** stable Crosscub **1.6.5.5 remains unchanged**. This direct-CrossPoint migration is experimental and not release-ready; simulator screenshots below are historical evidence, not captures or device qualification of this candidate.

## Direct-upstream migration

The technical foundation is now pinned directly to CrossPoint develop
`9ac3475733e677b7f61ba3f6e8ce41ce1b9b2967`, newer development code than upstream
stable 2.0.0. CrossInk `9914146eeae7b46b300f475a16c32426fc02ec1f` is selectively
adopted; owner behavior is retained from Crosscub
`818b0befac4d9249c7abded67fa980187c4b41af`. This is not a fresh product rename
or a claim that every upstream feature is finished. SDK is pinned to
`634a3f8c067a3714a6a7c622015138af096ab5d2`, platform 55.03.311, Core 6.1.19
(outer and nested).

[Feature/readiness ledger](SCOPE.md) distinguishes implemented source, pending
integration and target/device/security gates. New BLE, plugins, OPDS2, runtime
vector fonts, richer EPUB layout, library and timezone interfaces coexist with
owner Lua/API7, all 41 Store apps, Browser 0.3.5, fresh-consent 30MB AirDrop,
reading heatmap, safe file actions and owner OTA. No combined firmware build,
physical acceptance or stable promotion is claimed here. PR/manual CI retains
private BIN+ELF+log+source/SDK/toolchain/image-validation evidence; it cannot publish.

## Install apps

A compatible host firmware is required once. After that, ordinary Lua app updates do **not** require rebuilding or flashing firmware.

1. Connect Sticky to Wi-Fi and open **Applications → App Store**.
2. Select an app and install or update it. The configured owner store has **41 apps**.
3. Alternatively, merge an app's complete folder onto the SD card at `/.crosssmudge/applications/<app_id>/`. Include every runtime module and asset listed in its catalog record. Preserve existing saves; do not delete app/save directories for a routine update.

The web application manager is available at `http://<device-ip>/applications` while the device's web server is running. It supports app-folder/ZIP installation and management. These are local-device URLs, not a hosted Crosscub service.

Do not infer Store completeness from a source-catalog count; preserve the complete 41-app owner store. The live store is `tobiasweissert/Crosscub`, branch `integration/sticky-test-all`; do not replace its catalog with the smaller source inventory. App API requirements vary. Browser compatibility must be checked against its actual manifest and Lua/API7 host; the historical gallery describes an older Browser. Assisted mode requires compatible public-mode firmware; availability in a store is not proof that an older host supports it.

## Unified 23-app redesign candidate

[Current redesigned apps, per-app versions and safe manual-overlay instructions](docs/app-redesign.md) · [46-frame portrait/landscape atlas](docs/app-redesign-atlas.html) · [Verified local status](docs/app-redesign-status.md). The linked redesign receipts describe an earlier local candidate, not acceptance of this migration; publication status must be checked separately. Physical-device acceptance remains pending. **Back up the SD first; use copy-only overlay, not App Store Update for this candidate.** The current installer can remove an existing member before a failed replacement. The original41 images/provenance remain unchanged in their original paths;18 non-target thumbnails below are historical.

## App screenshot gallery

These are **actual SDL2 simulator captures**, not AI images, illustrations or reconstructed test-harness renders. The real firmware activity manager, Lua runtime, renderer, fonts and published app assets produced the pixels. BMP output from the SDL renderer was converted losslessly to PNG.

**Mixed gallery, explicitly labelled:** the23 redesigned thumbnails use independently reviewed, locally integrated extracted-package code and versions. Publication and physical-device acceptance remain pending. The other18 thumbnails retain the historical41-app preview unchanged and are not new captures of current canonical code. [Original gallery provenance](docs/app-gallery.md) remains historical; [new redesign provenance and qualifications](docs/app-redesign.md) describe the replacements.

All23 replacement thumbnails are native476×796 portraits. The linked redesign atlas also includes all23 native796×476 landscapes, with the actual two-pixel bezel crop and no screenshot padding. Meaningful gameplay/privacy/clue phases were entered using real inputs; values are dynamic, not copied from PDF examples.

All captures run with app networking disabled and in an isolated Linux network namespace. Weather, Browser, Hacker News and Trivia show their real offline/configuration/startup state—not live forecasts, fetched articles or provider answers. No NYT website, headline or image was fetched or used. Screenshots demonstrate the named state only; they are not exhaustive gameplay, touch, radio, SD, sleep/wake or physical e-ink qualification.

<table>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/flashcards.png"><img src="docs/images/apps-redesigned/flashcards.png" width="240" alt="Flashcards — real Sticky SDL simulator capture" /></a><br /><strong>Flashcards</strong><br /><sub>0.1.2 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/habits.png"><img src="docs/images/apps-redesigned/habits.png" width="240" alt="Habits — real Sticky SDL simulator capture" /></a><br /><strong>Habits</strong><br /><sub>0.1.2 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/pomodoro.png"><img src="docs/images/apps-redesigned/pomodoro.png" width="240" alt="Pomodoro — real Sticky SDL simulator capture" /></a><br /><strong>Pomodoro</strong><br /><sub>0.1.1 · redesign candidate</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/weather.png"><img src="docs/images/apps-redesigned/weather.png" width="240" alt="Weather — real Sticky SDL simulator capture" /></a><br /><strong>Weather</strong><br /><sub>0.1.1 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/browser.png"><img src="docs/images/apps-redesigned/browser.png" width="240" alt="Browser — real Sticky SDL simulator capture" /></a><br /><strong>Browser</strong><br /><sub>0.3.3 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/dice.png"><img src="docs/images/apps-redesigned/dice.png" width="240" alt="Dice Roller — real Sticky SDL simulator capture" /></a><br /><strong>Dice Roller</strong><br /><sub>1.0.2 · redesign candidate</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/lifecounter.png"><img src="docs/images/apps-redesigned/lifecounter.png" width="240" alt="Life Counter — real Sticky SDL simulator capture" /></a><br /><strong>Life Counter</strong><br /><sub>1.0.2 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/rosary.png"><img src="docs/images/apps-redesigned/rosary.png" width="240" alt="Holy Rosary — real Sticky SDL simulator capture" /></a><br /><strong>Holy Rosary</strong><br /><sub>1.0.2 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/dailyoffice.png"><img src="docs/images/apps-redesigned/dailyoffice.png" width="240" alt="Divine Worship — real Sticky SDL simulator capture" /></a><br /><strong>Divine Worship</strong><br /><sub>1.0.2 · redesign candidate</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps/wordle.png"><img src="docs/images/apps/wordle.png" width="240" alt="Wordle — real Sticky SDL simulator capture" /></a><br /><strong>Wordle</strong><br /><sub>1.2.1</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/2048.png"><img src="docs/images/apps-redesigned/2048.png" width="240" alt="2048 — real Sticky SDL simulator capture" /></a><br /><strong>2048</strong><br /><sub>1.1.1 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/sudoku.png"><img src="docs/images/apps/sudoku.png" width="240" alt="Sudoku — real Sticky SDL simulator capture" /></a><br /><strong>Sudoku</strong><br /><sub>1.1.2</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/blackjack.png"><img src="docs/images/apps-redesigned/blackjack.png" width="240" alt="Blackjack — real Sticky SDL simulator capture" /></a><br /><strong>Blackjack</strong><br /><sub>1.0.2 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/blockdrop.png"><img src="docs/images/apps-redesigned/blockdrop.png" width="240" alt="Block Drop — real Sticky SDL simulator capture" /></a><br /><strong>Block Drop</strong><br /><sub>1.0.3 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/codex.png"><img src="docs/images/apps-redesigned/codex.png" width="240" alt="Codex: Ink &amp; Iron — real Sticky SDL simulator capture" /></a><br /><strong>Codex: Ink &amp; Iron</strong><br /><sub>1.0.2 · redesign candidate</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/watertracker.png"><img src="docs/images/apps-redesigned/watertracker.png" width="240" alt="Water Tracker — real Sticky SDL simulator capture" /></a><br /><strong>Water Tracker</strong><br /><sub>1.0.3 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/stopwatch.png"><img src="docs/images/apps-redesigned/stopwatch.png" width="240" alt="Stopwatch — real Sticky SDL simulator capture" /></a><br /><strong>Stopwatch</strong><br /><sub>1.0.3 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/dashboard.png"><img src="docs/images/apps-redesigned/dashboard.png" width="240" alt="Desk Stand — real Sticky SDL simulator capture" /></a><br /><strong>Desk Stand</strong><br /><sub>1.0.4 · redesign candidate</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/hourglass.png"><img src="docs/images/apps-redesigned/hourglass.png" width="240" alt="Hourglass — real Sticky SDL simulator capture" /></a><br /><strong>Hourglass</strong><br /><sub>1.0.3 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/solitaire.png"><img src="docs/images/apps/solitaire.png" width="240" alt="Solitaire — real Sticky SDL simulator capture" /></a><br /><strong>Solitaire</strong><br /><sub>1.1.1</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/chess.png"><img src="docs/images/apps/chess.png" width="240" alt="Chess — real Sticky SDL simulator capture" /></a><br /><strong>Chess</strong><br /><sub>1.1.2</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps/cp-battleship.png"><img src="docs/images/apps/cp-battleship.png" width="240" alt="Battleship — real Sticky SDL simulator capture" /></a><br /><strong>Battleship</strong><br /><sub>0.1.5</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/cp-calculator.png"><img src="docs/images/apps/cp-calculator.png" width="240" alt="Calculator — real Sticky SDL simulator capture" /></a><br /><strong>Calculator</strong><br /><sub>0.1.4</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/cp-checkers.png"><img src="docs/images/apps/cp-checkers.png" width="240" alt="Checkers — real Sticky SDL simulator capture" /></a><br /><strong>Checkers</strong><br /><sub>0.1.4</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps/cp-connectfour.png"><img src="docs/images/apps/cp-connectfour.png" width="240" alt="Connect Four — real Sticky SDL simulator capture" /></a><br /><strong>Connect Four</strong><br /><sub>0.1.4</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/cp-forehead.png"><img src="docs/images/apps-redesigned/cp-forehead.png" width="240" alt="Forehead — real Sticky SDL simulator capture" /></a><br /><strong>Forehead</strong><br /><sub>0.1.5 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/cp-hearts.png"><img src="docs/images/apps-redesigned/cp-hearts.png" width="240" alt="Hearts — real Sticky SDL simulator capture" /></a><br /><strong>Hearts</strong><br /><sub>0.1.5 · redesign candidate</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps/cp-hex.png"><img src="docs/images/apps/cp-hex.png" width="240" alt="Hex — real Sticky SDL simulator capture" /></a><br /><strong>Hex</strong><br /><sub>0.1.5</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/cp-insider.png"><img src="docs/images/apps/cp-insider.png" width="240" alt="Insider — real Sticky SDL simulator capture" /></a><br /><strong>Insider</strong><br /><sub>0.1.4</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/cp-knucklebones.png"><img src="docs/images/apps/cp-knucklebones.png" width="240" alt="Knucklebones — real Sticky SDL simulator capture" /></a><br /><strong>Knucklebones</strong><br /><sub>0.1.4</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps/cp-minesweeper.png"><img src="docs/images/apps/cp-minesweeper.png" width="240" alt="Minesweeper — real Sticky SDL simulator capture" /></a><br /><strong>Minesweeper</strong><br /><sub>0.1.5</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/cp-notes.png"><img src="docs/images/apps/cp-notes.png" width="240" alt="Notes — real Sticky SDL simulator capture" /></a><br /><strong>Notes</strong><br /><sub>0.1.4</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/cp-wavelength.png"><img src="docs/images/apps-redesigned/cp-wavelength.png" width="240" alt="Wavelength — real Sticky SDL simulator capture" /></a><br /><strong>Wavelength</strong><br /><sub>0.1.5 · redesign candidate</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps/cp-yahtzee.png"><img src="docs/images/apps/cp-yahtzee.png" width="240" alt="Yahtzee — real Sticky SDL simulator capture" /></a><br /><strong>Yahtzee</strong><br /><sub>0.1.4</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/cp-jaipur.png"><img src="docs/images/apps/cp-jaipur.png" width="240" alt="Jaipur — real Sticky SDL simulator capture" /></a><br /><strong>Jaipur</strong><br /><sub>0.1.4</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/cp-seasalt.png"><img src="docs/images/apps/cp-seasalt.png" width="240" alt="Sea Salt — real Sticky SDL simulator capture" /></a><br /><strong>Sea Salt</strong><br /><sub>0.1.4</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps/cp-toybattle.png"><img src="docs/images/apps/cp-toybattle.png" width="240" alt="Toy Battle — real Sticky SDL simulator capture" /></a><br /><strong>Toy Battle</strong><br /><sub>0.1.5</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/cp-murdle.png"><img src="docs/images/apps-redesigned/cp-murdle.png" width="240" alt="Murdle — real Sticky SDL simulator capture" /></a><br /><strong>Murdle</strong><br /><sub>0.1.5 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps/cp-go.png"><img src="docs/images/apps/cp-go.png" width="240" alt="Go — real Sticky SDL simulator capture" /></a><br /><strong>Go</strong><br /><sub>0.1.4</sub></td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/cp-hackernews.png"><img src="docs/images/apps-redesigned/cp-hackernews.png" width="240" alt="Hacker News — real Sticky SDL simulator capture" /></a><br /><strong>Hacker News</strong><br /><sub>0.1.4 · redesign candidate</sub></td>
    <td align="center" width="33%"><a href="docs/images/apps-redesigned/cp-trivia.png"><img src="docs/images/apps-redesigned/cp-trivia.png" width="240" alt="Trivia — real Sticky SDL simulator capture" /></a><br /><strong>Trivia</strong><br /><sub>0.1.4 · redesign candidate</sub></td>
  </tr>
</table>

Full-resolution images are linked above. See [capture provenance and coverage](docs/app-gallery.md) and the [machine-readable manifest](docs/images/apps/capture-manifest.json) for each app's state, source hashes, image hash and dimensions.

## Reading and typography

Crosscub preserves the inherited reading experience rather than replacing it with an app launcher:

- Lexend Deca and Bitter reading fonts, Inter UI typography and supplemental/fallback glyphs.
- Reading statistics, finished-book tracking, progress, bookmarks and configurable sleep screens.
- EPUB layout enhancements including underlines, rules and simple tables.
- Focus-reading aids, guide dots and configurable controls.
- Existing book transfer, OPDS and local synchronization paths, subject to the selected firmware's compatibility and hardware qualification.

Legacy `.crosspoint`, `.crosssmudge` and `.smudge` paths remain intentional compatibility identifiers. They do not indicate a different active product name. Existing custom boot/sleep images and preferences remain supported.

## Firmware installation and updates

The accepted Crosscub firmware is integrated on `main` in [tobiasweissert/Crosscub](https://github.com/tobiasweissert/Crosscub). The previous repository name redirects here. Development continues on separate feature branches. Stable Crosscub 1.6.5.5 is unchanged; this branch is not a replacement release.

For a verified application image, back up the SD card, open **Settings → System → SD Card Firmware Update**, select the supplied `.bin` and confirm. An application update is not a factory image. Do not format SD or NVS: preserve books, settings, progress, apps and saves. Consult the [installation guide](docs/installation.md) before changing firmware; USB/factory procedures depend on the exact artifact and installed version.

OTA remains in Settings, but accepts only the owner's `crosscub-v…` tags, `crosscub-sticky-v….bin` assets, pinned SHA-256 and Crosscub application identity. It does not select upstream product firmware. Only a separately owner-qualified release may be offered as an update; this branch must not become /latest.

## File actions on the development branch

`feat/airdrop-original-names-file-actions` adds native firmware features; an App Store-only update cannot add them to an older host.

- AirDrop saves to `/Inbox/<original filename>` with the original safe UTF-8 spelling, case and extension. No postfix is added. Existing destination files are refused, never silently overwritten.
- In **Browse Files**, select a regular file and choose **Actions**; the existing long-press/hold route also remains available. **Delete** requires an explicit confirmation, with Cancel initially selected.
- **Move** opens a destination picker. Navigate to the folder and use **Select folder** (or hardware Right). The original filename and book state are carried; destination collisions are refused. Cancel leaves the file in place. Recursive folder deletion is not offered by this feature.
- An uncertain SD or metadata operation stops further Files mutations for the session. Checked app-state persistence may remain read-only across reopening until its retained recovery copies are inspected. Stop using the card and preserve it for inspection; restarting is not evidence that an uncertain operation was repaired. Host fault tests are not physical SD power-loss guarantees.

## Develop an app

An app is a folder with a manifest, Lua entry point and optional assets:

```text
my_app/
├── manifest.json
├── main.lua
└── icon.raw
```

```json
{
  "id": "my_app",
  "name": "My App",
  "version": "1.0.0",
  "author": "YourGitHubUsername",
  "description": "A small e-paper app.",
  "entry": "main.lua"
}
```

Use the [application developer guide](apps/README.md) for the actual drawing, lifecycle, input, persistence and network API. `icon.raw` is a 32×32, 128-byte monochrome icon encoded with the production converter; package all nested glyph/sprite files and retain their licenses. Touchscreen controls should share drawing and hit-test geometry.

Sticky normally has a **2 MiB per-app Lua heap**. The explicitly declared Browser capability has **4 MiB**, subject to allocator reserve/admission checks. These limits do not guarantee allocation success. `SMUDGE_X3_CONSTRAINTS=1` is an optional legacy 75 KiB simulator stress alias, not Sticky's production budget.

### Check changes

```sh
python3 test/crosscub/test_product.py
python3 test/crosscub/run_preservation.py --group apps --output /path/to/new-evidence/apps
# Full host suite also needs the documented existing read-only dependencies:
python3 test/crosscub/run_all.py --output /path/to/new-evidence/full
```

Host tests and simulator captures complement, but do not replace, target compilation and physical-device testing.

### Submit and publish separately

Prepare source changes against `main`, preserving IDs, saves and license notices. Include the complete file list in the source catalog and bump both manifest and catalog versions when publishing an update. A merged source change does not automatically publish an app. Updating the configured owner store requires a separately authorized per-app composition into `integration/sticky-test-all`, preserving all 41 entries and unrelated metadata.

## Build from source

Use Git with recursive submodules and the PlatformIO toolchain pinned by the selected revision's CI (Core 6.1.19 in this snapshot), not an arbitrary latest replacement.

```sh
git clone https://github.com/tobiasweissert/Crosscub.git
cd Crosscub
git submodule update --init --recursive
pio run -e sticky
```

| Environment | Purpose |
| --- | --- |
| `sticky` | reTerminal Sticky application firmware |
| `sticky-simulator` | Native SDL2 Sticky simulator |
| `simulator` | Compatibility alias for the Sticky simulator |

The native simulator needs SDL2 development headers/libraries and OpenSSL development support on Linux. This gallery uses isolated compatibility patches and explicit capture automation; see the provenance document rather than assuming an unchanged checkout has been qualified on every desktop platform. A Nix flake/shell is also provided under `nix/`.

## Repository layout

- `src/` — activity manager, reader, application runtime, settings and network UI.
- `apps/` — source apps, manifests, assets and the source catalog.
- `lib/` — EPUB, fonts, parsers, filesystem and platform abstractions.
- `freeink-sdk/` — pinned hardware/display SDK submodule.
- `web/` — device web portal sources, packaged by `scripts/build_web.py`.
- `docs/` — architecture, installation, controls and application guides.
- `scripts/`, `test/` — tooling, product contracts and host regression tests.

## Credits and licenses

- [CrossInk](https://github.com/uxjulia/CrossInk), the project upstream, including Julia (@uxjulia)'s typography, reading statistics and reading aids.
- [CrossSmudge](https://github.com/Mumfee/CrossSmudge) for its historical contributions to Crosscub's earlier foundation.
- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for the original e-reader architecture and EPUB engine.
- [FreeInk SDK](https://freeink.org/) for hardware drivers and display abstractions.
- CrossPlay contributors and the individual app authors, font designers and artwork projects credited in each package's license/provenance files.

The root [LICENSE](LICENSE) covers original CrossPoint code under **MIT**, except where individual files state otherwise. The combined firmware executable uses **GPLv3** because it includes wolfSSL; see [third-party notices and source instructions](licenses/README.md). Inherited/bundled components retain their own licenses and attribution requirements; this README does not relicense them. App screenshots depict the real packaged artwork and fonts, whose component notices remain applicable.
