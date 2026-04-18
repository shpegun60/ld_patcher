# Manual Patch Package

This document explains the current manual patch package model.

## Source Of Truth

The current source of truth for manual patching is no longer an external
top-level `json_patch/` folder.

The current patch packages live inside `ld_patcher` itself:

- [`../payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900/`](../payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900/)
- [`../payloads/json_patch_v10_st_ld_14_3_rel1/`](../payloads/json_patch_v10_st_ld_14_3_rel1/)

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
- `hooks/lexsup_options.fragment`
- `hooks/lexsup_switch.fragment`
- `hooks/ldlang_include.fragment`
- `hooks/ldlang_runtime.fragment`
- `hooks/pex-win32.fragment`
- `hooks/Makefile.am.fragment`

These are copy-paste helpers for a human doing the port by hand.

They are not compiled by themselves.

Important:

- both payload directories still contain older combined helper files such as `hooks/lexsup.c.fragment` and `hooks/ldlang.c.fragment`
- those combined helpers are legacy reference material only
- the active application recipes use the split fragment family listed above
- if you patch by hand and want parity with the current GUI/CLI implementation, use the split fragment family

## Exact Manual Rule

Use one payload package as the single source of truth for the whole manual
patching session.

Do not mix:

- files from one payload package
- with hook fragments from another payload package

## Manual Porting Rule

If you are patching by hand:

1. copy the three real implementation files into `src/binutils/ld/`
2. apply the eight active hook fragments to the matching source files
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

- [`MANUAL_WORKFLOW.md`](MANUAL_WORKFLOW.md)

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

- ST `13.3.rel1.20250523-0900` -> [`../payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900/`](../payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900/)
- ST `14.3.rel1` -> [`../payloads/json_patch_v10_st_ld_14_3_rel1/`](../payloads/json_patch_v10_st_ld_14_3_rel1/)

Practical source-tree mapping:

- if your working tree name begins with `gnu-tools-for-stm32-13.3.rel1`
  - use [`../payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900/`](../payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900/)
- if your working tree name begins with `gnu-tools-for-stm32-14.3.rel1`
  - use [`../payloads/json_patch_v10_st_ld_14_3_rel1/`](../payloads/json_patch_v10_st_ld_14_3_rel1/)

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
- `<payload-root>\hooks\lexsup_options.fragment`
  - `-> <source-root>\src\binutils\ld\lexsup.c`
- `<payload-root>\hooks\lexsup_switch.fragment`
  - `-> <source-root>\src\binutils\ld\lexsup.c`
- `<payload-root>\hooks\ldlang_include.fragment`
  - `-> <source-root>\src\binutils\ld\ldlang.c`
- `<payload-root>\hooks\ldlang_runtime.fragment`
  - `-> <source-root>\src\binutils\ld\ldlang.c`
- `<payload-root>\hooks\pex-win32.fragment`
  - `-> <source-root>\src\binutils\libiberty\pex-win32.c`
- `<payload-root>\hooks\Makefile.am.fragment`
  - `-> <source-root>\src\binutils\ld\Makefile.am`

## Detailed Insertion Cookbook

This section is the manual equivalent of the patch recipe `operations[]` list.

If you want to reproduce exactly what the program does, use this mapping and do
not improvise different insertion points.

### `ld.h.fragment`

- fragment:
  - `<payload-root>\hooks\ld.h.fragment`
- target:
  - `<source-root>\src\binutils\ld\ld.h`
- insert after:
  - `char *default_script;`
- result shape:

```c
/* Default linker script.  */
char *default_script;

#define X(ENUM_ID, LONGOPT, HAS_ARG, METAVAR, HELP_TEXT, ARGS_FIELD) \
  char *ARGS_FIELD;
#include "ldjson_options.def"
#undef X
```

### `ldlex.h.fragment`

- fragment:
  - `<payload-root>\hooks\ldlex.h.fragment`
- target:
  - `<source-root>\src\binutils\ld\ldlex.h`
- insert after:
  - `OPTION_DEFAULT_SCRIPT,`
- result shape:

```c
OPTION_DEFAULT_SCRIPT,

#define X(ENUM_ID, LONGOPT, HAS_ARG, METAVAR, HELP_TEXT, ARGS_FIELD) \
  OPTION_##ENUM_ID,
#include "ldjson_options.def"
#undef X
```

### `lexsup_options.fragment`

- fragment:
  - `<payload-root>\hooks\lexsup_options.fragment`
- target:
  - `<source-root>\src\binutils\ld\lexsup.c`
- insert after:
  - the `ld_options[]` row for `OPTION_DEFAULT_SCRIPT`
- result shape:

```c
{ {"dT", required_argument, NULL, OPTION_DEFAULT_SCRIPT},
  '\0', NULL, NULL, ONE_DASH },

#define X(ENUM_ID, LONGOPT, HAS_ARG, METAVAR, HELP_TEXT, ARGS_FIELD) \
  { {LONGOPT, HAS_ARG, NULL, OPTION_##ENUM_ID}, '\0', \
    N_(METAVAR), N_(HELP_TEXT), TWO_DASHES },
#include "ldjson_options.def"
#undef X
```

Current effective row generated from `ldjson_options.def`:

```c
{ {"dump-script-json", required_argument, NULL, OPTION_DUMP_SCRIPT_JSON},
  '\0', N_("FILE"), N_("Write linker script data as indexed JSON"), TWO_DASHES },
```

### `lexsup_switch.fragment`

- fragment:
  - `<payload-root>\hooks\lexsup_switch.fragment`
