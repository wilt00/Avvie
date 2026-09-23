#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>

#include <stdint.h>
#include <wchar.h>

#define PATH_CAPACITY 32768
#define COMMAND_CAPACITY 65536

static void show_error(const wchar_t *message) {
    MessageBoxW(NULL, message, L"Avvie could not start", MB_OK | MB_ICONERROR);
}

static BOOL join_path(wchar_t *output, size_t capacity, const wchar_t *root, const wchar_t *suffix) {
    return SUCCEEDED(StringCchPrintfW(output, capacity, L"%s%s", root, suffix));
}

static BOOL set_path_environment(const wchar_t *root) {
    wchar_t runtime[PATH_CAPACITY];
    wchar_t runtime_bin[PATH_CAPACITY];
    wchar_t value[PATH_CAPACITY];
    DWORD old_path_length;
    wchar_t *old_path = NULL;
    BOOL success = FALSE;

    if (!join_path(runtime, PATH_CAPACITY, root, L"runtime") ||
        !join_path(runtime_bin, PATH_CAPACITY, root, L"runtime\\bin")) {
        return FALSE;
    }

    old_path_length = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (old_path_length > 0) {
        old_path = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, old_path_length * sizeof(wchar_t));
        if (old_path == NULL || GetEnvironmentVariableW(L"PATH", old_path, old_path_length) == 0) {
            goto cleanup;
        }
    }

    if (FAILED(StringCchPrintfW(value, PATH_CAPACITY, L"%s;%s", runtime_bin, old_path ? old_path : L"")) ||
        !SetEnvironmentVariableW(L"PATH", value) ||
        !SetEnvironmentVariableW(L"PYTHONHOME", runtime) ||
        !join_path(value, PATH_CAPACITY, root, L"app") ||
        !SetEnvironmentVariableW(L"PYTHONPATH", value) ||
        !SetEnvironmentVariableW(L"PYGI_DLL_PATH", runtime_bin) ||
        !join_path(value, PATH_CAPACITY, root, L"runtime\\lib\\girepository-1.0") ||
        !SetEnvironmentVariableW(L"GI_TYPELIB_PATH", value) ||
        !join_path(value, PATH_CAPACITY, root, L"runtime\\share") ||
        !SetEnvironmentVariableW(L"XDG_DATA_DIRS", value) ||
        !join_path(value, PATH_CAPACITY, root, L"runtime\\share\\glib-2.0\\schemas") ||
        !SetEnvironmentVariableW(L"GSETTINGS_SCHEMA_DIR", value) ||
        !join_path(value, PATH_CAPACITY, root, L"runtime\\etc\\fonts") ||
        !SetEnvironmentVariableW(L"FONTCONFIG_PATH", value) ||
        !join_path(value, PATH_CAPACITY, root, L"runtime\\lib\\gdk-pixbuf-2.0\\2.10.0\\loaders") ||
        !SetEnvironmentVariableW(L"GDK_PIXBUF_MODULEDIR", value) ||
        !SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1")) {
        goto cleanup;
    }

    success = TRUE;

cleanup:
    if (old_path != NULL) {
        HeapFree(GetProcessHeap(), 0, old_path);
    }
    return success;
}

