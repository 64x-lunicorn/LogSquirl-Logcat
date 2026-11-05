# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **A Logcat filter group and a Logcat levels highlighter set.** The filter
  group *Logcat* has *Crashes* (Java and native crashes, tombstones and the
  crash buffer), *ANRs* and *Errors and worse*; the highlighter set *Logcat
  levels* colours fatal, error and warning lines. On LogSquirl 26.12 and
  later they are listed while the plugin is enabled, with nothing to
  import; on an older LogSquirl, import the two files from the release
  archive (#29).

### Changed
- **The icon matches the other LogSquirl plugins.** The log lines stay
  inside the frame the other plugin icons use.
- **Built against the plugin API of LogSquirl 26.12.** LogSquirl 26.11 and
  later start the plugin through `logsquirl_plugin_init_ex()` and tell it
  which of their functions it may use; LogSquirl 26.10 starts it as before,
  so the plugin still runs there.

### Fixed
- **A log file that cannot be written stops the capture.** When the disk is
  full or the log directory's drive goes away, the session stops and says
  which file could not be written, instead of capturing on while every line
  is lost (#32).
- **A log file or directory is a full path.** `~` is the home directory; a
  relative path is refused with a message instead of being created in
  LogSquirl's working directory (#33).
- **Only a regular file is written to.** A save path that is a FIFO, a
  device or a symbolic link is refused at once, so it cannot freeze
  LogSquirl or write elsewhere (#34).
- **A log file the plugin creates is readable by you only** (#38).
- **An error in the plugin no longer ends LogSquirl.** An exception in the
  plugin's start, shutdown, Configure or its menu entry is logged instead
  of escaping into LogSquirl; a start that fails is undone, the sidebar tab
  unregistered before its widget goes.
- **A device serial and the ADB path are shown as they are.** A device can
  report any serial; one that looks like HTML is shown as its literal text
  in the sidebar's session list, never rendered, and so is the ADB path in
  the dialog (#35).
- **adb output without line breaks no longer fills the memory.** When adb,
  or another program set as the ADB path, writes on and on without ending
  a line, every megabyte is written to the log file as a line of its own;
  what it writes to its error output is cut short in LogSquirl's log and
  in the error shown (#36).
- **Stopping all sessions takes two seconds at most.** Stop All, quitting
  LogSquirl and disabling the plugin end every adb at once and wait for
  them together, instead of up to two seconds for each device in turn; an
  adb that does not exit even when killed is left behind rather than
  waited for once more (#37).
- **The Android SDK's adb is found on Windows.** The `adb.exe` in
  `platform-tools` of `ANDROID_HOME` or `ANDROID_SDK_ROOT` is used again,
  rather than whichever adb comes first in the PATH, which may be another
  or an older one (#39).
- **No ADB path is taken from a shared temporary directory.** Before it
  is started by LogSquirl, the plugin reads no configuration at all,
  instead of a `logcat.ini` in the temporary directory, where on Linux any
  local user could have set the adb it runs (#40).
- **A save path given with `~` works throughout.** Rotating such a
  session continues in a new file next to it, instead of failing, and a
  second device given the same `~/…` path is refused instead of writing
  into the same file (#43).

## [0.4.0] — 2026-09-30

### Changed
- **Requires LogSquirl 26.10.0 or later.** This release is built with the Qt of
  LogSquirl 26.10.0 (Qt 6.11.3); an older LogSquirl cannot load it.

### Fixed
- Log messages and notifications reach LogSquirl as UTF-8, so non-ASCII
  device names and paths are no longer garbled on systems whose local
  8-bit encoding is not UTF-8.
- Lines that adb terminates with `\r\n` no longer keep a stray carriage
  return in the log file.
- A session whose adb cannot be launched is no longer listed as active
  with nothing running: the error is shown once, no tab is opened, and
  no empty log file is left behind. An adb that does not start within
  5 seconds is given up on with a message, instead of freezing LogSquirl
  for 30 seconds and failing silently.
- Temporary log files are now removed when LogSquirl quits: those of the
  running sessions and of every session that was stopped, ended or
  rotated away before, whose tabs close with LogSquirl. When the plugin is
  disabled or updated while LogSquirl keeps running, its sessions are
  stopped but every file is kept, since open tabs may still show them.
  0.3.0's cleanup never took effect: each session stopped at shutdown
  reported back that it had ended, and handling that preserved its file,
  while the files of earlier and rotated sessions were never removed at
  all. Save paths and files in the log directory are never removed. Shutdown also rescanned the
  devices once per session, blocking it.
- Temporary log files are now also removed when LogSquirl quits after the
  plugin was disabled and enabled again, or updated, while it kept running.
  The new plugin did not know the files the earlier one had kept for its
  tabs, so they stayed for good. Their directories are now named
  `logsquirl-logcat-<pid>-XXXXXX` after the LogSquirl process, and at quit
  the plugin removes every one with its own process ID. Directories left
  behind by a LogSquirl that no longer runs, e.g. after a crash, are removed
  when the plugin starts; those of a running LogSquirl, links and, on Unix,
  other users' directories are left alone.
- Device discovery no longer freezes LogSquirl: `adb devices` runs in the
  background instead of blocking the UI for up to 10 seconds, twice at
  startup and two or three times per Start or Stop. Starting and stopping
  a session no longer rescan at all. Refreshes requested while a scan is
  running are combined into one more scan after it, the Refresh button
  shows "Scanning…" meanwhile, and changing the ADB path (in the dialog
  or in Plugins → Configure) abandons a running scan for one with the new
  path.
- On Windows, an adb that cannot be started no longer logs a false "adb
  devices timed out" 10 seconds later, and closing the dialog while a
  device scan runs kills the scan instead of leaving it to Qt.
- Stopping a session on Windows no longer freezes LogSquirl for three
  seconds: adb is killed right away, since as a console program it ignores
  the polite request to close.
- The Android Logcat dialog (Plugins menu) stays on top of LogSquirl's
  window, and no longer keeps LogSquirl running when it is open while the
  main window is closed.
- adb's stderr is no longer discarded: its warnings go to the LogSquirl
  log, and when adb exits with an error (device offline, not found,
  unauthorised) the notification says what adb reported.
- Deleting the dialog with sessions still running no longer calls back
  into the half-destroyed dialog.
- Stopping a session no longer shows an "ADB process crashed" error on
  macOS and Linux, where adb ends from the stop signal.
- Starting, stopping and rotating never truncate an existing log file.
  A save path is appended to, so Stop and Start keep the earlier capture;
  generated file names get a `_2`, `_3`, … suffix when a file of that
  name exists, so a rotation within the same second as the start no
  longer wipes the capture it rotates away from.
- A rotation that cannot create its new file no longer leaves the session
  running with its log file closed, which silently dropped all further
  output: the capture continues in the old file and the error is shown.
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

[Unreleased]: https://github.com/64x-lunicorn/LogSquirl-Logcat/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/64x-lunicorn/LogSquirl-Logcat/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/64x-lunicorn/LogSquirl-Logcat/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/64x-lunicorn/LogSquirl-Logcat/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/64x-lunicorn/LogSquirl-Logcat/releases/tag/v0.1.0
