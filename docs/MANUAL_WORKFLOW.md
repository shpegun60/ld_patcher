# Manual Workflow

This document is the canonical no-GUI workflow for `ld_patcher`.

It replaces the old scattered `build_00..04` notes with one current path that
matches the maintained application workflow.

## Two Supported Non-GUI Modes

There are two valid ways to work without the GUI:

1. use `ld_patcher.exe` CLI
2. patch/build by hand using the payload packages and helper scripts in this repository

Recommended rule:

- prefer the CLI when possible
- use the hand-port/manual-build path when you explicitly want to inspect or control each step yourself

Important:

- both paths use the same patch payload files that the GUI/CLI workflow uses
- the payload source of truth is inside `payloads/`
- the self-contained verify source of truth is inside `verify_assets/`
- the real helper script files live in `scripts/manual_reference/`
- this document embeds the current helper scripts inline at the exact step where they are used

## A. Preferred No-GUI Flow: `ld_patcher.exe` CLI

### ZIP Input

1. Validate the source archive:

```text
ld_patcher.exe --validate <source.zip>
```

2. Extract it:

```text
ld_patcher.exe --extract <source.zip> <parent-dir> <directory-name>
```

3. Apply the patch:

```text
ld_patcher.exe --apply <profile-id> <working-root>
```

4. Build:

```text
ld_patcher.exe --build <profile-id> <working-root>
```

Optional explicit form when you want to force a specific build recipe or build root:

```text
ld_patcher.exe --build <profile-id> <working-root> [build-recipe-id] [build-root-override]
```

5. Verify:

```text
ld_patcher.exe --verify <profile-id> <drop-dir> [cubeide-or-compiler-root]
```

6. Package for CubeIDE:

```text
ld_patcher.exe --package <source-drop-dir> <package-dir>
```

### Directory Input

For a directory input, skip extract:

```text
ld_patcher.exe --validate <source-dir>
ld_patcher.exe --apply <profile-id> <source-dir>
ld_patcher.exe --build <profile-id> <source-dir>
ld_patcher.exe --verify <profile-id> <drop-dir> [cubeide-or-compiler-root]
ld_patcher.exe --package <source-drop-dir> <package-dir>
```

## B. Hand-Port / Hand-Build Flow

Use this path when you want to do the work yourself without relying on the GUI
workflow and without relying on `ld_patcher.exe` for execution.

The only things you still need externally are:

- an ST source ZIP or extracted source tree
- MSYS2/MinGW64 for the host build
- STM32CubeIDE or another `arm-none-eabi-g++` toolchain for verify

### Step -1: If you start from a ZIP, unpack it first

The fully manual path works on a writable source directory, not directly on the ZIP.

If you start from `gnu-tools-for-stm32-13.3.rel1.zip` or
`gnu-tools-for-stm32-14.3.rel1.zip`, unpack it first and then use the extracted
directory as the working tree for the remaining steps.

Example with PowerShell:

```powershell
Expand-Archive -LiteralPath .\gnu-tools-for-stm32-13.3.rel1.zip `
  -DestinationPath .\manual_extract `
  -Force
```

After extraction, the working tree is typically something like:

- `.\manual_extract\gnu-tools-for-stm32-13.3.rel1`
- `.\manual_extract\gnu-tools-for-stm32-14.3.rel1`

### Step 0: Pick the right payload

Use the payload that matches the source tree you are patching:

- `gnu-tools-for-stm32-13.3.rel1*`
  - `payloads/json_patch_v10_st_ld_13_3_rel1_20250523_0900`
- `gnu-tools-for-stm32-14.3.rel1*`
  - `payloads/json_patch_v10_st_ld_14_3_rel1`

These are the exact same payloads that the current `ld_patcher` recipes use.

### Step 1: Copy the real implementation files

Copy these three files into the target source tree:

- `ldjson_options.def`
- `ldjson_compat.h`
- `ldscript_json_impl.inc`

