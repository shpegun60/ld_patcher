# CubeIDE Integration

This document describes the current, canonical way to use a packaged patched
linker in STM32CubeIDE.

## Who This Document Is For

Use this document after:

- `Build` and `Verify` already worked
- you already have a final package directory
- you now want STM32CubeIDE to call the patched linker

If you do not have a package directory yet, stop here and finish:

- [Prerequisites](PREREQUISITES.md)
- [Manual Workflow](MANUAL_WORKFLOW.md)
- or the GUI / CLI build flow first

## Before You Open CubeIDE

You need one real package directory on disk.

It should contain at least:

- `ld.exe`
- `ld.bfd.exe`
- `arm-none-eabi-ld.exe`
- `arm-none-eabi-ld.bfd.exe`

Important:

- CubeIDE needs the folder path, not the path to `ld.exe` itself
- the `-B` flag must point at the directory that contains `ld.exe`
- keep the trailing slash in the `-B".../"` form shown below

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

Beginner-safe interpretation:

- you are not replacing files inside CubeIDE
- you are only adding one extra linker-search-prefix flag
- CubeIDE will still invoke the normal compiler driver, but the driver will find your packaged `ld.exe` first

How to copy the package directory path from Windows Explorer:

1. open the package folder in Explorer
2. click the address bar
3. copy the full folder path
4. convert backslashes to forward slashes only if you want to match the examples exactly

Do not point CubeIDE at:

- `...\ld.exe`
- `...\arm-none-eabi-ld.exe`

Point it at the containing folder instead.

In CubeIDE, add the linker search-prefix flag:

```text
-B"C:/path/to/_cubeide-arm-linker-st-13.3.rel1-jsonpatch/"
```

That makes the compiler driver resolve `ld.exe` from your patched package
instead of the default ST linker location.

Example with a real-looking final package folder:

```text
-B"C:/Users/admin/Documents/my_workspace/gnu/gnu-tools-for-stm32/gnu-tools-for-stm32-13.3.rel1/build/_cubeide-arm-linker-st-13.3.rel1-jsonpatch/"
```

Practical recommendation:

- add the `-B".../"` flag first
- add `-Wl,-v` for one diagnostic build
- confirm the patched linker banner in the console
- only then add `--dump-script-json=...`

## First-Test Procedure

If this is your first time wiring the package into CubeIDE, do it in this exact order:

1. add only the `-B".../"` flag
2. add `-Wl,-v`
3. build once
4. confirm that the build log mentions your patched linker package
5. only after that, add `--dump-script-json=...`

This order is important because it separates:

- "CubeIDE found my linker"
- from
- "my linker also wrote JSON"

## Useful Diagnostic Flag

Add this when you want to confirm which linker is being used:

```text
-Wl,-v
```

In the build log you should then see the banner of the patched linker.

If you do not see the patched linker banner, fix the routing first before you
try to debug JSON generation.

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

Good beginner choice:

- point JSON into a folder inside the project build output
- avoid Desktop, network drives, temporary cloud-sync folders, or protected system locations until everything already works

## What To Look For In Logs

The healthy signs are:

- the effective linker path resolves into your package directory
- the linker banner matches the expected patched ST build
- `--help` includes `dump-script-json`
- the requested JSON file actually appears on disk

If all four signs are true, your CubeIDE integration is healthy.

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

Also check:

- you pointed `-B` to the folder, not to `ld.exe`
- the `-B` value still has the closing slash

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

If you want to keep things simple:

- keep `-B".../"` permanently
- use `-Wl,-v` only for one or two diagnostic builds
- keep `--dump-script-json=...` only when you actually want JSON output

## See Also

- [Prerequisites](PREREQUISITES.md)
- [Manual Workflow](MANUAL_WORKFLOW.md)
- [CLI Reference](CLI_REFERENCE.md)
- [Linker JSON Contract](LINKER_JSON_CONTRACT.md)
