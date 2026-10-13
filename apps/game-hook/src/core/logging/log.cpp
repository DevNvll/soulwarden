#include "core/logging/log.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace soulwarden::core::log {
namespace {

INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
SRWLOCK g_lock = SRWLOCK_INIT;
HANDLE g_file = INVALID_HANDLE_VALUE;
wchar_t g_directory[MAX_PATH]{};

/** Resolves the data directory. The game folder is never used for hook output. */
void resolve_directory() noexcept {
    const DWORD length = GetEnvironmentVariableW(L"SOULWARDEN_DATA_DIR", g_directory, MAX_PATH);
    if (length != 0 && length < MAX_PATH) {
        return;
    }
    wchar_t local[MAX_PATH]{};
    const DWORD localLength = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (localLength == 0 || localLength >= MAX_PATH) {
        GetTempPathW(MAX_PATH, local);
    }
    swprintf(g_directory, MAX_PATH, L"%ls\\Soulwarden\\client", local);
}

/** Creates each directory level of a path. Existing levels are not an error. */
void create_directories(const wchar_t* path) noexcept {
    wchar_t partial[MAX_PATH]{};
    for (std::size_t index = 0; path[index] != L'\0' && index + 1 < MAX_PATH; ++index) {
        partial[index] = path[index];
        if (path[index + 1] == L'\\' || path[index + 1] == L'/' || path[index + 1] == L'\0') {
            CreateDirectoryW(partial, nullptr);
        }
    }
}

BOOL CALLBACK open_once(PINIT_ONCE, PVOID, PVOID*) noexcept {
    resolve_directory();
    wchar_t logs[MAX_PATH]{};
    swprintf(logs, MAX_PATH, L"%ls\\logs", g_directory);
    create_directories(logs);
    wchar_t path[MAX_PATH]{};
    swprintf(path, MAX_PATH, L"%ls\\hook.log", logs);
    g_file = CreateFileW(path,
                         FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr,
                         CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    return TRUE;
}

} // namespace

void initialize() noexcept {
    InitOnceExecuteOnce(&g_once, open_once, nullptr, nullptr);
}

const wchar_t* data_directory() noexcept {
    initialize();
    return g_directory;
}

void write(const char* format, ...) noexcept {
    initialize();
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }
    char line[2048];
    int used = snprintf(line,
                        sizeof(line),
                        "%10lu %5lu ",
                        static_cast<unsigned long>(GetTickCount()),
                        static_cast<unsigned long>(GetCurrentThreadId()));
    va_list arguments;
    va_start(arguments, format);
    const int body = vsnprintf(line + used, sizeof(line) - used - 2, format, arguments);
    va_end(arguments);
    if (body < 0) {
        return;
    }
    used += body < static_cast<int>(sizeof(line)) - used - 2
                ? body
                : static_cast<int>(sizeof(line)) - used - 3;
    line[used++] = '\n';
    AcquireSRWLockExclusive(&g_lock);
    DWORD written = 0;
    WriteFile(g_file, line, static_cast<DWORD>(used), &written, nullptr);
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace soulwarden::core::log