Destination:

- `<working-tree>\src\binutils\ld\`

Recommended script-assisted copy:

```powershell
powershell -ExecutionPolicy Bypass -File .\ld_patcher\scripts\manual_reference\copy_patch_payload.ps1 `
  -SourceRoot "C:\path\to\gnu-tools-for-stm32-13.3.rel1"
```

or explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File .\ld_patcher\scripts\manual_reference\copy_patch_payload.ps1 `
  -SourceRoot "C:\path\to\gnu-tools-for-stm32-14.3.rel1" `
  -PayloadName "json_patch_v10_st_ld_14_3_rel1"
```

Current helper script file:

- `scripts/manual_reference/copy_patch_payload.ps1`

Inline copy of the current script:

```powershell
$ErrorActionPreference = 'Stop'

param(
  [Parameter(Mandatory = $true)]
  [string] $SourceRoot,

  [string] $PayloadName
)

function Resolve-PatcherRoot {
  return (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}

function Resolve-PayloadName {
  param(
    [string] $Requested,
    [string] $SourceRootPath
  )

  if (-not [string]::IsNullOrWhiteSpace($Requested)) {
    return $Requested
  }

  $leaf = Split-Path -Leaf $SourceRootPath
  if ($leaf -like 'gnu-tools-for-stm32-14.3.rel1*') {
    return 'json_patch_v10_st_ld_14_3_rel1'
  }

  if ($leaf -like 'gnu-tools-for-stm32-13.3.rel1*') {
    return 'json_patch_v10_st_ld_13_3_rel1_20250523_0900'
  }

  throw "Could not infer payload from source root '$leaf'. Pass -PayloadName explicitly."
}

$patcherRoot = Resolve-PatcherRoot
$payload = Resolve-PayloadName -Requested $PayloadName -SourceRootPath $SourceRoot
$payloadRoot = Join-Path $patcherRoot "payloads\$payload"
$targetLdRoot = Join-Path $SourceRoot 'src\binutils\ld'

foreach ($path in @($payloadRoot, $targetLdRoot)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Required path is missing: $path"
  }
}

$realFiles = @(
  'ldjson_options.def',
  'ldjson_compat.h',
  'ldscript_json_impl.inc'
)

foreach ($name in $realFiles) {
  $src = Join-Path $payloadRoot $name
  $dst = Join-Path $targetLdRoot $name
  if (-not (Test-Path -LiteralPath $src)) {
    throw "Payload file is missing: $src"
  }
  Copy-Item -LiteralPath $src -Destination $dst -Force
}

[PSCustomObject]@{
  SourceRoot = (Get-Item -LiteralPath $SourceRoot).FullName
  Payload = $payload
  PayloadRoot = (Get-Item -LiteralPath $payloadRoot).FullName
  TargetLdRoot = (Get-Item -LiteralPath $targetLdRoot).FullName
  CopiedFiles = $realFiles
  NextStep = 'Apply hooks/*.fragment by hand, then run check_manual_patch.ps1'
}
```

### Step 2: Apply the hook fragments

Use these exact fragment files from the selected payload package:

- `<payload-root>\hooks\ld.h.fragment`
- `<payload-root>\hooks\ldlex.h.fragment`
- `<payload-root>\hooks\lexsup.c.fragment`
- `<payload-root>\hooks\ldlang.c.fragment`
- `<payload-root>\hooks\Makefile.am.fragment`

Paste them into:

- `<source-root>\src\binutils\ld\ld.h`
- `<source-root>\src\binutils\ld\ldlex.h`
- `<source-root>\src\binutils\ld\lexsup.c`
- `<source-root>\src\binutils\ld\ldlang.c`
- `<source-root>\src\binutils\ld\Makefile.am`

This remains a human copy-paste step by design.

Current insertion model:

- `ld.h`
  - add the X-macro field expansion after `char *default_script;`
- `ldlex.h`
  - add the X-macro enum expansion after `OPTION_DEFAULT_SCRIPT,`
- `lexsup.c`
  - add the generated option rows after the existing `default-script` / `dT` option rows
  - add the generated `case OPTION_...` switch block after `case OPTION_DEFAULT_SCRIPT:`
- `ldlang.c`
  - include `ldscript_json_impl.inc` at file scope
  - add `lang_dump_script_json ();` immediately after `lang_end ();`
- `Makefile.am`
  - add the new files to `HFILES`
  - add the new files to `EXTRA_DIST`

For the payload-to-target mapping, also see:

- `docs/MANUAL_PATCH_PACKAGE.md`

### Step 3: Validate the patched tree

Preferred no-GUI helper:

```powershell
powershell -ExecutionPolicy Bypass -File .\ld_patcher\scripts\manual_reference\check_manual_patch.ps1 `
  -SourceRoot "C:\path\to\gnu-tools-for-stm32-13.3.rel1"
```

