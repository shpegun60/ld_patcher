# Manual Reference Scripts

These scripts preserve the useful non-GUI/manual workflows inside `ld_patcher`.

Current primary script:

- `build_st_ld_manual.sh`

Core helper scripts:

- `copy_patch_payload.ps1`
- `check_manual_patch.ps1`
- `verify_drop_self_contained.ps1`
- `package_cubeide_drop.ps1`

Reference variants:

- `build_st_ld_manual_canonical_longpath.sh`
- `build_st_ld_manual_fallback.sh`
- `smoke_h7s_fiber_test.ps1`

Guidance:

- use `copy_patch_payload.ps1` to copy the real implementation files from the
  same payload packages used by the app
- use `check_manual_patch.ps1` after you paste the hook fragments
- use `build_st_ld_manual.sh` as the main hand-build path
- use the two longpath variants only when you specifically need to test that
  area
- use `verify_drop_self_contained.ps1` as the main manual verify path
- use `package_cubeide_drop.ps1` to create the final CubeIDE package directory
- use `smoke_h7s_fiber_test.ps1` only if you intentionally want the older
  external-project smoke path
