"""Portable Windows launcher for Avvie."""

import gettext
import os
import sys

APP_DIR = os.path.dirname(os.path.abspath(__file__))
LOCALE_DIR = os.path.join(APP_DIR, "locale")

sys.path.insert(0, APP_DIR)
gettext.bindtextdomain("avvie", LOCALE_DIR)
gettext.textdomain("avvie")
gettext.install("avvie", LOCALE_DIR)

from avvie import main  # noqa: E402,F401  (import starts the application)
