# 3.24+ because DOWNLOAD_EXTRACT_TIMESTAMP (used below for the WebView2
# NuGet fetch) doesn't exist before that - with 3.20 you'd get a confusing
# "Unknown option" at configure time instead of this clear message.
cmake_minimum_required(VERSION 3.24)
project(MessengerClient LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MSG_USE_WEBVIEW2 "Use Microsoft WebView2" ON)
option(MSG_BUILD_TESTS "Build unit tests" OFF)

if(NOT WIN32 AND MSG_USE_WEBVIEW2)
    message(WARNING "WebView2 is Windows-only. Disabling MSG_USE_WEBVIEW2.")
    set(MSG_USE_WEBVIEW2 OFF)
endif()

if(DEFINED ENV{VCPKG_ROOT})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
endif()

include(FetchContent)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 10.0.0
)
FetchContent_MakeAvailable(tinyxml2)

if(MSG_USE_WEBVIEW2)
    # NOTE: this is a plain NuGet .nupkg (a zip), NOT a CMake project - it
    # has no CMakeLists.txt at its root. FetchContent_MakeAvailable() would
    # try to add_subdirectory() it and fail configure with
    # "does not contain a CMakeLists.txt". FetchContent_Populate() just
    # downloads + extracts, which is all we need - we link the .lib/include
    # paths directly below.
    FetchContent_Declare(webview2
        URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(webview2)
    if(NOT webview2_POPULATED)
        FetchContent_Populate(webview2)
    endif()
endif()

set(MSG_SOURCES
    src/main.cpp
    src/Config.cpp
    src/AppState.cpp
    src/NetworkClient.cpp
    src/Crypto.cpp
    src/Bridge.cpp
    src/UiLayoutLoader.cpp
    src/WebViewHost.cpp
    src/LocalStore.cpp
)

# GUI-приложение без консоли (--cli временно подключается к родительской консоли, см. main.cpp)
add_executable(messenger_client WIN32 ${MSG_SOURCES})

if(MSVC)
    # Source files (this project's L"Кириллица..." literals - tray menu,
    # error messages, etc.) are saved as UTF-8. Without this flag, MSVC
    # decodes them using the system ANSI codepage instead, which silently
    # turns every Cyrillic wide-string literal into garbage at runtime -
    # exactly the "непонятные символы" the tray menu showed. /utf-8 tells
    # MSVC both to read source as UTF-8 and to encode string literals as
    # UTF-8 in the compiled binary, fixing every such literal at once
    # rather than patching each string.
    target_compile_options(messenger_client PRIVATE /utf-8)
endif()

target_include_directories(messenger_client PRIVATE src)
target_link_libraries(messenger_client PRIVATE nlohmann_json::nlohmann_json tinyxml2)

find_path(SODIUM_INCLUDE_DIR sodium.h PATHS ${CMAKE_PREFIX_PATH}/include)
find_library(SODIUM_LIBRARY sodium PATHS ${CMAKE_PREFIX_PATH}/lib)

if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
    message(FATAL_ERROR "libsodium not found! Install it: vcpkg install libsodium:x64-windows")
endif()

target_include_directories(messenger_client PRIVATE ${SODIUM_INCLUDE_DIR})
target_link_libraries(messenger_client PRIVATE ${SODIUM_LIBRARY})
target_compile_definitions(messenger_client PRIVATE MSG_HAVE_SODIUM=1)

if(MSG_USE_WEBVIEW2)
    target_compile_definitions(messenger_client PRIVATE MSG_USE_WEBVIEW2=1)

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(WEBVIEW2_LIB_PATH ${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib)
    else()
        set(WEBVIEW2_LIB_PATH ${webview2_SOURCE_DIR}/build/native/x86/WebView2LoaderStatic.lib)
    endif()

    target_include_directories(messenger_client PRIVATE ${webview2_SOURCE_DIR}/build/native/include)
    target_link_libraries(messenger_client PRIVATE
        ${WEBVIEW2_LIB_PATH}
        WindowsApp.lib
    )
endif()

add_custom_command(TARGET messenger_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/ui $<TARGET_FILE_DIR:messenger_client>/ui
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:messenger_client>/config
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:messenger_client>/image
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/image $<TARGET_FILE_DIR:messenger_client>/image
)

if(MSG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
