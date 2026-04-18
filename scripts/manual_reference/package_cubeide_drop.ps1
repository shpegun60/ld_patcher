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
