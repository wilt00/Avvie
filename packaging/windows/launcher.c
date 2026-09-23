#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>

#include <stdint.h>
#include <wchar.h>

#define PATH_CAPACITY 32768
#define COMMAND_CAPACITY 32767
#define APP_ID L"com.github.taiko2k.avvie"

static void show_error(const wchar_t *message) {
    MessageBoxW(NULL, message, L"Avvie could not start", MB_OK | MB_ICONERROR);
}

static BOOL join_path(wchar_t *output, size_t capacity, const wchar_t *root, const wchar_t *suffix) {
    return SUCCEEDED(StringCchPrintfW(output, capacity, L"%s%s", root, suffix));
}

static BOOL ensure_directory(const wchar_t *path) {
    int result = SHCreateDirectoryExW(NULL, path, NULL);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

static BOOL get_application_data_directory(wchar_t *output, size_t capacity) {
    wchar_t config_home[PATH_CAPACITY];
    PWSTR local_data = NULL;
    DWORD config_home_length =
        GetEnvironmentVariableW(L"XDG_CONFIG_HOME", config_home, PATH_CAPACITY);
    BOOL success = FALSE;

    if (config_home_length >= PATH_CAPACITY) {
        return FALSE;
    }
    if (config_home_length == 0) {
        if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_DEFAULT, NULL,
                                        &local_data))) {
            return FALSE;
        }
        if (FAILED(StringCchCopyW(config_home, PATH_CAPACITY, local_data))) {
            goto cleanup;
        }
    }

    if (FAILED(StringCchPrintfW(output, capacity, L"%s\\" APP_ID, config_home)) ||
        !ensure_directory(output)) {
        goto cleanup;
    }
    success = TRUE;

cleanup:
    CoTaskMemFree(local_data);
    return success;
}

