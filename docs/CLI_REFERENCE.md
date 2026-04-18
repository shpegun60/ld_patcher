# CLI Reference

This document describes the command-line interface exposed by `ld_patcher.exe`.

The CLI uses the same backend as the GUI workflow. It is not a second
implementation.

## Who This Document Is For

Use this document if:

- you want to run `ld_patcher.exe` from PowerShell instead of the GUI
- you want a repeatable copy-paste command chain
- you want to automate patch/build/verify/package in scripts or CI

If you want to patch the ST tree by hand without `ld_patcher.exe`, use
[Manual Workflow](MANUAL_WORKFLOW.md) instead.

Before you run real commands, install the required dependencies from:

- [Prerequisites](PREREQUISITES.md)

## Before You Type Anything

The beginner-safe way to use the CLI is:

1. open PowerShell
2. change directory into the repository root that contains `ld_patcher`
3. run `ld_patcher.exe` commands from there

Example:

```powershell
cd C:\Users\admin\Documents\my_workspace\gnu\gnu-tools-for-stm32
```

If you built `ld_patcher.exe` in the default Qt build tree, the usual binary is:

```text
ld_patcher\build\Desktop_Qt_6_10_1_MinGW_64_bit-Debug\release\ld_patcher.exe
```

You can call it either:

- by full path every time
- or after `cd`-ing into its directory

Example with full path:

```powershell
& .\ld_patcher\build\Desktop_Qt_6_10_1_MinGW_64_bit-Debug\release\ld_patcher.exe --validate .\gnu-tools-for-stm32-13.3.rel1.zip
```

## If You Have Never Used PowerShell

The minimum you need to know:

- `cd <path>`
  - changes the current folder
- `& <path-to-exe> <args>`
  - runs an `.exe` by full path
- quoted paths like `"C:\some path\file.zip"`
  - are required when the path contains spaces

How to open PowerShell:

1. open Windows Explorer
2. go to your workspace folder
3. click the address bar
4. type `powershell`
5. press Enter

Now commands run from that folder.

## Placeholder Glossary

The CLI docs use these placeholders repeatedly:

- `<source-dir-or-zip>`
  - either the ST source ZIP or an extracted source directory
- `<zip>`
  - the ST source ZIP file
- `<parent-dir>`
  - the folder that will contain the extracted working tree
- `<directory-name>`
  - the new folder name created under `<parent-dir>`
- `<profile-id>`
  - the catalog id for the selected ST release, for example:
    - `st_gnu_tools_for_stm32_13_3_rel1_20250523_0900`
    - `st_gnu_tools_for_stm32_14_3_rel1`
- `<working-root>`
  - the extracted or existing writable source directory
- `<drop-dir>`
  - the build output folder containing `ld.exe` and the other linker binaries
- `<package-dir>`
  - the final CubeIDE-ready folder
- `[cubeide-path]`
  - optional path to STM32CubeIDE root or directly to an ARM compiler root

## How To Get The Right `profile-id`

`--apply`, `--build`, and `--verify` require an explicit `profile-id`.

The safest way to get it is:

1. run `--validate` on your ZIP or source directory
2. read the printed `Profile id`
3. copy that exact id into the later commands

If you skip this and guess the id by hand, you can easily run the wrong recipe.

## Command Set

Supported commands:

- `--detect`
- `--validate`
- `--extract`
- `--apply`
- `--build`
- `--verify`
- `--package`

The program switches into CLI mode only when one of these commands is present
as the first argument. Otherwise it starts the Qt GUI.

## Catalog Resolution

All CLI commands load the same catalog as the GUI.

At runtime, `ld_patcher` looks for:

- [`../catalog/catalog.json`](../catalog/catalog.json)
- [`../payloads/`](../payloads/)
- [`../scripts/`](../scripts/)
- [`../verify_assets/`](../verify_assets/)

starting from:

- the current working directory
- then the application directory
- then parent directories

