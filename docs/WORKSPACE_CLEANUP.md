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
5. remove old `build_00..04`
6. remove old top-level `json_patch`
7. remove old root helper files if they are no longer needed outside
   `ld_patcher`