- target:
  - `<source-root>\src\binutils\ld\lexsup.c`
- insert after:
  - the complete `case OPTION_DEFAULT_SCRIPT:` block in `parse_args()`
- result shape:

```c
case OPTION_DEFAULT_SCRIPT:
  command_line.default_script = optarg;
  break;

#define X(ENUM_ID, LONGOPT, HAS_ARG, METAVAR, HELP_TEXT, ARGS_FIELD) \
  case OPTION_##ENUM_ID: \
    command_line.ARGS_FIELD = optarg; \
    break;
#include "ldjson_options.def"
#undef X
```

Current effective case generated from `ldjson_options.def`:

```c
case OPTION_DUMP_SCRIPT_JSON:
  command_line.dump_script_json = optarg;
  break;
```

### `ldlang_include.fragment`

- fragment:
  - `<payload-root>\hooks\ldlang_include.fragment`
- target:
  - `<source-root>\src\binutils\ld\ldlang.c`
- insert after:
  - the function that ends with:
    - `print_statement_list (statement_list.head, abs_output_section);`
    - followed by `}`
- result shape:

```c
print_statement_list (statement_list.head, abs_output_section);
}

#include "ldscript_json_impl.inc"
```

### `ldlang_runtime.fragment`

- fragment:
  - `<payload-root>\hooks\ldlang_runtime.fragment`
- target:
  - `<source-root>\src\binutils\ld\ldlang.c`
- insert after:
  - the tail of `lang_process()`:
    - `ldlang_check_require_defined_symbols ();`
    - `lang_end ();`
- result shape:

```c
ldlang_check_require_defined_symbols ();
lang_end ();

lang_dump_script_json ();
```

### `pex-win32.fragment`

- fragment:
  - `<payload-root>\hooks\pex-win32.fragment`
- target:
  - `<source-root>\src\binutils\libiberty\pex-win32.c`
- insert after:
  - `#include <stmicroelectronics/longpath.h>`
- result shape:

```c
#include <stmicroelectronics/longpath.h>

/* Local fallback for environments where ST longpath runtime wrappers are not
   generated. */
#ifndef STM32_LONGPATH_RUNTIME
static wchar_t *
stm32_local_utf8_to_wchar (const char *src)
{
  ...
}

static wchar_t *
stm32_local_handle_long_path_utf8 (const char *src)
{
  return stm32_local_utf8_to_wchar (src);
}

#define utf8_to_wchar stm32_local_utf8_to_wchar
#define handle_long_path_utf8 stm32_local_handle_long_path_utf8
#endif
```

Manual verification marker to look for:

```c
#define handle_long_path_utf8 stm32_local_handle_long_path_utf8
```

### `Makefile.am.fragment`

- fragment:
  - `<payload-root>\hooks\Makefile.am.fragment`
- target:
  - `<source-root>\src\binutils\ld\Makefile.am`
- apply rule:
  - append once near the existing `HFILES` / `EXTRA_DIST` definitions
- result shape:

```make
HFILES += ldjson_options.def ldjson_compat.h ldscript_json_impl.inc
EXTRA_DIST += ldjson_options.def ldjson_compat.h ldscript_json_impl.inc
```

## What The Patched Source Tree Should Contain

After a successful manual patch, the working tree should contain:

- copied files:
  - `src/binutils/ld/ldjson_options.def`
  - `src/binutils/ld/ldjson_compat.h`
  - `src/binutils/ld/ldscript_json_impl.inc`
- inserted markers:
  - `#include "ldjson_options.def"` in `ld.h`
  - `OPTION_##ENUM_ID` in `ldlex.h`
  - `#include "ldjson_options.def"` in `lexsup.c` twice:
    - once for the option rows
    - once for the switch cases
  - `#include "ldscript_json_impl.inc"` in `ldlang.c`
  - `lang_dump_script_json ();` in `ldlang.c`
  - `#define handle_long_path_utf8 stm32_local_handle_long_path_utf8` in `pex-win32.c`
  - `HFILES += ...` and `EXTRA_DIST += ...` in `Makefile.am`

## JSON Contract Reference

The patch package exists to produce the canonical linker JSON contract.

The current reference document is:

- [`LINKER_JSON_CONTRACT.md`](LINKER_JSON_CONTRACT.md)

Reference sample outputs live under:

- [`reference_samples/`](reference_samples/)

## Relationship To Manual Scripts

Current manual helper scripts live under:

- [`../scripts/manual_reference/`](../scripts/manual_reference/)

The intended pairings are:

- copy payload files:
  - [`../scripts/manual_reference/copy_patch_payload.ps1`](../scripts/manual_reference/copy_patch_payload.ps1)
- check payload + glue:
  - [`../scripts/manual_reference/check_manual_patch.ps1`](../scripts/manual_reference/check_manual_patch.ps1)
- build manually:
  - [`../scripts/manual_reference/build_st_ld_manual.sh`](../scripts/manual_reference/build_st_ld_manual.sh)
- verify manually:
  - [`../scripts/manual_reference/verify_drop_self_contained.ps1`](../scripts/manual_reference/verify_drop_self_contained.ps1)
- package manually:
  - [`../scripts/manual_reference/package_cubeide_drop.ps1`](../scripts/manual_reference/package_cubeide_drop.ps1)

This gives you a fully manual path that still uses the same payloads and verify
assets as the application workflow.

## See Also

- [Manual Workflow](MANUAL_WORKFLOW.md)
- [CLI Reference](CLI_REFERENCE.md)
- [Adding Support For A New Compiler Version](ADDING_SUPPORT.md)

