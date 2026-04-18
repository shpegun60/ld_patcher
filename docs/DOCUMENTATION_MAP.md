# Documentation Map

This is the canonical documentation set for `ld_patcher`.

If you want one current, working, non-duplicated source of truth, start here.

## Core Documents

- `README.md`
  - project overview
  - supported versions
  - workflow summary
  - build/runtime notes
- `docs/MANUAL_WORKFLOW.md`
  - the current no-GUI path
  - how to patch/build/verify/package manually
  - exact commands for script-assisted manual work
  - inline copies of the current helper scripts at the step where each one is used
- `docs/CLI_REFERENCE.md`
  - complete command-line interface reference
  - arguments, outputs, exit codes, and practical command chains
- `docs/CUBEIDE_INTEGRATION.md`
  - how to use the produced linker package in STM32CubeIDE
  - `-B...`, `-Wl,-v`, and JSON dump usage
- `docs/MANUAL_PATCH_PACKAGE.md`
  - how the manual patch package is organized
  - which payloads/files are the source of truth
  - how to port the patch by hand
- `docs/ADDING_SUPPORT.md`
  - cookbook for adding support for a new ST compiler/source version
  - how to create payloads, recipes, profiles, and catalog entries
- `docs/LINKER_JSON_CONTRACT.md`
  - canonical JSON contract
  - current top-level fields and payload rules
- `docs/WORKSPACE_CLEANUP.md`
  - what can be deleted from the workspace after consolidation

## Reference Material

- `docs/reference_samples/`
  - sample JSON outputs and contract examples
- `scripts/manual_reference/`
  - real manual helper scripts preserved inside `ld_patcher`
  - these are the same scripts embedded inline in `docs/MANUAL_WORKFLOW.md`

## Recommended Reading Order

If you are new to the project:

1. `README.md`
2. `docs/MANUAL_WORKFLOW.md`
3. `docs/CLI_REFERENCE.md`
4. `docs/CUBEIDE_INTEGRATION.md`
5. `docs/LINKER_JSON_CONTRACT.md`

If you want to patch a source tree by hand:

1. `docs/MANUAL_PATCH_PACKAGE.md`
2. `docs/MANUAL_WORKFLOW.md`
3. `docs/CUBEIDE_INTEGRATION.md`

If you want to add support for a new ST version:

1. `docs/ADDING_SUPPORT.md`
2. `docs/MANUAL_PATCH_PACKAGE.md`
3. `docs/CLI_REFERENCE.md`
4. inspect the closest existing files under `catalog/profiles/` and `catalog/recipes/`

If you want to clean up the old workspace:

1. `docs/WORKSPACE_CLEANUP.md`
