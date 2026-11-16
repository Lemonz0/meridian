cmake_minimum_required(VERSION 3.24)
project(Meridian LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Path to a WebKit WinCairo build tree (the directory containing bin64/ and lib64/).
set(WEBKIT_BUILD_DIR "D:/wk/WebKit/WebKitBuild/Release" CACHE PATH "WebKit WinCairo build output")
option(MERIDIAN_BUILD_APP "Build the browser (needs WEBKIT_BUILD_DIR)" ON)
option(MERIDIAN_BUILD_TESTS "Build unit tests" ON)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# --- services: pure C++, no WebKit dependency (unit-testable) ---------------
add_library(meridian_services STATIC
    src/services/UrlFixup.cpp
)
target_include_directories(meridian_services PUBLIC src)

# --- browser ----------------------------------------------------------------
if(MERIDIAN_BUILD_APP)
    find_path(WEBKIT_HEADERS_DIR WebKit/WKPage.h
        PATHS "${WEBKIT_BUILD_DIR}/WebKit/Headers" "${WEBKIT_BUILD_DIR}/include"
        NO_DEFAULT_PATH REQUIRED)
    find_library(WEBKIT_LIB NAMES WebKit WebKit2
        PATHS "${WEBKIT_BUILD_DIR}/lib64" "${WEBKIT_BUILD_DIR}/lib"
        NO_DEFAULT_PATH REQUIRED)
    message(STATUS "WebKit headers: ${WEBKIT_HEADERS_DIR}")
    message(STATUS "WebKit import lib: ${WEBKIT_LIB}")

    add_executable(Meridian WIN32
        src/app/main.cpp
        src/app/Meridian.manifest
        src/engine/WebView.cpp
        src/platform/SystemTheme.cpp
        src/ui/BrowserWindow.cpp
    )
    target_include_directories(Meridian PRIVATE src "${WEBKIT_HEADERS_DIR}")
    target_link_libraries(Meridian PRIVATE meridian_services "${WEBKIT_LIB}"
        d2d1 dwrite dwmapi comctl32 shell32 ole32 uxtheme)

    # Stage WebKit runtime (DLLs, helper processes, resources) next to Meridian.exe.
    add_custom_command(TARGET Meridian POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${WEBKIT_BUILD_DIR}/bin64" "$<TARGET_FILE_DIR:Meridian>"
        COMMENT "Staging WebKit runtime from ${WEBKIT_BUILD_DIR}/bin64")
endif()

# --- tests ------------------------------------------------------------------
if(MERIDIAN_BUILD_TESTS)
    enable_testing()
    add_executable(meridian_tests tests/test_main.cpp tests/UrlFixupTests.cpp)
    target_link_libraries(meridian_tests PRIVATE meridian_services)
    add_test(NAME services COMMAND meridian_tests)
endif()
