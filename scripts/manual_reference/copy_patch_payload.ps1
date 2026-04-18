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

