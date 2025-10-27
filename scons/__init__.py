# SPDX-FileCopyrightText: 2025 Gridshadows Gaming <https://www.gridshadows.co.uk>
# SPDX-License-Identifier: Zlib

"""
Module to generate the export interface for a GDExtension from the C++ source
files during an SCons build.

In order to successfully run, it requires a built version of the
[`godot-cpp` Git repository](https://github.com/godotengine/godot-cpp) and
a recent Clang version (tested with Clang 19)

See README.md for details on the attributes necessary for the export.
"""

__author__ = "Gridshadows Gaming"
__copyright__ = "Copyright 2025 Gridshadows Gaming"
__license__ = "ZLib"
__version__ = "0.1.0"
__status__ = "Development"

from .scons import configure_generate
