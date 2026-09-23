"""Portable Windows launcher for Avvie."""

import gettext
import os
import sys

WINDOWS_APP_ID = "com.github.taiko2k.avvie"

# The GUI runs in pythonw.exe rather than in the native bootstrap process.
# Give that process Avvie's identity before GTK creates any windows, otherwise
# Windows groups it as Python on the taskbar.
if sys.platform == "win32":
    import ctypes

    set_app_id = ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID
    set_app_id.argtypes = [ctypes.c_wchar_p]
    set_app_id.restype = ctypes.c_long
    set_app_id(WINDOWS_APP_ID)

APP_DIR = os.path.dirname(os.path.abspath(__file__))
LOCALE_DIR = os.path.join(APP_DIR, "locale")

sys.path.insert(0, APP_DIR)
gettext.bindtextdomain("avvie", LOCALE_DIR)
gettext.textdomain("avvie")
gettext.install("avvie", LOCALE_DIR)

from avvie import main  # noqa: E402,F401  (import starts the application)