static BOOL set_path_environment(const wchar_t *root) {
    wchar_t runtime[PATH_CAPACITY];
    wchar_t runtime_bin[PATH_CAPACITY];
    wchar_t value[PATH_CAPACITY];

    if (!join_path(runtime, PATH_CAPACITY, root, L"runtime") ||
        !join_path(runtime_bin, PATH_CAPACITY, root, L"runtime\\bin")) {
        return FALSE;
    }

    if (!SetEnvironmentVariableW(L"PATH", runtime_bin) ||
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
        return FALSE;
    }

    return TRUE;
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

static BOOL file_is_current(const wchar_t *cache_path, const wchar_t *query_path,
                            const wchar_t *loader_directory) {
    WIN32_FILE_ATTRIBUTE_DATA cache_data;
    WIN32_FILE_ATTRIBUTE_DATA query_data;
    WIN32_FILE_ATTRIBUTE_DATA loader_directory_data;
    WIN32_FIND_DATAW loader_data;
    wchar_t search_path[PATH_CAPACITY];
    HANDLE search = INVALID_HANDLE_VALUE;
    BOOL current = FALSE;

    if (!GetFileAttributesExW(cache_path, GetFileExInfoStandard, &cache_data) ||
        !GetFileAttributesExW(query_path, GetFileExInfoStandard, &query_data) ||
        !GetFileAttributesExW(loader_directory, GetFileExInfoStandard, &loader_directory_data) ||
        CompareFileTime(&cache_data.ftLastWriteTime, &query_data.ftLastWriteTime) < 0 ||
        CompareFileTime(&cache_data.ftLastWriteTime,
                        &loader_directory_data.ftLastWriteTime) < 0 ||
        FAILED(StringCchPrintfW(search_path, PATH_CAPACITY, L"%s\\*", loader_directory))) {
        return FALSE;
    }

    search = FindFirstFileW(search_path, &loader_data);
    if (search == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    do {
        if (!(loader_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            CompareFileTime(&cache_data.ftLastWriteTime, &loader_data.ftLastWriteTime) < 0) {
            goto cleanup;
        }
    } while (FindNextFileW(search, &loader_data));

    current = GetLastError() == ERROR_NO_MORE_FILES;

cleanup:
    FindClose(search);
    return current;
}

static BOOL generate_pixbuf_cache(const wchar_t *root, const wchar_t *cache_directory,
                                  wchar_t *cache_path, size_t cache_capacity) {
    wchar_t query_path[PATH_CAPACITY];
    wchar_t loader_directory[PATH_CAPACITY];
    wchar_t command_line[PATH_CAPACITY + 4];
    HANDLE output = INVALID_HANDLE_VALUE;
    HANDLE input = INVALID_HANDLE_VALUE;
    HANDLE error_output = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD exit_code = 1;

    if (FAILED(StringCchPrintfW(cache_path, cache_capacity, L"%s\\gdk-pixbuf-loaders-%016llx.cache",
                                cache_directory, (unsigned long long)path_hash(root))) ||
        !join_path(query_path, PATH_CAPACITY, root, L"runtime\\bin\\gdk-pixbuf-query-loaders.exe") ||
        !join_path(loader_directory, PATH_CAPACITY, root,
                   L"runtime\\lib\\gdk-pixbuf-2.0\\2.10.0\\loaders")) {
        return FALSE;
    }

    if (file_is_current(cache_path, query_path, loader_directory)) {
        return SetEnvironmentVariableW(L"GDK_PIXBUF_MODULE_FILE", cache_path);
    }

    output = CreateFileW(cache_path, GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    error_output = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE ||
        error_output == INVALID_HANDLE_VALUE ||
        FAILED(StringCchPrintfW(command_line, PATH_CAPACITY + 4, L"\"%s\"", query_path))) {
        goto cleanup;
    }

    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = error_output;

    if (!CreateProcessW(query_path, command_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, root,
                        &startup, &process)) {
        goto cleanup;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

cleanup:
    if (error_output != INVALID_HANDLE_VALUE) {
        CloseHandle(error_output);
    }
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

static int run_avvie(const wchar_t *root, const wchar_t *log_directory, int argument_count,
                     wchar_t **arguments) {
    wchar_t python_path[PATH_CAPACITY];
    wchar_t launch_path[PATH_CAPACITY];
    wchar_t log_path[PATH_CAPACITY];
    wchar_t command_line[COMMAND_CAPACITY] = L"";
    size_t command_length = 0;
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
            L"import shutil; assert shutil.which('jpegtran'), 'bundled jpegtran not found'; "
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

    if (FAILED(StringCchPrintfW(log_path, PATH_CAPACITY, L"%s\\launcher.log", log_directory))) {
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
    if (exit_code == 0) {
        DeleteFileW(log_path);
    } else if (!smoke_test) {
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
    wchar_t application_data_directory[PATH_CAPACITY];
    wchar_t cache_directory[PATH_CAPACITY];
    wchar_t log_directory[PATH_CAPACITY];
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
    if (!get_application_data_directory(application_data_directory, PATH_CAPACITY) ||
        FAILED(StringCchPrintfW(cache_directory, PATH_CAPACITY, L"%s\\Cache",
                                application_data_directory)) ||
        FAILED(StringCchPrintfW(log_directory, PATH_CAPACITY, L"%s\\Logs",
                                application_data_directory)) ||
        !ensure_directory(cache_directory) || !ensure_directory(log_directory)) {
        show_error(L"Could not initialize the application data directories.");
        return 1;
    }
    if (!generate_pixbuf_cache(executable_path, cache_directory, cache_path, PATH_CAPACITY)) {
        show_error(L"Could not initialize the bundled image loaders.");
        return 1;
    }

    arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == NULL) {
        show_error(L"Could not parse the command line.");
        return 1;
    }
    exit_code = run_avvie(executable_path, log_directory, argument_count, arguments);
    LocalFree(arguments);
    return exit_code;
}