That means the safest way to use the CLI is to run it from inside the
`ld_patcher` tree or from a deployed copy that still contains those folders.

## `--detect`

Detects the best matching profile for a source directory or source ZIP.

Usage:

```text
ld_patcher.exe --detect <source-dir-or-zip>
```

Input:

- ST `gnu-tools-for-stm32` ZIP
- extracted ST `gnu-tools-for-stm32` source directory

Output:

- source inspection fields such as input kind, inferred root, product markers
- best candidate profile
- matched profile if the detector reached a real match
- evidence lines and warnings

Typical use:

```text
ld_patcher.exe --detect C:\work\gnu-tools-for-stm32-14.3.rel1.zip
```

What success looks like:

- the command prints source inspection details
- you see a `best_candidate_profile`
- on a fully recognized input you also see a real matched profile

What to do next:

- if detection looks correct, run `--validate` on the same input

## `--validate`

Runs analysis, profile selection, and patch compatibility checks.

Usage:

```text
ld_patcher.exe --validate <source-dir-or-zip>
```

Output includes:

- selected profile id
- working root
- patch recipe id
- patch package root
- `applicable=true|false`
- `already_patched=true|false`
- support level
- individual validation checks
- idempotency checks

Exit codes:

- `0` = validation succeeded and the source is applicable
- `1` = catalog load failure
- `2` = analysis or validation backend failure
- `3` = validation completed, but the source is not applicable

Typical use:

```text
ld_patcher.exe --validate C:\work\gnu-tools-for-stm32-13.3.rel1
```

What success looks like:

- exit code `0`
- `applicable=true`
- `already_patched=false` for a fresh source tree or ZIP
- `already_patched=true` for a tree you have already patched
- a visible patch recipe id and patch package root

What to do next:

- for ZIP input, go to `--extract`
- for directory input, go to `--apply`

## `--extract`

Extracts a ZIP into a writable working directory.

Usage:

```text
ld_patcher.exe --extract <zip> <parent-dir> <directory-name>
```

Parameters:

- `<zip>`: source ZIP
- `<parent-dir>`: parent folder where the extracted working tree should be created
- `<directory-name>`: final directory name to create under `<parent-dir>`

Output:

- `working_root=...`
- `skipped=true|false`
- message lines

Exit codes:

- `0` = extraction succeeded
- `2` = extraction failed

Typical use:

```text
ld_patcher.exe --extract C:\work\gnu-tools-for-stm32-13.3.rel1.zip C:\work\trees gnu-tools-for-stm32-13.3.rel1
```

What success looks like:

- exit code `0`
- `working_root=...` prints the new extracted directory
- the directory really exists on disk afterward

What to do next:

- use the printed `working_root` as `<working-root>` in `--apply`

## `--apply`

Applies the patch recipe selected by the given profile to a writable working tree.

Usage:

```text
ld_patcher.exe --apply <profile-id> <working-root>
```

Parameters:

- `<profile-id>`: for example `st_gnu_tools_for_stm32_13_3_rel1_20250523_0900`
- `<working-root>`: extracted or existing writable source directory

What it does:

- resolves the profile
- resolves the profile's patch recipe
- resolves the payload package root
- runs copy/insert/append operations
- runs post-apply validation

Output:

- `working_root=...`
- `already_patched=true|false`
- message lines

Exit codes:

- `0` = apply succeeded
- `1` = catalog load failure
- `2` = apply failed

Typical use:

```text
ld_patcher.exe --apply st_gnu_tools_for_stm32_14_3_rel1 C:\work\gnu-tools-for-stm32-14.3.rel1
```

What success looks like:

- exit code `0`
- the output says apply succeeded
- the working tree now contains:
  - `src/binutils/ld/ldjson_options.def`
  - `src/binutils/ld/ldjson_compat.h`
  - `src/binutils/ld/ldscript_json_impl.inc`

What to do next:

- run `--build` on the same `<working-root>`

## `--build`

