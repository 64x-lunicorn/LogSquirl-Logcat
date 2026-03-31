# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Plugin icon** — added `icon` field to `plugin.json` for display in the
  host Plugin Management dialog.
- **Decentralized registry** — added `releases.json` with per-platform download
  URLs and SHA-256 checksums for all releases.

## [0.2.0] — 2026-03-26

### Added
- **Sidebar panel** — device selection, start/stop, and active session list
  are now displayed in a dedicated LogSquirl sidebar tab (replaces the
  standalone QDialog). Sessions show rotate (↻) and stop (■) buttons.
- **Log directory** — configurable log save path with automatic filename
  generation (`YYYY-MM-dd_HHmmss_<serial>.log`). The path is persisted
  across sessions.

### Changed
- Plugin UI type now uses `register_sidebar_tab()` instead of
  `register_menu_action()` for the main interface.
- Session list shows only the device name (no line count).

### Fixed
- Session list no longer displays redundant line counts next to the device
  serial number.

## [0.1.0] — 2026-03-25

### Added
- Initial release of the Android Logcat plugin for LogSquirl.
- **Device discovery** — automatic ADB device scanning with refresh.
- **Multi-device support** — capture logcat from multiple devices simultaneously.
- **Live tailing** — each device opens in its own LogSquirl tab with follow mode.
- **Save to file** — optionally persist logcat output to a `.log` file.
- **ADB auto-detection** — finds `adb` via `ANDROID_HOME`, `ANDROID_SDK_ROOT`,
  system `PATH`, and well-known platform-specific paths (Homebrew on macOS, etc.).
- **Configurable ADB path** — override via Plugins → Configure dialog.
- **Persistent logs** — captured output stays visible after stopping a session
  (`preserveTempFile()`).
- **Plugin menu integration** — accessible via Plugins → Android Logcat… (QDialog).
- **Unit tests** — Catch2 test suite covering `parseDeviceList()`, plugin
  metadata, and `AdbProcess` construction.
- **CI/CD** — GitHub Actions workflows for build (Linux, macOS, Windows)
  and tag-triggered releases with per-platform ZIP artifacts.
- **Documentation** — README with Mermaid architecture diagrams, developer
  guide for the Plugin SDK.

[Unreleased]: https://github.com/64x-lunicorn/LogSquirl-Logcat/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/64x-lunicorn/LogSquirl-Logcat/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/64x-lunicorn/LogSquirl-Logcat/releases/tag/v0.1.0
