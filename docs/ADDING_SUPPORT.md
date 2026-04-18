# Adding Support For A New Compiler Version

This document is the practical cookbook for teaching `ld_patcher` about a new
ST `gnu-tools-for-stm32` source package.

The goal is to add support without inventing a second patch system. New support
must plug into the same current model:

- `payloads/` contain the patch material
- `catalog/profiles/` route detection
- `catalog/recipes/patch/` decide compatibility and apply operations
- `catalog/recipes/build/` describe how to build
- `catalog/recipes/verify/` prove that the result works
- `catalog/catalog.json` decides what is active

## When You Need A New Support Entry

You usually need a new support entry when:

- ST publishes a new `gnu-tools-for-stm32` release tag
- an older patch recipe no longer validates because anchors moved
- a new version still belongs to the same family, but source details changed
- build/verify still follow the same broad path, but you need a new profile and maybe a new patch recipe

## The Moving Parts

For a new version, the files you will most often touch are:

- `catalog/catalog.json`
- `catalog/profiles/*.json`
- `catalog/recipes/patch/*.json`
- `catalog/recipes/build/*.json`
- `catalog/recipes/verify/*.json`
- `payloads/<new_payload_package>/...`

Schema references:

- `catalog/schemas/version_profile.schema.json`
- `catalog/schemas/patch_recipe.schema.json`
- `catalog/schemas/build_recipe.schema.json`
- `catalog/schemas/verify_recipe.schema.json`

## Recommended Order

Use this order. It keeps the work structured and reduces false starts.

1. inspect the new ST source package
2. decide whether an existing payload package is still valid or needs a new copy
3. create or update the patch recipe
4. create or update the build recipe
5. decide whether the existing verify recipes are still sufficient
6. create the new version profile
7. add the new files to `catalog/catalog.json`
8. run the full validation and build chain
9. only then promote the status from `candidate` to `verified_local`

## Step 1: Inspect The New ST Source Package

Start with the new source ZIP or extracted source tree.

You want to answer these questions:

- what is the real folder/archive root name?
- what is `src/gcc/gcc/BASE-VER`?
- what does `build-common.sh` say for product and release level?
- are the expected linker source files still present?
- did the known anchor points move?

The main source files to inspect are usually:

- `src/binutils/ld/ld.h`
- `src/binutils/ld/ldlex.h`
- `src/binutils/ld/lexsup.c`
- `src/binutils/ld/ldlang.c`
- `src/binutils/ld/Makefile.am`
- `src/binutils/libiberty/pex-win32.c`
- `build-common.sh`
- `src/gcc/gcc/BASE-VER`

Use the current supported versions as references:

- `catalog/profiles/st_gnu_tools_for_stm32_13_3_rel1_20250523_0900.json`
- `catalog/profiles/st_gnu_tools_for_stm32_14_3_rel1.json`

## Step 2: Decide Whether You Need A New Payload Package

The payload package is the patch material itself.

It contains:

- real implementation files copied into `src/binutils/ld/`
- hook fragments inserted or appended into target source files

Current examples:

- `payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900`
- `payloads/json_patch_v10_st_ld_14_3_rel1`

Use the existing payload package unchanged if:

- the implementation files stay the same
- the hook fragments also stay valid

Create a new payload package if:

- hook fragments need to change for the new version
- new patch files are needed
- the longpath fallback or Makefile fragment changes

Recommended practice:

- copy the closest existing payload package
- rename it to a version-specific directory
- edit only the files that truly changed

Example:

```text
payloads/json_patch_v10_st_ld_15_1_rel1
```

## Step 3: Create Or Update The Patch Recipe

The patch recipe is where compatibility and operations are defined.

Current examples:

- `catalog/recipes/patch/json_patch_v10_st_ld.json`
- `catalog/recipes/patch/json_patch_v10_st_ld_14_3_rel1.json`

The patch recipe controls:

- patch package root
- required source files
- applicability checks
- operations
- idempotency rules
- post-apply checks
- compatibility result mapping

### What To Edit

At minimum, review these sections:

- `patch_package`
- `required_files`
- `applicability_checks`
- `operations`
- `idempotency_rules`
- `post_apply_checks`

### Typical New-Version Work

For a new ST version, the most common reason to fork a patch recipe is:

- one or more `match_regex` anchors moved
- a previously unique anchor is now ambiguous
- a new fallback hook is needed for a changed source file

Typical patch operation types in the current engine:

- `copy_file`
- `insert_after_regex`
- `append_if_missing`

Current engine behavior is data-driven for file paths and checks, but not for
new operation types. If you need a brand-new operation type, that is a code
change in `workflowservice.cpp`, not only a JSON edit.

### Practical Rule For Anchors

Use the narrowest stable anchor that still describes the real source layout.

Bad anchor style:

- very generic regex that could match multiple places in a newer source tree

Good anchor style:

- regex tied to the finalized local code block you actually want to patch

Example from `14.3`:

- the runtime hook anchor was narrowed to the finalized `lang_process()` tail
  instead of relying on a generic `lang_end ();` match

### Initial Status

When you first add a new patch recipe:

- use `"status": "candidate"`

Only switch to:

- `"status": "verified_local"`

after the full chain really passes.

## Step 4: Build Recipe Strategy

The build recipe describes how to build the patched linker.

Current examples:

- `catalog/recipes/build/msys2_mingw64_st_ld_13_3_verified.json`
- `catalog/recipes/build/msys2_mingw64_st_ld_14_3_verified.json`

If the new version builds the same way, the simplest rule is:

- copy the closest build recipe
- update the id/display name/notes
- keep the same structure unless the build really changed

Important sections:

- `environment`
- `working_directory_template`
- `clean_command`
- `configure_command`
- `build_command`
- `required_tools`
- `expected_outputs`
- `artifact_collection`

### What Usually Stays The Same

For ST releases in the same family, these often stay unchanged:

- MSYS2 MinGW64 environment
- `configure` triplets
- `make all-ld`
- `make install-ld`
- expected linker binary names

### What Often Needs Checking

- longpath include handling
- additional runtime DLL collection
- path to configure script
- whether the build still produces the same `drop_dir` artifacts

### Build Status

Use:

- `"status": "candidate"`

until build really succeeds in practice.

## Step 5: Verify Recipe Strategy

The active profiles currently use:

- `catalog/recipes/verify/sanity_cli.json`
- `catalog/recipes/verify/json_smoke_self_contained.json`

In many cases, you do not need a new verify recipe for a new version.

Reuse the existing verify recipes if:

- the built drop layout is unchanged
- the patched linker still exposes `--dump-script-json`
- the same self-contained smoke project is enough to prove the JSON contract

Create a new verify recipe only if:

- the verification contract changes
- new artifacts must be produced
- the current self-contained test no longer proves the right behavior

## Step 6: Create The New Version Profile

The profile ties everything together.

Current examples:

- `catalog/profiles/st_gnu_tools_for_stm32_13_3_rel1_20250523_0900.json`
- `catalog/profiles/st_gnu_tools_for_stm32_14_3_rel1.json`

The profile defines:

- selectors
- detection hints
- patch recipe id
- build recipe ids
- verify recipe ids
- priority
- status

### What To Update

At minimum:

- `id`
- `display_name`
- `selectors.tags`
- `selectors.archive_roots`
- `selectors.folder_name_patterns`
- `detection_hints`
- `patch_recipe_id`
- `build_recipe_ids`
- `verify_recipe_ids`
- `priority`
- `status`

### Practical Detection Rules

Use multiple signals together:

- archive root / folder name
- product markers in `build-common.sh`
- release level in `build-common.sh`
- base GCC version from `src/gcc/gcc/BASE-VER`
- expected linker source layout

Do not rely only on the version string in the filename.

## Step 7: Index The New Files In `catalog/catalog.json`

`catalog/catalog.json` is authoritative.

This is critical:

- if you add a JSON file but do not index it in `catalog/catalog.json`
  it will not be active
- if you leave stale files on disk but remove them from the index
  they are ignored

For a new supported version, update:

- `profiles`
- `patch_recipes`
- `build_recipes`
- `verify_recipes` only if you created new verify recipes

## Step 8: Validate The New Version

Use the real source ZIP or extracted source tree.

Recommended sequence:

1. `--detect`
2. `--validate`
3. `--extract` if starting from ZIP
4. `--apply`
5. `--build`
6. `--verify`
7. `--package`

Example:

```text
ld_patcher.exe --detect C:\work\gnu-tools-for-stm32-15.1.rel1.zip
ld_patcher.exe --validate C:\work\gnu-tools-for-stm32-15.1.rel1.zip
ld_patcher.exe --extract C:\work\gnu-tools-for-stm32-15.1.rel1.zip C:\work\trees gnu-tools-for-stm32-15.1.rel1
ld_patcher.exe --apply st_gnu_tools_for_stm32_15_1_rel1 C:\work\trees\gnu-tools-for-stm32-15.1.rel1
ld_patcher.exe --build st_gnu_tools_for_stm32_15_1_rel1 C:\work\trees\gnu-tools-for-stm32-15.1.rel1
ld_patcher.exe --verify st_gnu_tools_for_stm32_15_1_rel1 C:\work\trees\gnu-tools-for-stm32-15.1.rel1\build\gnu-tools-for-stm32-15.1.rel1_msys2_mingw64_st_ld_15_1_verified_drop C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE
ld_patcher.exe --package C:\work\trees\gnu-tools-for-stm32-15.1.rel1\build\gnu-tools-for-stm32-15.1.rel1_msys2_mingw64_st_ld_15_1_verified_drop C:\work\trees\gnu-tools-for-stm32-15.1.rel1\build\_cubeide-arm-linker-st-15.1.rel1-jsonpatch
```

### What Success Means

Do not call a version supported until all of these are true:

- detect selects the intended profile
- validate says `applicable=true`
- apply succeeds
- build succeeds
- verify succeeds
- package succeeds
- the final package works as a CubeIDE linker package

## Step 9: Promote Statuses

After the full local chain passes:

- update the new profile from `candidate` to `verified_local`
- update the new patch recipe from `candidate` to `verified_local`
- update the new build recipe from `candidate` to `verified_local` if you created one
- update verify recipe status only if you created a new verify recipe and proved it

Also update:

- `README.md`
- supported versions lists
- notes inside the JSON files

## Recommended File-Copy Pattern

If you are adding support for a new version, the lowest-friction pattern is:

1. copy the closest existing payload package
2. copy the closest existing patch recipe
3. copy the closest existing build recipe only if needed
4. create a new profile
5. index the new files in `catalog/catalog.json`

Then tighten the anchors and notes until the new version validates cleanly.

## Practical Example Mapping

If you were adding a hypothetical `15.1.rel1`, a typical shape would be:

- new payload:
  - `payloads/json_patch_v10_st_ld_15_1_rel1`
- new patch recipe:
  - `catalog/recipes/patch/json_patch_v10_st_ld_15_1_rel1.json`
- new build recipe if needed:
  - `catalog/recipes/build/msys2_mingw64_st_ld_15_1_verified.json`
- existing verify recipes reused:
  - `sanity_cli`
  - `json_smoke_self_contained`
- new profile:
  - `catalog/profiles/st_gnu_tools_for_stm32_15_1_rel1.json`

## What Usually Breaks First

When a new ST version arrives, the most common breakpoints are:

- `ldlang.c` runtime hook anchor
- `lexsup.c` option-row anchor
- `lexsup.c` switch anchor
- `pex-win32.c` longpath fallback anchor
- package/drop naming assumptions

That is where you should look first.

## Manual And Programmatic Paths Must Stay Unified

Do not create a second standalone patch bundle outside `ld_patcher`.

The intended model is:

- program workflow uses `payloads/` and `catalog/`
- manual workflow uses those same `payloads/` and `verify_assets/`

If you update support for a new version, update both:

- the catalog-driven workflow
- the manual docs that describe how to use the same payload package by hand

The shared source of truth is the important part.
