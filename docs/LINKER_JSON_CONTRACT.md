# Linker JSON Contract

This document is the current canonical contract for `--dump-script-json`.

## Who This Document Is For

Read this document if:

- you are writing a parser that consumes the JSON emitted by the patched linker
- you are checking whether a new linker build still follows the agreed format
- you are adding support for a new ST release and need to confirm that the
  output contract did not accidentally change

Do not start with this document if your goal is only:

- to patch a source tree
- to build the linker
- to connect the linker to STM32CubeIDE

For those tasks, start with:

- [Manual Workflow](MANUAL_WORKFLOW.md)
- [CLI Reference](CLI_REFERENCE.md)
- [CubeIDE Integration](CUBEIDE_INTEGRATION.md)

## How To Use This Contract

Treat this file as the normative shape description for the JSON produced by the
patched linker.

Practical rule:

- if an example JSON file and this contract ever disagree, treat this contract
  as the authority and update the examples or the code accordingly

Reference samples live in:

- [`reference_samples/`](reference_samples/)

## Top-Level Keys

The canonical top-level keys are:

- `format`
- `output`
- `memory_regions`
- `output_sections`
- `input_sections`
- `discarded_input_sections`
- `symbols`

`memory_regions`, `output_sections`, `input_sections`, and `symbols` are indexed
objects with:

- `items`
- `by_name`
- `null_name_ids`

`by_name` maps:

- `name -> [id...]`

`discarded_input_sections` is a simple top-level list of discard records.

## `output`

`output` is the canonical top-level object for linker-wide output metadata.

Minimal agreed payload:

- `entry_symbol`

`entry_symbol` may be `null`.

Do not add as part of the canonical contract:

- `filename`
- `target`
- `entry_from_cmdline`
- `is_relocatable`
- `is_shared`
- `is_pie`

## `memory_regions`

Minimal payload for each `memory_regions.items[*]` entry:

- `id`
- `name`
- `origin_hex`
- `length_hex`
- `attrs`

`attrs` is an object, not a plain string.

Minimal `attrs` payload:

- `required`
- `forbidden`
- `flags_hex`
- `not_flags_hex`

Do not collapse this into `attrs: "xrw"`.

Do not add as part of the canonical contract:

- `origin_exp`
- `length_exp`
- `current`
- `last_os`
- `had_full_message`

## `output_sections`

Minimal payload for each `output_sections.items[*]` entry:

- `id`
- `name`
- `vma_region`
- `lma_region`
- `vma_hex`
- `lma_hex`
- `size_hex`
- `script_subsections`

Optional:

- `flags.letters`
- `flags.hex`

`vma_region` and `lma_region` may be `null`.

`vma_hex`, `lma_hex`, and `size_hex` may be `null`.

Do not export raw internal BFD structures such as:

- `bfd_section`

Only export stable derived values.

## `input_sections`

Minimal payload for each `input_sections.items[*]` entry:

- `id`
- `name`
- `discarded`
- `output_section_id`
- `owner_file`
- `value_hex`
- `size_hex`
- `output_offset_hex`
- `flags`

`flags` must be an object with:

- `letters`
- `hex`

`output_section_id`, `owner_file`, `value_hex`, `size_hex`, and
`output_offset_hex` may be `null`.

Do not add as part of the canonical contract:

- `object_file`
- `archive_file`
- `archive_member`
- `input_statement_file`
- `rawsize_hex`

## `discarded_input_sections`

Each `discarded_input_sections[*]` record contains:

- `input_section_id`
- `discard_reason`

Contract rule:

- every `input_section_id` must reference an existing input section
- discarded records and `input_sections.items[*].discarded` must stay consistent

## `symbols`

Minimal payload for each `symbols.items[*]` entry:

- `id`
- `name`
- `state`
- `value_hex`
- `size_hex`
- `section`
- `output_section_id`
- `input_section_id`
- `script_defined`

Preferred stable `state` values:

- `defined`
- `undefined`
- `common`
- `alias`
- `warning`

`state` intentionally does not encode weak-vs-strong detail.

Interpretation:

- `defined` covers both strong-defined and weak-defined symbols
- `undefined` covers both strong-undefined and weak-undefined symbols

If weak detail is ever needed later, it should be added as a separate optional
field rather than overloading `state`.

Do not add as part of the canonical contract:

- raw `hash_type`
- `object_file`
- `archive_file`
- `archive_member`

## Versioning

Current reference format:

- `format.name = "ldscript-json"`
- `format.major = 10`
- `format.minor = 0`

Parsers should:

- reject unknown `format.major`
- tolerate larger `format.minor`

## Practical Notes

- hex values are canonical lowercase `0x...` strings
- `output.entry_symbol` may be a symbol-like name or a numeric string
- script-defined absolute values may legitimately appear as `section = "ABS"`
  with `output_section_id = null`
- non-discarded debug/comment-style input sections may still have
  `output_section_id` while keeping `value_hex` and `output_offset_hex` as
  `null`
- a defined symbol with non-`ABS` `section` and `output_section_id = null` is
  not automatically an error; check `input_section_id` and discarded-state
  context first

## Reference Samples

Reference samples live under:

- [`reference_samples/`](reference_samples/)

## See Also

- [Manual Workflow](MANUAL_WORKFLOW.md)
- [CLI Reference](CLI_REFERENCE.md)
- [CubeIDE Integration](CUBEIDE_INTEGRATION.md)
