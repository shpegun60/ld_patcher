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
$pexWin32 = Join-Path $SourceRoot 'src\binutils\libiberty\pex-win32.c'

Assert-Regex $ldH 'char \*default_script;\r?\n\r?\n#define X\(ENUM_ID, LONGOPT, HAS_ARG, METAVAR, HELP_TEXT, ARGS_FIELD\)' 'ld.h X-macro glue block'
Assert-Regex $ldlexH 'OPTION_DEFAULT_SCRIPT,\r?\n#define X\(ENUM_ID, LONGOPT, HAS_ARG, METAVAR, HELP_TEXT, ARGS_FIELD\)' 'ldlex.h X-macro glue block'
Assert-Regex $lexsupC '#include "ldjson_options\.def"' 'lexsup.c includes ldjson_options.def'
Assert-Regex $lexsupC 'command_line\.ARGS_FIELD = optarg;' 'lexsup.c parse_args glue block'
Assert-Regex $ldlangC '#include "ldscript_json_impl\.inc"' 'ldlang.c includes implementation'
Assert-Regex $ldlangC 'lang_dump_script_json \(\);' 'ldlang.c runtime hook'
Assert-Regex $pexWin32 '#define handle_long_path_utf8 stm32_local_handle_long_path_utf8' 'pex-win32 longpath fallback glue'
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