That script checks:

- the copied implementation files are present
- the copied implementation files still match the selected payload package
- the expected glue blocks exist in:
  - `ld.h`
  - `ldlex.h`
  - `lexsup.c`
  - `ldlang.c`
  - `Makefile.am`

Current helper script file:

- `scripts/manual_reference/check_manual_patch.ps1`

Inline copy of the current script:

```powershell
$ErrorActionPreference = 'Stop'

param(
  [Parameter(Mandatory = $true)]
  [string] $SourceRoot,

  [string] $PayloadName
)

function Resolve-PatcherRoot {
  return (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}

function Resolve-PayloadName {
  param(
    [string] $Requested,
    [string] $SourceRootPath
  )

  if (-not [string]::IsNullOrWhiteSpace($Requested)) {
    return $Requested
  }

  $leaf = Split-Path -Leaf $SourceRootPath
  if ($leaf -like 'gnu-tools-for-stm32-14.3.rel1*') {
    return 'json_patch_v10_st_ld_14_3_rel1'
  }

  if ($leaf -like 'gnu-tools-for-stm32-13.3.rel1*') {
    return 'json_patch_v10_st_ld_13_3_rel1_20250523_0900'
  }

  throw "Could not infer payload from source root '$leaf'. Pass -PayloadName explicitly."
}

function Add-Result {
  param([bool] $Ok, [string] $Check, [string] $Detail)
  $script:Checks.Add([PSCustomObject]@{ Ok = $Ok; Check = $Check; Detail = $Detail })
  if (-not $Ok) {
    $script:Failures.Add("${Check}: $Detail")
  }
}

function Get-FileText {
  param([string] $Path)
  if (-not $script:ContentCache.ContainsKey($Path)) {
    $script:ContentCache[$Path] = Get-Content -LiteralPath $Path -Raw
  }
  return $script:ContentCache[$Path]
}

function Assert-PathExists {
  param([string] $Path, [string] $Check)
  if (Test-Path -LiteralPath $Path) {
    Add-Result $true $Check $Path
  } else {
    Add-Result $false $Check "Missing file: $Path"
  }
}

function Assert-SameFile {
  param([string] $Expected, [string] $Actual, [string] $Check)
  if (-not (Test-Path -LiteralPath $Expected) -or -not (Test-Path -LiteralPath $Actual)) {
    Add-Result $false $Check 'Compared file is missing'
    return
  }

  $expectedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Expected).Hash
  $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Actual).Hash
  if ($expectedHash -eq $actualHash) {
    Add-Result $true $Check $actualHash
  } else {
    Add-Result $false $Check "Hash mismatch: expected $expectedHash actual $actualHash"
  }
}

function Assert-Regex {
  param([string] $Path, [string] $Pattern, [string] $Check)
  if (-not (Test-Path -LiteralPath $Path)) {
    Add-Result $false $Check "Missing file: $Path"
    return
  }

  $matched = [regex]::IsMatch((Get-FileText $Path), $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
  if ($matched) {
    Add-Result $true $Check 'ok'
  } else {
    Add-Result $false $Check "Expected snippet not found in $Path"
  }
}

$patcherRoot = Resolve-PatcherRoot
$payload = Resolve-PayloadName -Requested $PayloadName -SourceRootPath $SourceRoot
$payloadRoot = Join-Path $patcherRoot "payloads\$payload"
$srcLd = Join-Path $SourceRoot 'src\binutils\ld'

$Checks = [System.Collections.Generic.List[object]]::new()
$Failures = [System.Collections.Generic.List[string]]::new()
$ContentCache = @{}

$realFiles = @('ldjson_options.def', 'ldjson_compat.h', 'ldscript_json_impl.inc')
foreach ($file in $realFiles) {
  Assert-PathExists (Join-Path $srcLd $file) "Real file present: $file"
  Assert-SameFile (Join-Path $payloadRoot $file) (Join-Path $srcLd $file) "Real file matches payload: $file"
}

$ldH = Join-Path $srcLd 'ld.h'
$ldlexH = Join-Path $srcLd 'ldlex.h'
$lexsupC = Join-Path $srcLd 'lexsup.c'
$ldlangC = Join-Path $srcLd 'ldlang.c'
$makefileAm = Join-Path $srcLd 'Makefile.am'

Assert-Regex $ldH 'char \*default_script;\r?\n\r?\n#define X\(ENUM_ID, LONGOPT, HAS_ARG, METAVAR, HELP_TEXT, ARGS_FIELD\)' 'ld.h X-macro glue block'
Assert-Regex $ldlexH 'OPTION_DEFAULT_SCRIPT,\r?\n#define X\(ENUM_ID, LONGOPT, HAS_ARG, METAVAR, HELP_TEXT, ARGS_FIELD\)' 'ldlex.h X-macro glue block'
Assert-Regex $lexsupC '#include "ldjson_options\.def"' 'lexsup.c includes ldjson_options.def'
Assert-Regex $lexsupC 'command_line\.ARGS_FIELD = optarg;' 'lexsup.c parse_args glue block'
Assert-Regex $ldlangC '#include "ldscript_json_impl\.inc"' 'ldlang.c includes implementation'
Assert-Regex $ldlangC 'lang_dump_script_json \(\);' 'ldlang.c runtime hook'
Assert-Regex $makefileAm 'HFILES \+= ldjson_options\.def ldjson_compat\.h ldscript_json_impl\.inc' 'Makefile.am HFILES glue'
Assert-Regex $makefileAm 'EXTRA_DIST \+= ldjson_options\.def ldjson_compat\.h ldscript_json_impl\.inc' 'Makefile.am EXTRA_DIST glue'

$Checks | Format-Table -AutoSize

if ($Failures.Count -gt 0) {
  Write-Host ''
  Write-Host 'Manual patch check failed.' -ForegroundColor Red
  foreach ($failure in $Failures) {
    Write-Host "- $failure" -ForegroundColor Red
  }
  exit 1
}

Write-Host ''
[PSCustomObject]@{
  ok = $true
  message = 'payload files and manual glue look consistent'
  payload = $payload
  source = $srcLd
  checked = $Checks.Count
}
```