Builds the patched linker with a selected build recipe.

Usage:

```text
ld_patcher.exe --build <profile-id> <working-root> [build-recipe-id] [build-root-override]
```

Parameters:

- `<profile-id>`: selected version profile
- `<working-root>`: writable patched source tree
- `[build-recipe-id]`: optional explicit build recipe override
- `[build-root-override]`: optional explicit build/output root

If `[build-recipe-id]` is omitted, the first build recipe attached to the
profile is used.

If `[build-root-override]` is omitted, the standard default is:

- `<working-root>\build`

Output:

- `recipe=...`
- `drop_dir=...`
- `package_dir=...`
- `build_dir=...`
- message lines

Exit codes:

- `0` = build succeeded
- `1` = catalog load failure
- `2` = build failed

Typical use:

```text
ld_patcher.exe --build st_gnu_tools_for_stm32_13_3_rel1_20250523_0900 C:\work\gnu-tools-for-stm32-13.3.rel1
```

Explicit override example:

```text
ld_patcher.exe --build st_gnu_tools_for_stm32_14_3_rel1 C:\work\gnu-tools-for-stm32-14.3.rel1 msys2_mingw64_st_ld_14_3_verified C:\work\custom-build-root
```

What success looks like:

- exit code `0`
- you get printed paths for:
  - `build_dir`
  - `drop_dir`
  - `package_dir`
- the `drop_dir` contains at least:
  - `ld.exe`
  - `ld.bfd.exe`
  - `arm-none-eabi-ld.exe`
  - `arm-none-eabi-ld.bfd.exe`

What to do next:

- use the printed `drop_dir` for `--verify`
- optionally use the printed `package_dir` later for `--package`

## `--verify`

Runs the verify recipes attached to the selected profile against a build drop.

Usage:

```text
ld_patcher.exe --verify <profile-id> <drop-dir> [cubeide-path]
```

Parameters:

- `<profile-id>`: selected version profile
- `<drop-dir>`: build drop directory containing the built linker binaries
- `[cubeide-path]`: optional STM32CubeIDE root or compiler root

What the active profiles currently run:

- `sanity_cli`
- `json_smoke_self_contained`

Output:

- `drop_dir=...`
- `recipe=<verify-recipe-id>|PASS|FAIL`

Exit codes:

- `0` = all required verify recipes passed
- `1` = catalog load failure
- `2` = verify failed

Typical use:

```text
ld_patcher.exe --verify st_gnu_tools_for_stm32_14_3_rel1 C:\work\gnu-tools-for-stm32-14.3.rel1\build\gnu-tools-for-stm32-14.3.rel1_msys2_mingw64_st_ld_14_3_verified_drop C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE
```

What success looks like:

- exit code `0`
- both active verify recipes pass:
  - `sanity_cli`
  - `json_smoke_self_contained`
- the drop dir now contains:
  - `_verify_smoke_self\ldpatcher_self_smoke.elf`
  - `_verify_smoke_self\ldpatcher_self_smoke.map`
  - `_verify_smoke_self\ldpatcher_self_smoke.json`

What to do next:

- run `--package` if you want a clean CubeIDE-ready folder

## `--package`

Copies the built linker artifacts into a final CubeIDE-ready package folder.

Usage:

```text
ld_patcher.exe --package <source-drop-dir> <package-dir>
```

Parameters:

- `<source-drop-dir>`: build drop directory
- `<package-dir>`: final package directory to create or replace

What it copies:

- `ld.exe`
- `ld.bfd.exe`
- `arm-none-eabi-ld.exe`
- `arm-none-eabi-ld.bfd.exe`
- `libwinpthread-1.dll`
- `libzstd.dll`

Output:

- `source_drop_dir=...`
- `package_dir=...`
- `skipped=true|false`
- message lines

Exit codes:

- `0` = packaging succeeded
- `2` = packaging failed

Typical use:

```text
ld_patcher.exe --package C:\work\gnu-tools-for-stm32-13.3.rel1\build\gnu-tools-for-stm32-13.3.rel1_msys2_mingw64_st_ld_13_3_verified_drop C:\work\gnu-tools-for-stm32-13.3.rel1\build\_cubeide-arm-linker-st-13.3.rel1-jsonpatch
```

What success looks like:

- exit code `0`
- the `package_dir` exists on disk
- it contains:
  - `ld.exe`
  - `ld.bfd.exe`
  - `arm-none-eabi-ld.exe`
  - `arm-none-eabi-ld.bfd.exe`
  - `libwinpthread-1.dll`
  - `libzstd.dll`

What to do next:

- use that `package_dir` in STM32CubeIDE through `-B".../"`
- follow [CubeIDE Integration](CUBEIDE_INTEGRATION.md)

## Practical CLI Chains

### ZIP input

```text
ld_patcher.exe --validate C:\work\gnu-tools-for-stm32-14.3.rel1.zip
ld_patcher.exe --extract C:\work\gnu-tools-for-stm32-14.3.rel1.zip C:\work\trees gnu-tools-for-stm32-14.3.rel1
ld_patcher.exe --apply st_gnu_tools_for_stm32_14_3_rel1 C:\work\trees\gnu-tools-for-stm32-14.3.rel1
ld_patcher.exe --build st_gnu_tools_for_stm32_14_3_rel1 C:\work\trees\gnu-tools-for-stm32-14.3.rel1
ld_patcher.exe --verify st_gnu_tools_for_stm32_14_3_rel1 C:\work\trees\gnu-tools-for-stm32-14.3.rel1\build\gnu-tools-for-stm32-14.3.rel1_msys2_mingw64_st_ld_14_3_verified_drop C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE
ld_patcher.exe --package C:\work\trees\gnu-tools-for-stm32-14.3.rel1\build\gnu-tools-for-stm32-14.3.rel1_msys2_mingw64_st_ld_14_3_verified_drop C:\work\trees\gnu-tools-for-stm32-14.3.rel1\build\_cubeide-arm-linker-st-14.3.rel1-jsonpatch
```

### Directory input

```text
ld_patcher.exe --validate C:\work\gnu-tools-for-stm32-13.3.rel1
ld_patcher.exe --apply st_gnu_tools_for_stm32_13_3_rel1_20250523_0900 C:\work\gnu-tools-for-stm32-13.3.rel1
ld_patcher.exe --build st_gnu_tools_for_stm32_13_3_rel1_20250523_0900 C:\work\gnu-tools-for-stm32-13.3.rel1
ld_patcher.exe --verify st_gnu_tools_for_stm32_13_3_rel1_20250523_0900 C:\work\gnu-tools-for-stm32-13.3.rel1\build\gnu-tools-for-stm32-13.3.rel1_msys2_mingw64_st_ld_13_3_verified_drop C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE
ld_patcher.exe --package C:\work\gnu-tools-for-stm32-13.3.rel1\build\gnu-tools-for-stm32-13.3.rel1_msys2_mingw64_st_ld_13_3_verified_drop C:\work\gnu-tools-for-stm32-13.3.rel1\build\_cubeide-arm-linker-st-13.3.rel1-jsonpatch
```

## Notes

- `--apply`, `--build`, and `--verify` do not auto-detect the profile id for you. You pass the selected profile explicitly.
- `--verify` expects the built drop directory, not the source tree.
- `--package` is optional. `Build` already creates the drop directory used by `Verify`.

If you are new to the CLI, the safest habit is:

1. `--validate`
2. copy the printed profile id
3. keep reusing that exact id for the rest of the session

## See Also

- [Prerequisites](PREREQUISITES.md)
- [Manual Workflow](MANUAL_WORKFLOW.md)
- [CubeIDE Integration](CUBEIDE_INTEGRATION.md)
- [Adding Support For A New Compiler Version](ADDING_SUPPORT.md)
