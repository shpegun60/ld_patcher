# Manual Patch Package

This document explains the current manual patch package model.

## Source Of Truth

The current source of truth for manual patching is no longer an external
top-level `json_patch/` folder.

The current patch packages live inside `ld_patcher` itself:

- `payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900`
- `payloads/json_patch_v10_st_ld_14_3_rel1`

These are the payloads used by the active patch recipes.

That means:

- the GUI uses them
- the CLI uses them
- the manual workflow in this repository should use them too

## Package Contents

Each payload package contains the same two kinds of files.

### Real implementation files

- `ldjson_options.def`
- `ldjson_compat.h`
- `ldscript_json_impl.inc`

These are the real files used by the build after copying them into:

- `src/binutils/ld/`

### Hook fragments

- `hooks/ld.h.fragment`
- `hooks/ldlex.h.fragment`
- `hooks/lexsup.c.fragment`
- `hooks/ldlang.c.fragment`
- `hooks/Makefile.am.fragment`

These are copy-paste helpers for a human doing the port by hand.

They are not compiled by themselves.

## Exact Manual Rule

Use one payload package as the single source of truth for the whole manual
patching session.

Do not mix:

- files from one payload package
- with hook fragments from another payload package

## Manual Porting Rule

If you are patching by hand:

1. copy the three real implementation files into `src/binutils/ld/`
2. apply the five hook fragments to the matching source files
3. run:

```text
ld_patcher.exe --validate <working-tree>
```

That is the current maintained way to validate the manual port.

Recommended helper scripts:

```powershell
powershell -ExecutionPolicy Bypass -File .\ld_patcher\scripts\manual_reference\copy_patch_payload.ps1 `
  -SourceRoot "C:\path\to\gnu-tools-for-stm32-13.3.rel1"
```

```powershell
powershell -ExecutionPolicy Bypass -File .\ld_patcher\scripts\manual_reference\check_manual_patch.ps1 `
  -SourceRoot "C:\path\to\gnu-tools-for-stm32-13.3.rel1"
```

For the full step-by-step manual workflow, including inline copies of the current helper scripts, see:

- `docs/MANUAL_WORKFLOW.md`

## Relationship To Recipes

The active patch recipes describe:

- required files
- anchor checks
- patch operations
- idempotency rules
- post-apply checks

The payload package provides the actual patch contents consumed by those
recipes.

In other words:

- recipe = logic
- payload = patch material

## Which Package To Use

Use the payload that matches the detected/selected profile:

- ST `13.3.rel1.20250523-0900` -> `payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900`
- ST `14.3.rel1` -> `payloads/json_patch_v10_st_ld_14_3_rel1`

Practical source-tree mapping:

- if your working tree name begins with `gnu-tools-for-stm32-13.3.rel1`
  - use `payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900`
- if your working tree name begins with `gnu-tools-for-stm32-14.3.rel1`
  - use `payloads/json_patch_v10_st_ld_14_3_rel1`

## File Mapping

Real file copy targets:

- `<payload-root>\ldjson_options.def`
  - `-> <source-root>\src\binutils\ld\ldjson_options.def`
- `<payload-root>\ldjson_compat.h`
  - `-> <source-root>\src\binutils\ld\ldjson_compat.h`
- `<payload-root>\ldscript_json_impl.inc`
  - `-> <source-root>\src\binutils\ld\ldscript_json_impl.inc`

Manual glue fragment targets:

- `<payload-root>\hooks\ld.h.fragment`
  - `-> <source-root>\src\binutils\ld\ld.h`
- `<payload-root>\hooks\ldlex.h.fragment`
  - `-> <source-root>\src\binutils\ld\ldlex.h`
- `<payload-root>\hooks\lexsup.c.fragment`
  - `-> <source-root>\src\binutils\ld\lexsup.c`
- `<payload-root>\hooks\ldlang.c.fragment`
  - `-> <source-root>\src\binutils\ld\ldlang.c`
- `<payload-root>\hooks\Makefile.am.fragment`
  - `-> <source-root>\src\binutils\ld\Makefile.am`

## JSON Contract Reference

The patch package exists to produce the canonical linker JSON contract.

The current reference document is:

- `docs/LINKER_JSON_CONTRACT.md`

Reference sample outputs live under:

- `docs/reference_samples/`

## Relationship To Manual Scripts

Current manual helper scripts live under:

- `scripts/manual_reference/`

The intended pairings are:

- copy payload files:
  - `scripts/manual_reference/copy_patch_payload.ps1`
- check payload + glue:
  - `scripts/manual_reference/check_manual_patch.ps1`
- build manually:
  - `scripts/manual_reference/build_st_ld_manual.sh`
- verify manually:
  - `scripts/manual_reference/verify_drop_self_contained.ps1`
- package manually:
  - `scripts/manual_reference/package_cubeide_drop.ps1`

This gives you a fully manual path that still uses the same payloads and verify
assets as the application workflow.

