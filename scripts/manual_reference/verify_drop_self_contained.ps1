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