Optional backend validation with the application backend:

```text
ld_patcher.exe --validate <working-tree>
```

### Step 4: Build manually

Current primary manual build script:

- `scripts/manual_reference/build_st_ld_manual.sh`

Useful variables:

- `LDPATCHER_WORKSPACE_ROOT`
- `LDPATCHER_SOURCE_ROOT`
- `LDPATCHER_OUT_DIR`
- `LDPATCHER_BUILD_DIR`
- `LDPATCHER_INSTALL_DIR`
- `LDPATCHER_DROP_DIR`
- `LDPATCHER_DISPLAY_VERSION`

Minimal example for `13.3` from MSYS2 MINGW64:

```bash
export LDPATCHER_WORKSPACE_ROOT=/c/Users/admin/Documents/my_workspace/gnu/gnu-tools-for-stm32
export LDPATCHER_SOURCE_ROOT=$LDPATCHER_WORKSPACE_ROOT/gnu-tools-for-stm32-13.3.rel1
export LDPATCHER_OUT_DIR=$LDPATCHER_SOURCE_ROOT/build
export LDPATCHER_BUILD_DIR=$LDPATCHER_OUT_DIR/_build-ld-st-manual
export LDPATCHER_INSTALL_DIR=$LDPATCHER_OUT_DIR/_install-ld-st-manual
export LDPATCHER_DROP_DIR=$LDPATCHER_OUT_DIR/_drop-ld-st-manual
export LDPATCHER_DISPLAY_VERSION=13.3.rel1
/usr/bin/bash /c/Users/admin/Documents/my_workspace/gnu/gnu-tools-for-stm32/ld_patcher/scripts/manual_reference/build_st_ld_manual.sh
```

