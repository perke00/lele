# Building ATLAS (Windows / Linux)

Requires CMake >= 3.22 and a C++20 compiler (Visual Studio 2022 or newer, or g++/clang on Linux).
For the Windows setup .exe, NSIS must be installed (https://nsis.sourceforge.io) and on PATH.
macOS is not supported.

## Windows (Developer PowerShell / x64 Native Tools prompt)

    cmake -S . -B build -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure
    cmake --build build --config Release --target package

Output: `build/ATLAS-0.1.0-Windows-setup.exe` (installer) and `.zip` (portable).

## Linux

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build && ctest --test-dir build --output-on-failure
    cmake --build build --target package

## Plugin / installer

The Windows build produces `ATLAS.vst3` (JUCE, downloaded automatically by CMake) and the NSIS
installer copies it to `C:\Program Files\Common Files\VST3\ATLAS.vst3`, the folder FL Studio scans.
After installing: FL Studio -> Options -> Manage plugins -> Find plugins. Add ATLAS to a mixer insert
(effect slot), press Arm, play a kick through it.
