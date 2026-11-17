cmake_minimum_required(VERSION 3.24)
project(Meridian LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Path to a WebKit Windows-port build tree (contains bin/, lib/, WebKit/Headers/).
# Note: upstream's Windows port is now Skia-based (USE_SKIA=ON), formerly "WinCairo".
set(WEBKIT_BUILD_DIR "D:/wk/WebKit/WebKitBuild/Release" CACHE PATH "WebKit WinCairo build output")
option(MERIDIAN_BUILD_APP "Build the browser (needs WEBKIT_BUILD_DIR)" ON)
option(MERIDIAN_BUILD_TESTS "Build unit tests" ON)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# Sources are UTF-8; without this MSVC reads them in the ANSI codepage and
# string literals like L"’" end up garbled ("Couldnâ€™t").
add_compile_options(/utf-8)

# SQLite comes from WebKit's own vcpkg tree; sqlite3.dll is already part of
# the WebKit runtime we ship, so this adds no new dependency.
set(SQLITE_ROOT "${WEBKIT_BUILD_DIR}/vcpkg_installed/x64-windows-webkit")
find_path(SQLITE_INCLUDE_DIR sqlite3.h PATHS "${SQLITE_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
find_library(SQLITE_LIB sqlite3 PATHS "${SQLITE_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)

# --- platform: Windows integration, no WebKit dependency --------------------
add_library(meridian_platform STATIC
    src/platform/CertificateTrust.cpp
    src/platform/Log.cpp
    src/platform/SystemTheme.cpp
    src/platform/UiDispatcher.cpp
    src/platform/WindowCapture.cpp
)
target_include_directories(meridian_platform PUBLIC src)
target_link_libraries(meridian_platform PUBLIC dwmapi crypt32 d3d11 dxgi windowsapp)

# --- services: no WebKit dependency (unit-testable) -------------------------
add_library(meridian_services STATIC
    src/services/Bookmarks.cpp
    src/services/Database.cpp
    src/services/Favicons.cpp
    src/services/History.cpp
    src/services/IconStore.cpp
    src/services/Reader.cpp
    src/services/ReadingList.cpp
    src/services/SearchSuggestions.cpp
    src/services/SessionStore.cpp
    src/services/Settings.cpp
    src/services/Suggestions.cpp
    src/services/TabGroups.cpp
    src/services/Text.cpp
    src/services/UrlFixup.cpp
)
target_include_directories(meridian_services PUBLIC src PRIVATE "${SQLITE_INCLUDE_DIR}")
target_link_libraries(meridian_services PUBLIC meridian_platform "${SQLITE_LIB}" winhttp windowscodecs shlwapi ole32)

function(meridian_copy_sqlite target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SQLITE_ROOT}/bin/sqlite3.dll" "$<TARGET_FILE_DIR:${target}>")
endfunction()

# --- browser ----------------------------------------------------------------
if(MERIDIAN_BUILD_APP)
    find_path(WEBKIT_HEADERS_DIR WebKit/WKPage.h
        PATHS "${WEBKIT_BUILD_DIR}/WebKit/Headers" "${WEBKIT_BUILD_DIR}/include"
        NO_DEFAULT_PATH REQUIRED)
    find_library(WEBKIT_LIB NAMES WebKit2 WebKit
        PATHS "${WEBKIT_BUILD_DIR}/lib" "${WEBKIT_BUILD_DIR}/lib64"
        NO_DEFAULT_PATH REQUIRED)
    message(STATUS "WebKit headers: ${WEBKIT_HEADERS_DIR}")
    message(STATUS "WebKit import lib: ${WEBKIT_LIB}")

    add_executable(Meridian WIN32
        src/app/main.cpp
        src/app/App.cpp
        src/app/Meridian.manifest
        src/engine/WebView.cpp
        src/ui/BrowserWindow.cpp
        src/ui/Chrome.cpp
        src/ui/HistoryView.cpp
        src/ui/StartPage.cpp
        src/ui/SuggestionPopup.cpp
        src/ui/TabStrip.cpp
        src/ui/TabOverview.cpp
    )
    target_include_directories(Meridian PRIVATE src "${WEBKIT_HEADERS_DIR}")
    target_link_libraries(Meridian PRIVATE meridian_services meridian_platform "${WEBKIT_LIB}"
        d2d1 dwrite comctl32 shell32 uxtheme)

    # Dev probe for Windows.Graphics.Capture (not shipped).
    add_executable(meridian_capture_probe tools/capture_probe.cpp)
    target_link_libraries(meridian_capture_probe PRIVATE meridian_platform)

    # Stage only the WebKit runtime (DLLs, helper processes, resources) next to
    # Meridian.exe — not the ~3 GB of PDBs and test binaries.
    add_custom_command(TARGET Meridian POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DSRC=${WEBKIT_BUILD_DIR}/bin -DDST=$<TARGET_FILE_DIR:Meridian>
                -P ${CMAKE_SOURCE_DIR}/cmake/StageWebKitRuntime.cmake
        COMMENT "Staging WebKit runtime from ${WEBKIT_BUILD_DIR}/bin")
endif()

# --- tests ------------------------------------------------------------------
if(MERIDIAN_BUILD_TESTS)
    enable_testing()
    add_executable(meridian_tests tests/test_main.cpp tests/UrlFixupTests.cpp
        tests/SessionStoreTests.cpp tests/FaviconTests.cpp
        tests/HistoryTests.cpp tests/BookmarkTests.cpp tests/SuggestionTests.cpp
        tests/LibraryTests.cpp tests/ReaderTests.cpp)
    target_link_libraries(meridian_tests PRIVATE meridian_services)
    meridian_copy_sqlite(meridian_tests)
    add_test(NAME services COMMAND meridian_tests)
endif()
