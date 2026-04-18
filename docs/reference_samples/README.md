# Reference Samples

This directory contains small reference artifacts that are still useful after
the old workspace helper/build folders are removed.

Current contents:

- canonical sample JSON shape
- real smoke-test JSON output samples
- optional `jq` validator for the canonical JSON contract

These files are documentation/reference material, not runtime dependencies of
`ld_patcher`.

Useful files:

- `sample_universal.json`
  - compact shape example
- `sample_h7s_fiber_test_ldscript.json`
  - curated real linker JSON excerpt
- `h7s_fiber_test_Boot.json`
  - larger real-world sample
- `validate_core.jq`
  - optional `jq` validator for the canonical contract