Minimal example for `14.3` from MSYS2 MINGW64:

```bash
export LDPATCHER_WORKSPACE_ROOT=/c/Users/admin/Documents/my_workspace/gnu/gnu-tools-for-stm32
export LDPATCHER_SOURCE_ROOT=$LDPATCHER_WORKSPACE_ROOT/gnu-tools-for-stm32-14.3.rel1
export LDPATCHER_OUT_DIR=$LDPATCHER_SOURCE_ROOT/build
export LDPATCHER_BUILD_DIR=$LDPATCHER_OUT_DIR/_build-ld-st-manual
export LDPATCHER_INSTALL_DIR=$LDPATCHER_OUT_DIR/_install-ld-st-manual
export LDPATCHER_DROP_DIR=$LDPATCHER_OUT_DIR/_drop-ld-st-manual
export LDPATCHER_DISPLAY_VERSION=14.3.rel1
/usr/bin/bash /c/Users/admin/Documents/my_workspace/gnu/gnu-tools-for-stm32/ld_patcher/scripts/manual_reference/build_st_ld_manual.sh
```

Inline copy of the current script:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT=${LDPATCHER_WORKSPACE_ROOT:-/c/Users/admin/Documents/my_workspace/gnu/gnu-tools-for-stm32}
SRC=${LDPATCHER_SOURCE_ROOT:-$ROOT/gnu-tools-for-stm32-13.3.rel1.20250523-0900}
OUT=${LDPATCHER_OUT_DIR:-$ROOT/manual_build}
BLD=${LDPATCHER_BUILD_DIR:-$OUT/_build-ld-st-manual}
INS=${LDPATCHER_INSTALL_DIR:-$OUT/_install-ld-st-manual}
DROP=${LDPATCHER_DROP_DIR:-$OUT/_cubeide-arm-linker-st-manual-jsonpatch}
PKGVER=${LDPATCHER_DISPLAY_VERSION:-manual}
JOBS=${JOBS:-$(nproc)}

export PATH=/mingw64/bin:/usr/bin

echo "LDPATCHER_PROGRESS 5 Preparing build directories"
rm -rf "$BLD" "$INS" "$DROP"
mkdir -p "$BLD" "$INS" "$DROP"

cd "$BLD"
export CFLAGS="-I$SRC/src/liblongpath-win32/include"
export CPPFLAGS="-I$SRC/src/liblongpath-win32/include"

echo "LDPATCHER_PROGRESS 15 Configuring binutils tree"
"$SRC/src/binutils/configure" \
  --prefix="$INS" \
  --build=x86_64-w64-mingw32 \
  --host=x86_64-w64-mingw32 \
  --target=arm-none-eabi \
  --disable-gdb \
  --disable-sim \
  --disable-nls \
  --enable-plugins \
  --with-sysroot="$INS/arm-none-eabi" \
  --with-pkgversion="GNU Tools for STM32 $PKGVER" \
  --disable-werror

