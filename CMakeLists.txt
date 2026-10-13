cmake_minimum_required(VERSION 3.25)

project(Soulwarden LANGUAGES C CXX)

option(SOULWARDEN_BUILD_GAME_HOOK "Build the distributable game hook DLL" ON)
option(SOULWARDEN_BUILD_BACKEND "Build the standalone local backend" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# A product must not have an absolute path of the build computer: the path can have a name of
# the user. Mbed TLS keeps __FILE__ text in the backend.
add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")

if(SOULWARDEN_BUILD_GAME_HOOK)
    if(NOT MINGW)
        message(FATAL_ERROR "The game hook requires a MinGW Windows toolchain.")
    endif()

    set(SOULWARDEN_CASE_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/case-include")
    file(MAKE_DIRECTORY "${SOULWARDEN_CASE_INCLUDE}")
    foreach(header IN ITEMS Windows WinSock2 MSWSock WS2tcpip)
        string(TOLOWER "${header}" lower_header)
        file(CONFIGURE
            OUTPUT "${SOULWARDEN_CASE_INCLUDE}/${header}.h"
            CONTENT "#include <${lower_header}.h>\n"
            @ONLY)
    endforeach()

    file(GLOB_RECURSE SOULWARDEN_HOOK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/apps/game-hook/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/apps/game-hook/vendor/*.cpp")

    add_library(steam_api64 SHARED ${SOULWARDEN_HOOK_SOURCES})
    target_compile_definitions(steam_api64 PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        UNICODE
        _UNICODE
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        NDEBUG
        # Dear ImGui (the tool window) reads its configuration from this header.
        IMGUI_USER_CONFIG="core/ui/imgui_user_config.h")
    target_compile_options(steam_api64 PRIVATE
        -O2
        -fms-extensions
        -Wno-braced-scalar-init
        -Wno-nonportable-include-path)
    target_include_directories(steam_api64 PRIVATE
        "${SOULWARDEN_CASE_INCLUDE}"
        apps/game-hook/src
        apps/game-hook/vendor/detours
        apps/game-hook/vendor/imgui
        apps/game-hook/vendor/imgui/backends
        apps/game-hook/vendor)
    target_link_libraries(steam_api64 PRIVATE
        user32
        winhttp
        wininet
        ws2_32
        # The Direct3D 12 part and the Win32 part of Dear ImGui.
        gdi32
        dwmapi
        d3dcompiler
        dxgi)
    target_link_options(steam_api64 PRIVATE
        -static
        -Wl,--subsystem,windows
        -Wl,--strip-all)
    set_target_properties(steam_api64 PROPERTIES
        PREFIX ""
        OUTPUT_NAME "steam_api64"
        SUFFIX ".dll")
endif()

if(SOULWARDEN_BUILD_BACKEND)
    add_subdirectory(apps/backend)
endif()
