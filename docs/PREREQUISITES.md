# Prerequisites

This document describes what must be installed before you use `ld_patcher`
successfully.

It is written for two different audiences:

- normal users who want to run the program or follow the manual workflow
- maintainers who want to build `ld_patcher.exe` itself from source

## Short Version

If you want the shortest safe checklist, install these first:

1. one supported ST source ZIP or extracted ST source tree
2. MSYS2 on Windows
3. the MSYS2 `mingw64` packages used by the build recipes
4. STM32CubeIDE or another ARM GCC toolchain containing `arm-none-eabi-g++.exe`

If you only want to:

- `Analyze`
- `Validate`

then you do **not** need MSYS2 or STM32CubeIDE yet.

## Task-Based Dependency Matrix

### 1. Only inspect a source ZIP or source tree

Needed:

- `ld_patcher`
- one supported ST source ZIP or extracted source tree

Not needed yet:

- MSYS2
- STM32CubeIDE
- Qt SDK

This covers:

- `Analyze`
- `Validate`

### 2. Apply the patch

Needed:

- everything from the inspect-only case
- a writable extracted source tree

Not needed yet:

- MSYS2 for the actual linker build
- STM32CubeIDE for verify

This covers:

- `Apply`

### 3. Build the patched linker

Needed:

- everything from the patch step
- MSYS2
- the required `mingw64` build packages

This covers:

- `Build`

### 4. Verify the patched linker

Needed:

- everything from the build step
- STM32CubeIDE or another ARM GNU toolchain that provides:
  - `arm-none-eabi-g++.exe`

This covers:

- `Verify`

### 5. Package for CubeIDE

Needed:

- a successful `Build`

This covers:

- `Package (CubeIDE)`

It does not need any extra software beyond the build step.

### 6. Build `ld_patcher.exe` itself from source

Needed:

- Qt 6 with a MinGW kit
- `qmake`
- `mingw32-make`
- `windeployqt`
- CMake
- PowerShell

Important:

- this is only for maintainers or developers of `ld_patcher`
- normal users of a ready-built `release\ld_patcher.exe` do **not** need the Qt SDK

## Supported Host Assumption

The current documented workflow assumes:

- Windows
- PowerShell
- MSYS2 for the ST linker build

That is the environment the current recipes and scripts are designed around.

## Supported ST Source Inputs

Currently supported ST source inputs:

- `gnu-tools-for-stm32-13.3.rel1.zip`
- `gnu-tools-for-stm32-14.3.rel1.zip`
- extracted trees created from those source snapshots

Official source page:

- ST `gnu-tools-for-stm32`
  - https://github.com/STMicroelectronics/gnu-tools-for-stm32

## 1. Install MSYS2

Official site:

- https://www.msys2.org/

Official installation page:

- https://www.msys2.org/#installation

What `ld_patcher` currently expects for the build recipes:

- an MSYS2 installation with:
  - `msys2_shell.cmd`
  - `bash.exe`
  - `mingw64` toolchain binaries

Typical default install path:

- `C:\msys64`

### Recommended manual install path

This is the most predictable option for users.

1. install MSYS2 from the official installer
2. open the `MSYS2 MinGW 64-bit` shell
3. install the required packages:

```bash
pacman -Sy --noconfirm --needed \
  make \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-make \
  mingw-w64-x86_64-binutils \
  mingw-w64-x86_64-zstd
```

These package names match the current `ld_patcher` build backend.

### Optional dependency only for canonical longpath helper debugging

Most users do **not** need anything extra beyond the packages above.

One narrow exception exists:

- if you intentionally debug ST's original `liblongpath-win32/helper.py` flow
- or if you use the preserved reference script
  [`../scripts/manual_reference/build_st_ld_manual_canonical_longpath.sh`](../scripts/manual_reference/build_st_ld_manual_canonical_longpath.sh)

then the Python environment visible from MSYS2 may also need:

```bash
python -m pip install python-magic
```

This is not required for the normal current `ld_patcher` GUI/CLI workflow and
it is not required for the normal maintained manual fallback workflow.