echo "LDPATCHER_PROGRESS 45 Building ld"
make -j"$JOBS" MAKEINFO=true all-ld
echo "LDPATCHER_PROGRESS 75 Installing ld"
make MAKEINFO=true install-ld

echo "LDPATCHER_PROGRESS 90 Collecting runtime artifacts"
cp "$INS/bin/arm-none-eabi-ld.exe" "$DROP/ld.exe"
cp "$INS/bin/arm-none-eabi-ld.bfd.exe" "$DROP/ld.bfd.exe"
cp "$INS/bin/arm-none-eabi-ld.exe" "$DROP/arm-none-eabi-ld.exe"
cp "$INS/bin/arm-none-eabi-ld.bfd.exe" "$DROP/arm-none-eabi-ld.bfd.exe"
cp /mingw64/bin/libwinpthread-1.dll "$DROP/libwinpthread-1.dll"
cp /mingw64/bin/libzstd.dll "$DROP/libzstd.dll"

echo "LDPATCHER_PROGRESS 98 Checking ld.exe --help"
"$DROP/ld.exe" --help | grep -q "dump-script-json"
echo "LDPATCHER_PROGRESS 100 Manual build completed"
```

Optional reference variants are also preserved as files:

- `scripts/manual_reference/build_st_ld_manual_canonical_longpath.sh`
- `scripts/manual_reference/build_st_ld_manual_fallback.sh`

Primary rule:

- use `build_st_ld_manual.sh` unless you specifically need to experiment with longpath wrapper generation behavior

### Step 5: Verify

The application verify flow for the active profiles runs two recipes:

1. `sanity_cli`
2. `json_smoke_self_contained`

If you want the manual path to mirror the application behavior as closely as
possible, do both parts in this order.

#### Step 5a: Run the CLI sanity checks

Quick manual equivalent of the `sanity_cli` recipe:

```powershell
& "C:\path\to\drop-dir\ld.exe" --version
& "C:\path\to\drop-dir\ld.exe" --help
& "C:\path\to\drop-dir\ld.exe" --help | findstr dump-script-json
```

What this should prove:

- `ld.exe` exists and starts
- `ld.exe --help` exits successfully
- `--dump-script-json` is advertised in `--help`
- `ld.exe --version` exits successfully

#### Step 5b: Run the self-contained JSON smoke verify

Equivalent script-assisted no-GUI verify:

```powershell
powershell -ExecutionPolicy Bypass -File .\ld_patcher\scripts\manual_reference\verify_drop_self_contained.ps1 `
  -DropDir "C:\path\to\drop-dir" `
  -CubeIdePath "C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE"
```

This wrapper uses the same underlying verify assets and the same current
self-contained verify script as the app workflow:

- `verify_assets/self_contained_smoke/`
- `scripts/verify_smoke_self_contained.ps1`

It produces:

- `<drop-dir>\_verify_smoke_self\ldpatcher_self_smoke.elf`
- `<drop-dir>\_verify_smoke_self\ldpatcher_self_smoke.map`
- `<drop-dir>\_verify_smoke_self\ldpatcher_self_smoke.json`

Current helper script file:

- `scripts/manual_reference/verify_drop_self_contained.ps1`

Inline copy of the current script:

```powershell
$ErrorActionPreference = 'Stop'

param(
  [Parameter(Mandatory = $true)]
  [string] $DropDir,

  [string] $CubeIdePath
)

$env:LDPATCHER_DROP_DIR = $DropDir
if (-not [string]::IsNullOrWhiteSpace($CubeIdePath)) {
  $env:LDPATCHER_CUBEIDE_PATH = $CubeIdePath
}

