# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- Log messages and notifications reach LogSquirl as UTF-8, so non-ASCII
  device names and paths are no longer garbled on systems whose local
  8-bit encoding is not UTF-8.
- Lines that adb terminates with `\r\n` no longer keep a stray carriage
  return in the log file.
- A session whose adb cannot be launched is no longer listed as active
  with nothing running: the error is shown once, no tab is opened, and
  no empty log file is left behind.
- Stopping a session no longer shows an "ADB process crashed" error on
  macOS and Linux, where adb ends from the stop signal.
- Starting, stopping and rotating never truncate an existing log file.
  A save path is appended to, so Stop and Start keep the earlier capture;
  generated file names get a `_2`, `_3`, … suffix when a file of that
  name exists, so a rotation within the same second as the start no
  longer wipes the capture it rotates away from.
- A second session is refused instead of writing into the save path of
  one that is still running.
- Wireless devices (`192.168.1.5:5555`) get a valid temporary file name
  on Windows: the serial's `:` is replaced, as it already was for files
  in the log directory.

## [0.3.0] — 2026-04-02

### Added
- **Plugin icon** — added `icon` field to `plugin.json` for display in the
  host Plugin Management dialog.
- **Decentralized registry** — added `releases.json` with per-platform download
  URLs and SHA-256 checksums for all releases.
- **Temp file cleanup** — temporary log files are removed on shutdown.
- **Default log directory** — logs are saved to a configurable default directory.

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
