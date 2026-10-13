#pragma once

// Dear ImGui configuration of the hook DLL. The compile definition
// IMGUI_USER_CONFIG="core/ui/imgui_user_config.h" selects this file for each Dear ImGui source.

// Only the initialization interface of the DX12 backend with ImGui_ImplDX12_InitInfo.
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
// No imgui.ini and no log file: the hook writes nothing into the game folder.
#define IMGUI_DISABLE_FILE_FUNCTIONS
// No ShellExecute: the hook starts no other program. This also removes the shell32 import.
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
// The game keeps the input method. This also removes the imm32 import.
#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
// The frame runs on the submission thread of the renderer: it makes no clipboard call there.
#define IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
// The game keeps the gamepad. This also removes the XInput load.
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
// The demo windows are not a part of the product.
#define IMGUI_DISABLE_DEMO_WINDOWS