& (Join-Path (Split-Path -Parent $PSScriptRoot) 'verify_smoke_self_contained.ps1')
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
```

Optional historical real-project smoke script is still preserved as a reference file:

- `scripts/manual_reference/smoke_h7s_fiber_test.ps1`

That script is intentionally secondary because it depends on an external STM32
project and is not the portable default anymore.

### Step 6: Package for CubeIDE

Script-assisted no-GUI helper:

```powershell
powershell -ExecutionPolicy Bypass -File .\ld_patcher\scripts\manual_reference\package_cubeide_drop.ps1 `
  -DropDir "C:\path\to\drop-dir" `
  -PackageDir "C:\path\to\_cubeide-arm-linker-st-13.3.rel1-jsonpatch"
```

Current helper script file:

- `scripts/manual_reference/package_cubeide_drop.ps1`

Inline copy of the current script:

```powershell
$ErrorActionPreference = 'Stop'

param(
  [Parameter(Mandatory = $true)]
  [string] $DropDir,

  [Parameter(Mandatory = $true)]
  [string] $PackageDir
)

if (-not (Test-Path -LiteralPath $DropDir -PathType Container)) {
  throw "Drop directory is missing: $DropDir"
}

$required = @(
  'ld.exe',
  'ld.bfd.exe',
  'arm-none-eabi-ld.exe',
  'arm-none-eabi-ld.bfd.exe',
  'libwinpthread-1.dll',
  'libzstd.dll'
)

foreach ($file in $required) {
  $path = Join-Path $DropDir $file
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Required artifact is missing: $path"
  }
}

New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null
foreach ($file in $required) {
  Copy-Item -LiteralPath (Join-Path $DropDir $file) -Destination (Join-Path $PackageDir $file) -Force
}

[PSCustomObject]@{
  DropDir = (Get-Item -LiteralPath $DropDir).FullName
  PackageDir = (Get-Item -LiteralPath $PackageDir).FullName
  Files = $required
}
```

The current package helper copies:

- `ld.exe`
- `ld.bfd.exe`
- `arm-none-eabi-ld.exe`
- `arm-none-eabi-ld.bfd.exe`
- `libwinpthread-1.dll`
- `libzstd.dll`

### Step 7: Connect the package to STM32CubeIDE

After packaging, point CubeIDE at the new linker package with `-B...`.

Quick example:

```text
-B"C:/path/to/_cubeide-arm-linker-st-13.3.rel1-jsonpatch/"
```

Then optionally confirm the routing once with:

```text
-Wl,-v
```

And enable JSON export with:

```text
-Wl,--dump-script-json=C:/path/to/out.json
```

The full CubeIDE hookup guidance lives in:

- `docs/CUBEIDE_INTEGRATION.md`

## Longpath Note

The historical workspace had two build styles:

- canonical ST longpath-wrapper generation
- fallback rebuild with direct include/fallback assumptions

That knowledge is preserved, but the current canonical working guidance is:

- use the maintained `ld_patcher` workflow first
- if you build by hand, start with `build_st_ld_manual.sh`
- only reach for the longpath-specific variants if you are debugging that exact area

## `-fno-use-linker-plugin`

Historical experiments showed that direct command-line tests could work with and
without `-fno-use-linker-plugin`.

Current guidance:

- do not add `-fno-use-linker-plugin` by default
- use it only as a temporary troubleshooting flag if your toolchain/plugin setup requires it

## End-to-End Manual Checklist

If you want the shortest current script-assisted manual path:

1. copy payload files:
   - `copy_patch_payload.ps1`
2. paste the five hook fragments by hand
3. validate the patch:
   - `check_manual_patch.ps1`
4. build:
   - `build_st_ld_manual.sh`
5. verify:
   - `verify_drop_self_contained.ps1`
6. package:
   - `package_cubeide_drop.ps1`
7. connect the package in CubeIDE:
   - see `docs/CUBEIDE_INTEGRATION.md`

That is the current fully manual workflow using the same payloads and verify
assets as the maintained `ld_patcher` application path.
