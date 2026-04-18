# CubeIDE Integration

This document describes the current, canonical way to use a packaged patched
linker in STM32CubeIDE.

## What You Get

The final package directory created by `ld_patcher` contains:

- `ld.exe`
- `ld.bfd.exe`
- `arm-none-eabi-ld.exe`
- `arm-none-eabi-ld.bfd.exe`
- required runtime DLLs

Typical package directory names:

- `_cubeide-arm-linker-st-13.3.rel1-jsonpatch`
- `_cubeide-arm-linker-st-14.3.rel1-jsonpatch`

The exact suffix follows the real working-tree name.

## How CubeIDE Should Use It

Typical STM32CubeIDE path:

1. open project properties
2. go to:
   - `C/C++ Build`
   - `Settings`
3. in `Tool Settings`, open the linker section:
   - usually `MCU GCC Linker`
   - in some configurations this may appear as `MCU/MPU GCC Linker`
4. find the linker flags / miscellaneous flags field
5. add the `-B".../"` prefix there

In CubeIDE, add the linker search-prefix flag:

```text
-B"C:/path/to/_cubeide-arm-linker-st-13.3.rel1-jsonpatch/"
```

That makes the compiler driver resolve `ld.exe` from your patched package
instead of the default ST linker location.

Practical recommendation:

- add the `-B".../"` flag first
- add `-Wl,-v` for one diagnostic build
- confirm the patched linker banner in the console
- only then add `--dump-script-json=...`

## Useful Diagnostic Flag

Add this when you want to confirm which linker is being used:

```text
-Wl,-v
```

In the build log you should then see the banner of the patched linker.

## JSON Output Flag

If you want the linker to emit JSON:

```text
-Wl,--dump-script-json=C:/path/to/out.json
```

You can then inspect the output with your own tooling or feed it into downstream
analysis.

Recommended first path:

- write JSON somewhere inside your build/output directory
- avoid paths that the linker cannot create or overwrite

## What To Look For In Logs

The healthy signs are:

- the effective linker path resolves into your package directory
- the linker banner matches the expected patched ST build
- `--help` includes `dump-script-json`
- the requested JSON file actually appears on disk

## Quick Sanity Checks Outside CubeIDE

Version:

```text
ld.exe --version
```

Help:

```text
ld.exe --help
```

Feature presence:

```text
ld.exe --help | findstr dump-script-json
```

Recommended linker package sanity set:

- `ld.exe --version`
- `ld.exe --help | findstr dump-script-json`
- `objdump -p ld.exe` if you want to inspect runtime DLL dependencies

## Typical Failure Modes

### CubeIDE still uses ST's original linker

Usually this means:

- the `-B` path is wrong
- the quotes are wrong
- the package directory does not actually contain `ld.exe`
- the flag was placed in the wrong settings field

### `ld.exe` does not start

Usually this means:

- runtime DLLs are missing from the package directory

### JSON file is not produced

Usually this means:

- the patched linker is not actually the one being used
- or the command line does not really contain `--dump-script-json=...`
- or the output path is invalid for the build environment

## Recommended Practice

For normal use:

1. run `Package (CubeIDE)` in `ld_patcher`
2. point CubeIDE to the resulting package with `-B...`
3. add `-Wl,-v` once to confirm the routing
4. optionally add `--dump-script-json=...`
5. remove `-Wl,-v` again if you no longer want the extra banner noise
