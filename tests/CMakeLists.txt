# ============================================================================
# Unit tests for logsquirl-logcat
#
# Uses Catch2 v2 (BDD style) to match the host project's test conventions.
# ============================================================================

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v2.13.10
)
FetchContent_MakeAvailable(Catch2)

# ── Test executable ───────────────────────────────────────────────────────

add_executable(logsquirl_logcat_tests
    tests_main.cpp
    parsedevicelist_test.cpp
    plugininfo_test.cpp
    adbprocess_test.cpp
    hostapi_test.cpp
    takelines_test.cpp
    sessions_test.cpp
    devicescan_test.cpp
    devicewidget_test.cpp
    devicestore_test.cpp
    devicetile_test.cpp
    sidebarwidget_test.cpp
    deviceroster_test.cpp
    generatelogpath_test.cpp
    plugin_test.cpp
    presets_test.cpp
)

set_target_properties(logsquirl_logcat_tests PROPERTIES
    AUTOMOC ON
)

target_include_directories(logsquirl_logcat_tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"   # logsquirl_plugin_api.h
        "${CMAKE_SOURCE_DIR}/src"       # plugin.h, adbprocess.h, sessions.h, …
)

# Link against the same Qt6 modules and Catch2.
# We also link the plugin sources directly (as an OBJECT library would)
# so tests can exercise internal functions without loading the .dylib.
target_link_libraries(logsquirl_logcat_tests
    PRIVATE
        Catch2::Catch2
        Qt6::Core
        Qt6::Widgets
)

# Compile plugin sources into the test binary so we can test internal
# functions like parseDeviceList() without symbol-export gymnastics.
target_sources(logsquirl_logcat_tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src/plugin.cpp"
        "${CMAKE_SOURCE_DIR}/src/adbprocess.cpp"
        "${CMAKE_SOURCE_DIR}/src/tempdirs.cpp"
        "${CMAKE_SOURCE_DIR}/src/sessions.cpp"
        "${CMAKE_SOURCE_DIR}/src/devicescan.cpp"
        "${CMAKE_SOURCE_DIR}/src/devicewidget.cpp"
        "${CMAKE_SOURCE_DIR}/src/sidebarwidget.cpp"
        "${CMAKE_SOURCE_DIR}/src/deviceroster.cpp"
        "${CMAKE_SOURCE_DIR}/src/devicestore.cpp"
        "${CMAKE_SOURCE_DIR}/src/devicetile.cpp"
        "${CMAKE_SOURCE_DIR}/src/settingsdialog.cpp"
)

# Where presets_test.cpp finds the files the plugin ships.
target_compile_definitions(logsquirl_logcat_tests
    PRIVATE
        LOGCAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ── Register with CTest ──────────────────────────────────────────────────
add_test(NAME logsquirl_logcat_tests COMMAND logsquirl_logcat_tests)
