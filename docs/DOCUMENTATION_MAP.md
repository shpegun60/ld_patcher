# Documentation Map

This is the canonical documentation set for `ld_patcher`.

If you want one current, working, non-duplicated source of truth, start here.

## Choose Your Path

If you are not sure which document you need, use this table:

- you want to click through the GUI and get a patched linker package:
  - start with [`../README.md`](../README.md)
  - then read [`PREREQUISITES.md`](PREREQUISITES.md)
- you want to type commands in PowerShell and let `ld_patcher.exe` do the work:
  - read [`PREREQUISITES.md`](PREREQUISITES.md)
  - read [`CLI_REFERENCE.md`](CLI_REFERENCE.md)
- you want to do the patch by hand and compare source files yourself:
  - read [`PREREQUISITES.md`](PREREQUISITES.md)
  - read [`MANUAL_PATCH_PACKAGE.md`](MANUAL_PATCH_PACKAGE.md)
  - then read [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)
- you already built the linker and now only need to connect it to CubeIDE:
  - read [`PREREQUISITES.md`](PREREQUISITES.md)
  - read [`CUBEIDE_INTEGRATION.md`](CUBEIDE_INTEGRATION.md)
- you want to teach `ld_patcher` about a new ST release:
  - read [`PREREQUISITES.md`](PREREQUISITES.md)
  - read [`ADDING_SUPPORT.md`](ADDING_SUPPORT.md)

## Common Terms Used In All Documents

- `source ZIP`
  - the official ST archive such as `gnu-tools-for-stm32-13.3.rel1.zip`
- `working tree`
  - the extracted source directory that you actually patch and build
- `payload package`
  - the folder under [`../payloads/`](../payloads/) that contains the patch files and hook fragments
- `drop dir`
  - the build output folder containing `ld.exe`, `ld.bfd.exe`, and related DLLs
- `package dir`
  - the final CubeIDE-ready folder created from the drop dir
- `profile-id`
  - the catalog id that selects the correct patch/build/verify path for one ST release

If you are new to PowerShell or terminals, read:

- [`../README.md`](../README.md)
- then [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)

## Core Documents

- [`README.md`](../README.md)
  - project overview
  - supported versions
  - workflow summary
  - build/runtime notes
- [`PREREQUISITES.md`](PREREQUISITES.md)
  - what must be installed before each workflow
  - exact MSYS2 package names
  - STM32CubeIDE / ARM compiler expectations
  - when Qt is needed and when it is not
- [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)
  - the current no-GUI path
  - how to patch/build/verify/package manually
  - exact commands for script-assisted manual work
  - inline copies of the current helper scripts at the step where each one is used
- [`CLI_REFERENCE.md`](CLI_REFERENCE.md)
  - complete command-line interface reference
  - arguments, outputs, exit codes, and practical command chains
- [`CUBEIDE_INTEGRATION.md`](CUBEIDE_INTEGRATION.md)
  - how to use the produced linker package in STM32CubeIDE
  - `-B...`, `-Wl,-v`, and JSON dump usage
- [`MANUAL_PATCH_PACKAGE.md`](MANUAL_PATCH_PACKAGE.md)
  - how the manual patch package is organized
  - which payloads/files are the source of truth
  - how to port the patch by hand
- [`ADDING_SUPPORT.md`](ADDING_SUPPORT.md)
  - cookbook for adding support for a new ST compiler/source version
  - how to create payloads, recipes, profiles, and catalog entries
- [`LINKER_JSON_CONTRACT.md`](LINKER_JSON_CONTRACT.md)
  - canonical JSON contract
  - current top-level fields and payload rules
- [`WORKSPACE_CLEANUP.md`](WORKSPACE_CLEANUP.md)
  - what can be deleted from the workspace after consolidation

## Reference Material

- [`reference_samples/`](reference_samples/)
  - sample JSON outputs and contract examples
- [`../scripts/manual_reference/`](../scripts/manual_reference/)
  - real manual helper scripts preserved inside `ld_patcher`
  - these are the same scripts embedded inline in [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)

## Recommended Reading Order

If you are new to the project:

1. [`README.md`](../README.md)
2. [`DOCUMENTATION_MAP.md`](DOCUMENTATION_MAP.md)
3. [`PREREQUISITES.md`](PREREQUISITES.md)
4. [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)
5. [`CLI_REFERENCE.md`](CLI_REFERENCE.md)
6. [`CUBEIDE_INTEGRATION.md`](CUBEIDE_INTEGRATION.md)
7. [`LINKER_JSON_CONTRACT.md`](LINKER_JSON_CONTRACT.md)

If you have never used the command line before:

1. [`README.md`](../README.md)
2. [`PREREQUISITES.md`](PREREQUISITES.md)
3. [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)
4. [`CLI_REFERENCE.md`](CLI_REFERENCE.md)
5. only then move to the maintainer docs

If you want to patch a source tree by hand:

1. [`PREREQUISITES.md`](PREREQUISITES.md)
2. [`MANUAL_PATCH_PACKAGE.md`](MANUAL_PATCH_PACKAGE.md)
3. [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)
4. [`CUBEIDE_INTEGRATION.md`](CUBEIDE_INTEGRATION.md)

If you want to add support for a new ST version:

1. [`ADDING_SUPPORT.md`](ADDING_SUPPORT.md)
2. [`MANUAL_PATCH_PACKAGE.md`](MANUAL_PATCH_PACKAGE.md)
3. [`CLI_REFERENCE.md`](CLI_REFERENCE.md)
4. inspect the closest existing files under [`../catalog/profiles/`](../catalog/profiles/) and [`../catalog/recipes/`](../catalog/recipes/)

If you want to clean up the old workspace:

1. [`WORKSPACE_CLEANUP.md`](WORKSPACE_CLEANUP.md)
