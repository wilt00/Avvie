@echo off
setlocal
set "ROOT=%~dp0"
set "RUNTIME=%ROOT%runtime"

set "PATH=%RUNTIME%\bin"
set "PYTHONHOME=%RUNTIME%"
set "PYTHONPATH=%ROOT%app"
set "PYGI_DLL_PATH=%RUNTIME%\bin"
set "GI_TYPELIB_PATH=%RUNTIME%\lib\girepository-1.0"
set "XDG_DATA_DIRS=%RUNTIME%\share"
set "GSETTINGS_SCHEMA_DIR=%RUNTIME%\share\glib-2.0\schemas"
set "FONTCONFIG_PATH=%RUNTIME%\etc\fonts"
set "GDK_PIXBUF_MODULEDIR=%RUNTIME%\lib\gdk-pixbuf-2.0\2.10.0\loaders"
if defined XDG_CONFIG_HOME (
    set "AVVIE_CONFIG_HOME=%XDG_CONFIG_HOME%"
) else (
    set "AVVIE_CONFIG_HOME=%LOCALAPPDATA%"
)
set "AVVIE_CACHE_DIR=%AVVIE_CONFIG_HOME%\com.github.taiko2k.avvie\Cache"
if not exist "%AVVIE_CACHE_DIR%" mkdir "%AVVIE_CACHE_DIR%"
if errorlevel 1 exit /b %errorlevel%
set "GDK_PIXBUF_MODULE_FILE=%AVVIE_CACHE_DIR%\gdk-pixbuf-loaders-cmd.cache"

"%RUNTIME%\bin\gdk-pixbuf-query-loaders.exe" > "%GDK_PIXBUF_MODULE_FILE%"
if errorlevel 1 exit /b %errorlevel%

"%RUNTIME%\bin\pythonw.exe" "%ROOT%app\launch.py" %*