### Automatic MSYS2 bootstrap in the program

Current behavior:

- the GUI/CLI build backend can try to install MSYS2 automatically through `winget`
- then it can install the required packages through `pacman`

Practical recommendation:

- install MSYS2 yourself first if you want fewer surprises
- treat the automatic bootstrap as a convenience, not as the only path

For the automatic bootstrap to work, Windows must already have:

- `winget`

If `winget` is missing, `ld_patcher` cannot install MSYS2 automatically.

## 2. Install STM32CubeIDE or Another ARM GNU Toolchain

Official STM32CubeIDE page:

- https://www.st.com/en/development-tools/stm32cubeide.html

What the verify step needs:

- `arm-none-eabi-g++.exe`

The current verify flow searches for it in this order:

1. `STM32_GCC`
2. `PATH`
3. the GUI field `CubeIDE / Compiler Root`
4. standard `C:\ST` installs
5. standard `C:\Program Files\STMicroelectronics` installs

### Recommended option

Install STM32CubeIDE and let `ld_patcher` auto-detect it.

This is the easiest route because:

- the compiler is present
- the verify step knows how to find it
- the GUI tries to pre-fill the newest CubeIDE install it can find

### Alternative option

If you already have a separate ARM GNU toolchain, you can use it instead, as long as:

- it contains `arm-none-eabi-g++.exe`
- you either:
  - add it to `PATH`
  - or set `STM32_GCC`
  - or point the GUI `CubeIDE / Compiler Root` field at it

### Quick compiler sanity check

From PowerShell:

```powershell
& "C:\path\to\arm-none-eabi-g++.exe" --version
```

Success looks like:

- the command starts
- it prints the compiler version

## 3. If You Only Use A Ready-Built `ld_patcher.exe`

If you already have a built and deployed `release\ld_patcher.exe`, you do not
need to install Qt just to use the application.

You still need:

- the ST source ZIP or source tree
- MSYS2 for `Build`
- STM32CubeIDE or another ARM compiler for `Verify`

## 4. If You Want To Build `ld_patcher.exe` From Source

This is the developer/maintainer setup.

### Required software

- Qt 6 with a MinGW desktop kit
- CMake
- MinGW toolchain compatible with the chosen Qt kit
- PowerShell

Official Qt open-source download page:

- https://www.qt.io/development/download-open-source

Official CMake download page:

- https://cmake.org/download/

### What the build script uses

`build_ld_patcher.ps1` resolves and uses:

- `qmake`
- `mingw32-make`
- `windeployqt`
- CMake

It also:

- builds bundled `libzip`
- deploys Qt runtime files into the `release` folder

### What normal users can skip

If you are not editing `ld_patcher` source code, you can skip this whole
section.

## 5. Beginner-Safe Installation Order

If you want the least confusing order, do this:

1. get one supported ST source ZIP
2. install MSYS2
3. install the required MSYS2 packages
4. install STM32CubeIDE
5. only then start the GUI or manual workflow

That order removes most of the "why did Build/Verify fail?" noise.

## 6. Quick Pre-Flight Checklist

Before you start the real workflow, confirm:

- the ST source ZIP or extracted source tree exists
- `C:\msys64\msys2_shell.cmd` exists, or you know your real MSYS2 location
- the required MSYS2 packages are installed
- `arm-none-eabi-g++.exe` exists somewhere reachable by CubeIDE, `PATH`, or `STM32_GCC`

## 7. What Usually Fails First

The most common environment failures are:

- MSYS2 is not installed at all
- MSYS2 exists, but the required `mingw64` packages were never installed
- STM32CubeIDE is missing, so verify cannot find `arm-none-eabi-g++.exe`
- the user tries to build `ld_patcher.exe` from source without a Qt kit

If you install the prerequisites in this document first, those failures become
much less likely.

## See Also

- [README](../README.md)
- [Documentation Map](DOCUMENTATION_MAP.md)
- [CLI Reference](CLI_REFERENCE.md)
- [Manual Workflow](MANUAL_WORKFLOW.md)
- [CubeIDE Integration](CUBEIDE_INTEGRATION.md)
