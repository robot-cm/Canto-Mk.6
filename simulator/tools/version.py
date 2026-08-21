#!/usr/bin/env python3
# Minimal stub for embedded simulator builds.
# The upstream jerryscript CMakeLists resolves tools/version.py relative to
# CMAKE_SOURCE_DIR, which is the simulator/ dir in this embedded build (not the
# jerryscript root). JERRY_VERSION is only used for a STATUS message, so a fixed
# string is sufficient.
import sys

if __name__ == "__main__":
    print("3.0.0")
    sys.exit(0)
