# Changelog

## [1.2.2]

### Fixed

- Include the package-local keypad helper in Store installations. Preserve older-firmware fallback rendering; native key styling requires firmware support.

## [1.2.0] - 2026-11-15

### Changed

- Use tested CrossPlay Lua gameplay on Sticky under the existing app ID; retain the exact original game and saves on all other devices and older firmware.

### Added

- Bounded, readback-verified two-slot saves, legacy save preservation, source-oracle and runtime tests.

### Fixed

- Preserve original modules/assets and missing-device fallback; isolate Sticky replacement dependencies.

- Reject invalid dictionary guesses and preserve daily results across interrupted writes.
