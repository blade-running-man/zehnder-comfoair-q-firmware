"""PlatformIO pre-build hook: re-configure CMake when the firmware version has changed.

The version is fixed at CMake configure time (tools/version.cmake sets PROJECT_VER, which IDF
compiles into esp_app_desc_t). Under idf.py that is enough: the git files are CMake configure
dependencies, and Ninja re-runs CMake when one changes. PlatformIO does NOT go through that
check. Its espidf builder decides on its own whether to re-run CMake
(is_cmake_reconfigure_required() in builder/frameworks/espidf.py of the pinned platform), from
the mtimes of CMakeLists.txt, the sdkconfig files and dependencies.lock against CMakeCache.txt
-- so a new commit or tag left the old version in the image. This tree shipped exactly that: an
image stamped `cda52b6` built on top of later commits.

So on every build this asks tools/version.py for the version now and compares it with the one
CMake last configured with (project_ver.txt in the build directory, written by version.cmake).
On a difference it deletes build.ninja: a missing build.ninja is one of the conditions
PlatformIO's check already honours, and CMake writes it again on every configure, along with the
new stamp. That also catches what no git file records -- a tree turning dirty or clean -- which
the idf.py path cannot see.

DO NOT backdate CMakeCache.txt instead, although the check honours that too: CMake leaves an
unchanged cache file untouched, so its mtime stays in the past and every later build
re-configures (measured on this tree with the pinned CMake 4.0.3). DO NOT delete the build
directory or the cache either: that is a full rebuild, and only esp_app_desc.c's compile
definition actually changes.
"""
import subprocess
import sys
from pathlib import Path

Import("env")  # noqa: F821  -- injected by PlatformIO/SCons

ROOT = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
BUILD = Path(env.subst("$BUILD_DIR"))  # noqa: F821

sys.path.insert(0, str(ROOT / "tools"))
import version  # noqa: E402

result = subprocess.run([sys.executable, str(ROOT / "tools" / "version.py")],
                        capture_output=True, text=True)
if result.returncode != 0:
    print(result.stderr, file=sys.stderr)
    raise SystemExit("tools/version.py failed: the firmware version cannot be determined")
current = result.stdout.strip()
ninja = BUILD / "build.ninja"
if ninja.is_file() and version.stamp_is_stale(BUILD / "project_ver.txt", current):
    print(f"Firmware version is now {current}; re-configuring CMake")
    ninja.unlink()