static uint64_t path_hash(const wchar_t *path) {
    uint64_t hash = UINT64_C(14695981039346656037);
    while (*path != L'\0') {
        wchar_t character = *path++;
        if (character >= L'A' && character <= L'Z') {
            character = character - L'A' + L'a';
        }
        hash ^= (uint16_t)character;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static BOOL file_is_current(const wchar_t *cache_path, const wchar_t *query_path) {
    WIN32_FILE_ATTRIBUTE_DATA cache_data;
    WIN32_FILE_ATTRIBUTE_DATA query_data;

    if (!GetFileAttributesExW(cache_path, GetFileExInfoStandard, &cache_data) ||
        !GetFileAttributesExW(query_path, GetFileExInfoStandard, &query_data)) {
        return FALSE;
    }

    return CompareFileTime(&cache_data.ftLastWriteTime, &query_data.ftLastWriteTime) >= 0;
}

static BOOL generate_pixbuf_cache(const wchar_t *root, wchar_t *cache_path, size_t cache_capacity) {
    wchar_t local_data[PATH_CAPACITY];
    wchar_t cache_directory[PATH_CAPACITY];
    wchar_t query_path[PATH_CAPACITY];
    wchar_t command_line[PATH_CAPACITY + 4];
    DWORD local_data_length;
    HANDLE output = INVALID_HANDLE_VALUE;
    HANDLE input = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD exit_code = 1;

    local_data_length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_data, PATH_CAPACITY);
    if (local_data_length == 0 || local_data_length >= PATH_CAPACITY) {
        local_data_length = GetTempPathW(PATH_CAPACITY, local_data);
        if (local_data_length == 0 || local_data_length >= PATH_CAPACITY) {
            return FALSE;
        }
        if (local_data[local_data_length - 1] == L'\\') {
            local_data[local_data_length - 1] = L'\0';
        }
    }

    if (FAILED(StringCchPrintfW(cache_directory, PATH_CAPACITY, L"%s\\Avvie", local_data))) {
        return FALSE;
    }
    if (!CreateDirectoryW(cache_directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return FALSE;
    }
    if (FAILED(StringCchPrintfW(cache_path, cache_capacity, L"%s\\gdk-pixbuf-loaders-%016llx.cache",
                                cache_directory, (unsigned long long)path_hash(root))) ||
        !join_path(query_path, PATH_CAPACITY, root, L"runtime\\bin\\gdk-pixbuf-query-loaders.exe")) {
        return FALSE;
    }

    if (file_is_current(cache_path, query_path)) {
        return SetEnvironmentVariableW(L"GDK_PIXBUF_MODULE_FILE", cache_path);
    }

    output = CreateFileW(cache_path, GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE ||
        FAILED(StringCchPrintfW(command_line, PATH_CAPACITY + 4, L"\"%s\"", query_path))) {
        goto cleanup;
    }

    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;

    if (!CreateProcessW(query_path, command_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, root,
                        &startup, &process)) {
        goto cleanup;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

cleanup:
    if (input != INVALID_HANDLE_VALUE) {
        CloseHandle(input);
    }
    if (output != INVALID_HANDLE_VALUE) {
        CloseHandle(output);
    }
    if (exit_code != 0) {
        DeleteFileW(cache_path);
        return FALSE;
    }
    return SetEnvironmentVariableW(L"GDK_PIXBUF_MODULE_FILE", cache_path);
}

static BOOL append_character(wchar_t *command, size_t capacity, size_t *length, wchar_t character) {
    if (*length + 1 >= capacity) {
        return FALSE;
    }
    command[(*length)++] = character;
    command[*length] = L'\0';
    return TRUE;
}

static BOOL append_quoted_argument(wchar_t *command, size_t capacity, size_t *length, const wchar_t *argument) {
    size_t backslashes = 0;

    if (*length > 0 && !append_character(command, capacity, length, L' ')) {
        return FALSE;
    }
    if (!append_character(command, capacity, length, L'\"')) {
        return FALSE;
    }

    for (; *argument != L'\0'; ++argument) {
        if (*argument == L'\\') {
            ++backslashes;
            continue;
        }
        if (*argument == L'\"') {
            while (backslashes > 0) {
                --backslashes;
                if (!append_character(command, capacity, length, L'\\') ||
                    !append_character(command, capacity, length, L'\\')) {
                    return FALSE;
                }
            }
            if (!append_character(command, capacity, length, L'\\') ||
                !append_character(command, capacity, length, L'\"')) {
                return FALSE;
            }
            continue;
        }
        while (backslashes > 0) {
            --backslashes;
            if (!append_character(command, capacity, length, L'\\')) {
                return FALSE;
            }
        }
        if (!append_character(command, capacity, length, *argument)) {
            return FALSE;
        }
    }

    while (backslashes > 0) {
        --backslashes;
        if (!append_character(command, capacity, length, L'\\') ||
            !append_character(command, capacity, length, L'\\')) {
            return FALSE;
        }
    }
    return append_character(command, capacity, length, L'\"');
}

static int run_avvie(const wchar_t *root, int argument_count, wchar_t **arguments) {
    wchar_t python_path[PATH_CAPACITY];
    wchar_t launch_path[PATH_CAPACITY];
    wchar_t log_path[PATH_CAPACITY];
    wchar_t local_data[PATH_CAPACITY];
    wchar_t command_line[COMMAND_CAPACITY] = L"";
    size_t command_length = 0;
    DWORD local_data_length;
    HANDLE log = INVALID_HANDLE_VALUE;
    HANDLE input = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD exit_code = 1;
    int first_forwarded_argument = 1;
    BOOL smoke_test = argument_count == 2 && wcscmp(arguments[1], L"--launcher-smoke-test") == 0;

    if (!join_path(python_path, PATH_CAPACITY, root,
                   smoke_test ? L"runtime\\bin\\python.exe" : L"runtime\\bin\\pythonw.exe") ||
        !join_path(launch_path, PATH_CAPACITY, root, L"app\\launch.py") ||
        !append_quoted_argument(command_line, COMMAND_CAPACITY, &command_length, python_path)) {
        return 1;
    }

    if (smoke_test) {
        const wchar_t *test_code =
            L"import gi; gi.require_version('Gtk','4.0'); gi.require_version('Adw','1'); "
            L"from gi.repository import Gtk, Adw; import piexif; from PIL import Image; "
            L"print('native launcher smoke test OK')";
        if (!append_quoted_argument(command_line, COMMAND_CAPACITY, &command_length, L"-c") ||
            !append_quoted_argument(command_line, COMMAND_CAPACITY, &command_length, test_code)) {
            return 1;
        }
        first_forwarded_argument = argument_count;
    } else if (!append_quoted_argument(command_line, COMMAND_CAPACITY, &command_length, launch_path)) {
        return 1;
    }

    for (int index = first_forwarded_argument; index < argument_count; ++index) {
        if (!append_quoted_argument(command_line, COMMAND_CAPACITY, &command_length, arguments[index])) {
            return 1;
        }
    }

    local_data_length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_data, PATH_CAPACITY);
    if (local_data_length == 0 || local_data_length >= PATH_CAPACITY ||
        FAILED(StringCchPrintfW(log_path, PATH_CAPACITY, L"%s\\Avvie\\avvie.log", local_data))) {
        return 1;
    }

    log = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        goto cleanup;
    }

    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = log;
    startup.hStdError = log;

    if (!CreateProcessW(python_path, command_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, root,
                        &startup, &process)) {
        goto cleanup;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

cleanup:
    if (input != INVALID_HANDLE_VALUE) {
        CloseHandle(input);
    }
    if (log != INVALID_HANDLE_VALUE) {
        CloseHandle(log);
    }
    if (exit_code != 0 && !smoke_test) {
        wchar_t message[PATH_CAPACITY];
        if (SUCCEEDED(StringCchPrintfW(message, PATH_CAPACITY,
                                      L"Avvie exited with code %lu. Details are in:\n%s",
                                      exit_code, log_path))) {
            show_error(message);
        }
    }
    return (int)exit_code;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance, PWSTR command_line, int show_command) {
    wchar_t executable_path[PATH_CAPACITY];
    wchar_t cache_path[PATH_CAPACITY];
    wchar_t *separator;
    wchar_t **arguments;
    int argument_count;
    int exit_code;

    (void)instance;
    (void)previous_instance;
    (void)command_line;
    (void)show_command;

    DWORD path_length = GetModuleFileNameW(NULL, executable_path, PATH_CAPACITY);
    if (path_length == 0 || path_length >= PATH_CAPACITY) {
        show_error(L"Could not determine the installation directory.");
        return 1;
    }
    separator = wcsrchr(executable_path, L'\\');
    if (separator == NULL) {
        show_error(L"Could not determine the installation directory.");
        return 1;
    }
    separator[1] = L'\0';

    if (!set_path_environment(executable_path)) {
        show_error(L"Could not configure the bundled runtime.");
        return 1;
    }
    if (!generate_pixbuf_cache(executable_path, cache_path, PATH_CAPACITY)) {
        show_error(L"Could not initialize the bundled image loaders.");
        return 1;
    }

    arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == NULL) {
        show_error(L"Could not parse the command line.");
        return 1;
    }
    exit_code = run_avvie(executable_path, argument_count, arguments);
    LocalFree(arguments);
    return exit_code;
}
