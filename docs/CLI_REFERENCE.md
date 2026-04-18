# CLI Reference

This document describes the command-line interface exposed by `ld_patcher.exe`.

The CLI uses the same backend as the GUI workflow. It is not a second
implementation.

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

- `catalog/catalog.json`
- `payloads/`
- `scripts/`
- `verify_assets/`

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
