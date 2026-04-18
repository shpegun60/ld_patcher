# Workspace Cleanup

This document describes what can be safely deleted after the documentation and
manual knowledge were consolidated into `ld_patcher`.

## Goal

The target state is:

- one maintained tool repository: `ld_patcher`
- separate tool repos kept only if still needed:
  - `ld_sniffer`
  - `ld_viewer`
- optional source archives/source trees kept only as inputs

## Read This Before Deleting Anything

This document is intentionally conservative.

The idea is:

- keep the files that are still part of the active `ld_patcher` workflow
- delete only the old helper/build folders that are now duplicated by the
  maintained docs and scripts inside `ld_patcher`

Beginner-safe rule:

- if you are unsure whether a folder is still needed, do not delete it yet
- first check whether the same information now exists inside:
  - [`../README.md`](../README.md)
  - [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)
  - [`MANUAL_PATCH_PACKAGE.md`](MANUAL_PATCH_PACKAGE.md)
  - [`../scripts/manual_reference/`](../scripts/manual_reference/)

## Knowledge Now Preserved Inside `ld_patcher`

`ld_patcher` now contains:

- current documentation
- manual workflow instructions
- CubeIDE hookup instructions
- current JSON contract docs
- current payload packages for hand-porting
- preserved manual helper scripts
- reference JSON samples

That means the old helper/build folders no longer need to remain in the
workspace just to preserve documentation.

## Where The Old Numbered Build-Folder Knowledge Lives Now

If you used the old numbered build folders as ad-hoc documentation, the useful
parts are now distributed into the maintained docs like this:

- manual patching, manual build, helper scripts, longpath notes, and verify flow:
  - [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)
- exact payload contents and insertion mapping:
  - [`MANUAL_PATCH_PACKAGE.md`](MANUAL_PATCH_PACKAGE.md)
- CubeIDE hookup and package sanity:
  - [`CUBEIDE_INTEGRATION.md`](CUBEIDE_INTEGRATION.md)
- required software and optional longpath-helper dependencies:
  - [`PREREQUISITES.md`](PREREQUISITES.md)
- sample JSON/reference artifacts:
  - [`reference_samples/`](reference_samples/)

Practical conclusion:

- the useful human guidance that used to be scattered across numbered build
  folders now lives in the maintained `ld_patcher` docs and helper scripts
- what mostly remains outside `ld_patcher` is generated output, temporary test
  artifacts, and old workspace-specific layouts

## Usually Safe To Delete

After you confirm you no longer need them as live working folders, these are the
first candidates to remove:

- `build_00`
- `build_01`
- `build_02`
- `build_03`
- `build_04`
- `json_patch`
- root helper files:
  - `_run_canonical_build.sh`
  - `_run_fallback_rebuild.sh`
  - `diagnostics.txt`

## Keep These

Keep:

- `ld_patcher`
- `ld_sniffer`
- `ld_viewer`

Keep source inputs only if you still want to use them:

- `gnu-tools-for-stm32-13.3.rel1.zip`
- `gnu-tools-for-stm32-14.3.rel1.zip`
- extracted source trees you still actively patch/build against

## Important Note About `AGENTS.md`

The workspace-root `AGENTS.md` is a live Codex instruction file.

Its knowledge is now reflected in the consolidated documentation, but deleting
the root `AGENTS.md` changes agent behavior in this workspace.

So:

- if your goal is only to preserve information, it is safe conceptually
- if your goal is also to preserve current local Codex behavior, keep the root
  `AGENTS.md`

## Recommended Cleanup Order

1. keep `ld_patcher`
2. keep `ld_sniffer`
3. keep `ld_viewer`
4. keep only source ZIPs/trees you still actively use
5. remove old numbered build folders
6. remove old top-level `json_patch`
7. remove old root helper files if they are no longer needed outside
   `ld_patcher`

## Final Safety Check

Before you delete the old workspace folders, make sure all of the following are
true:

- the docs you actually use now are inside `ld_patcher/docs/`
- the helper scripts you still need are inside [`../scripts/manual_reference/`](../scripts/manual_reference/)
- the payloads you still need are inside [`../payloads/`](../payloads/)
- you no longer depend on the old top-level `json_patch` folder as a separate
  source of truth

If all four are true, cleanup is usually safe.
