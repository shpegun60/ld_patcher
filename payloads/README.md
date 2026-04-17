# payloads

Optional local home for patch payload packages used by `ld_patcher`.

Example layout:

- `payloads/<package_name>/<any files your recipe expects>`
- `payloads/<package_name>/hooks/...`

Patch recipes may point here through:

- `patch_package.root_hint`
- `patch_package.root_hints`

`ld_patcher` does not require a fixed payload file set in code.

The actual payload shape is defined by the patch recipe:

- `patch_package.required_files`
- `operations[*].source_path`
- any recipe-specific checks

This is useful when a profile wants its own self-contained payload package instead of reusing `../json_patch`.
