# logsquirl-logcat — Android Logcat Plugin for LogSquirl

[![CI Build](https://github.com/64x-lunicorn/LogSquirl-Logcat/actions/workflows/ci-build.yml/badge.svg)](https://github.com/64x-lunicorn/LogSquirl-Logcat/actions/workflows/ci-build.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/License-GPL--3.0--or--later-blue.svg)](LICENSE)
[![Platforms](https://img.shields.io/badge/Platforms-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

A [LogSquirl](https://github.com/64x-lunicorn/LogSquirl) plugin that streams
Android `logcat` output from ADB-connected devices directly into LogSquirl tabs.
Supports **multiple parallel devices**, optional **save-to-file**, and provides
a dialog for device selection and session control.

This plugin also serves as a **reference implementation / sample plugin** for
the LogSquirl Plugin SDK.  Every design decision is documented, and the code
is heavily commented to help you build your own plugins.

## Features

- **Device Discovery** — Automatic ADB device scanning with one-click refresh
- **Multi-Device** — Capture logcat from multiple devices simultaneously
- **Live Tailing** — Each device opens in its own LogSquirl tab with follow mode
- **Save to File** — Optionally persist logcat output to a `.log` file
- **ADB Auto-Detection** — Finds `adb` via `ANDROID_HOME`, `ANDROID_SDK_ROOT`,
  system `PATH`, or well-known platform-specific paths; configurable override
  via the plugin settings dialog
- **Persistent Logs** — Captured output remains visible in LogSquirl after
  stopping a session
- **Cross-Platform** — Works on macOS, Linux, and Windows
- **Use LogSquirl Filters** — No built-in filtering; leverage LogSquirl's
  powerful regex search and highlighters on the raw logcat output

## Prerequisites

- **LogSquirl** ≥ 26.03 with the plugin system enabled
- **Qt6** (Core + Widgets) — same version LogSquirl was built with
- **CMake** ≥ 3.16
- **ADB** (Android Debug Bridge) — part of the
  [Android SDK Platform-Tools](https://developer.android.com/tools/releases/platform-tools)
- A C++17-capable compiler (GCC ≥ 9, Clang ≥ 14, MSVC ≥ 19.29)

## Build

```bash
# Clone
git clone https://github.com/64x-lunicorn/LogSquirl-Logcat.git
cd LogSquirl-Logcat

# Configure
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# If Qt6 is not in PATH (e.g. Homebrew on macOS):
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt6)"

# Build
cmake --build build

# The shared library is in build/:
#   macOS:   build/liblogsquirl_logcat.dylib
#   Linux:   build/liblogsquirl_logcat.so
#   Windows: build/logsquirl_logcat.dll
```

### Running Tests

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

## Install

Copy the plugin library **and** `plugin.json` into one of LogSquirl's
plugin search directories:

| Platform | Plugin Directory |
|----------|-----------------|
| macOS    | `~/Library/Application Support/logsquirl/plugins/io.github.logsquirl.logcat/` |
| Linux    | `~/.local/share/logsquirl/plugins/io.github.logsquirl.logcat/` |
| Windows  | `%APPDATA%/logsquirl/plugins/io.github.logsquirl.logcat/` |

```bash
# Example for macOS:
DEST="$HOME/Library/Application Support/logsquirl/plugins/io.github.logsquirl.logcat"
mkdir -p "$DEST"
cp build/liblogsquirl_logcat.dylib "$DEST/"
cp plugin.json "$DEST/"
```

Or use `cmake --install`:

```bash
cmake --install build --prefix "$HOME/.local"
```

After installing, restart LogSquirl (or re-scan via *Plugins → Manage Plugins…*).

## Usage

1. **Enable the plugin** in *Plugins → Manage Plugins…* — check
   "Android Logcat" and click OK.  (On first run, the plugin is
   auto-enabled if no other plugins are configured.)

2. Open the logcat dialog via **Plugins → Android Logcat…**

3. In the dialog:

   - **Device dropdown** — Select a connected ADB device.
   - **Refresh** — Re-scan for devices.
   - **Start** — Begin capturing logcat for the selected device.
     A new tab opens in LogSquirl with live output in follow mode.
   - **Stop** — Stop the capture for the selected device.
     The tab remains open with all captured output preserved.
   - **Stop All** — Stop all active sessions.

4. **Save to file** — Check "Save to file" and enter/browse a `.log` file
   path. Output is also written to this file alongside the LogSquirl tab.

5. **Multiple devices** — Select another device, click Start again.
   Each device gets its own tab.

6. **Configure ADB path** — *Plugins → Manage Plugins…* → select plugin →
   Configure.  Enter the full path to the `adb` executable (or leave empty
   for auto-detection).

## Architecture

```mermaid
graph TD
    subgraph LogSquirl Host
        H[MainWindow]
    end

    subgraph Plugin — C ABI Boundary
        P[plugin.cpp<br/>get_info / init /<br/>shutdown / configure]
        DW[DeviceWidget<br/>QDialog — session control]
        AP1[AdbProcess #1<br/>emulator-5554]
        AP2[AdbProcess #2<br/>R5CR10XXXXX]
    end

    H -- register_menu_action --> P
    P -- creates --> DW
    DW -- manages --> AP1
    DW -- manages --> AP2
    AP1 -- writes --> TF1[Temp File #1]
    AP2 -- writes --> TF2[Temp File #2]
    TF1 -- open_file follow=1 --> H
    TF2 -- open_file follow=1 --> H
```

### Data Flow

```mermaid
sequenceDiagram
    participant User
    participant DW as DeviceWidget
    participant AP as AdbProcess
    participant ADB as adb logcat
    participant TF as Temp File
    participant LS as LogSquirl Tab

    User->>DW: Click Start
    DW->>AP: start()
    AP->>ADB: spawn adb -s SERIAL logcat
    AP->>TF: create temp file
    AP-->>DW: open_file(tempPath, follow=1)
    DW-->>LS: New tab opens
    loop Streaming
        ADB->>AP: stdout lines
        AP->>TF: write + flush
        LS->>TF: tail/follow reads
    end
    User->>DW: Click Stop
    DW->>AP: preserveTempFile() + stop()
    AP->>ADB: SIGTERM
    Note over LS: Tab stays open with captured output
```

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Plugin type = UI** | The DataSource type is limited to 1 stream per plugin. UI type allows managing multiple independent streams. |
| **open_file() instead of push_line()** | Each device writes to its own temp file. The host opens each file with follow/tail mode → one tab per device. |
| **register_menu_action()** | Plugin opens a QDialog from the Plugins menu rather than embedding a widget in the status bar. Cleaner separation. |
| **QProcess for ADB** | Cross-platform, integrates with Qt event loop, no need for threading. |
| **No built-in filtering** | LogSquirl's regex search and highlighters are more powerful than any filter we could build. |
| **Library name without extension** | `plugin.json` uses `"library": "logsquirl_logcat"` — QLibrary resolves the platform suffix automatically. |

## Project Structure

```
logsquirl-logcat/
├── CMakeLists.txt              # Standalone build — finds Qt6, builds shared lib
├── plugin.json                 # Plugin manifest (cross-platform)
├── LICENSE                     # GPL-3.0-or-later
├── README.md                   # This file
├── CHANGELOG.md                # Release history
├── .gitignore
├── include/
│   └── logsquirl_plugin_api.h  # Vendored SDK header (MIT license)
├── src/
│   ├── plugin.h                # Global state shared across translation units
│   ├── plugin.cpp              # C ABI entry points (get_info, init, shutdown)
│   ├── adbprocess.h            # ADB discovery + per-device QProcess wrapper
│   ├── adbprocess.cpp
│   ├── devicewidget.h          # Qt6 session dialog (QDialog)
│   └── devicewidget.cpp
├── tests/
│   ├── CMakeLists.txt          # Catch2 test setup
│   ├── tests_main.cpp          # QApplication + Catch2 runner
│   ├── parsedevicelist_test.cpp
│   ├── plugininfo_test.cpp
│   └── adbprocess_test.cpp
└── docs/
    └── DEVELOPER_GUIDE.md      # How to use this as a plugin template
```

## Using This as a Plugin Template

This plugin is designed to be a starting point for your own LogSquirl plugins.
See [docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md) for a step-by-step
guide on how the Plugin SDK works, annotated code walkthroughs, and tips for
building your own plugins.

**Quick start to fork this as a template:**

1. Copy this directory
2. Rename the library in `CMakeLists.txt`
3. Update `plugin.json` with your plugin's identity
4. Modify `plugin.cpp` entry points
5. Replace `AdbProcess` + `DeviceWidget` with your own logic
6. Build and install

## License

This plugin is licensed under the **GNU General Public License v3.0 or later**
(GPL-3.0-or-later).

The vendored `include/logsquirl_plugin_api.h` header is licensed under the
**MIT License** to allow plugins of any license to use the LogSquirl Plugin SDK.
