# ============================================================================
# logsquirl-logcat — Android Logcat plugin for LogSquirl
#
# This is a standalone CMake project.  It does NOT depend on the LogSquirl
# build tree — only on the vendored logsquirl_plugin_api.h header (MIT).
#
# Build:
#   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# The output is a shared library (liblogsquirl_logcat.dylib / .so / .dll)
# that can be installed alongside its plugin.json into one of LogSquirl's
# plugin search directories.
# ============================================================================
cmake_minimum_required(VERSION 3.16)

project(logsquirl_logcat
    VERSION   0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Find Qt6 ──────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

# ── Plugin shared library ─────────────────────────────────────────────────
add_library(logsquirl_logcat SHARED
    src/plugin.cpp
    src/adbprocess.cpp
    src/devicewidget.cpp
)

# Qt MOC needs to know about our Q_OBJECT classes
set_target_properties(logsquirl_logcat PROPERTIES
    AUTOMOC ON
    # Let CMake choose the correct prefix/suffix per platform:
    #   macOS:   liblogsquirl_logcat.dylib
    #   Linux:   liblogsquirl_logcat.so
    #   Windows: logsquirl_logcat.dll
    # The plugin.json uses "logsquirl_logcat" (no extension) so that
    # QLibrary can resolve the platform-correct name automatically.
    POSITION_INDEPENDENT_CODE ON
)

# Default symbol visibility: hidden.  Only LOGSQUIRL_PLUGIN_EXPORT symbols
# are visible (matches the __attribute__((visibility("default"))) macro).
if(NOT WIN32)
    target_compile_options(logsquirl_logcat PRIVATE -fvisibility=hidden)
endif()

target_include_directories(logsquirl_logcat
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"   # Vendored SDK header
        "${CMAKE_CURRENT_SOURCE_DIR}/src"        # Internal headers
)

target_link_libraries(logsquirl_logcat
    PRIVATE
        Qt6::Core
        Qt6::Widgets
)

# ── Install rule ──────────────────────────────────────────────────────────
# Copies the library and manifest into a directory that can be placed
# inside any of LogSquirl's plugin search paths.
include(GNUInstallDirs)

set(PLUGIN_INSTALL_DIR
    "${CMAKE_INSTALL_DATADIR}/LogSquirl/plugins/io.github.logsquirl.logcat"
    CACHE PATH "Plugin installation directory"
)

install(TARGETS logsquirl_logcat LIBRARY DESTINATION "${PLUGIN_INSTALL_DIR}")
install(FILES   plugin.json              DESTINATION "${PLUGIN_INSTALL_DIR}")

# ── Tests (opt-in) ────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
