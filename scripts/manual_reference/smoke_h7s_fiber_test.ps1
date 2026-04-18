$ErrorActionPreference = 'Stop'

$Root = if ($env:LDPATCHER_WORKSPACE_ROOT) { $env:LDPATCHER_WORKSPACE_ROOT } else { 'C:\Users\admin\Documents\my_workspace\gnu\gnu-tools-for-stm32' }
$ProjectRoot = if ($env:LDPATCHER_EXTERNAL_PROJECT_ROOT) { $env:LDPATCHER_EXTERNAL_PROJECT_ROOT } else { 'C:\Users\admin\Documents\my_workspace\stm32\experiments\h7s_fiber_test' }
$ProjectDebug = Join-Path $ProjectRoot 'Boot\Debug'
$Drop = if ($env:LDPATCHER_DROP_DIR) { $env:LDPATCHER_DROP_DIR } else { Join-Path $Root 'manual_build\_cubeide-arm-linker-st-manual-jsonpatch' }
$Gcc = $env:STM32_GCC
$ObjectsList = Join-Path $ProjectDebug 'objects.list'
$LinkerScript = Join-Path $ProjectRoot 'Boot\STM32H7S3L8HX_FLASH.ld'
$OutDir = Join-Path $Root 'manual_build'
$OutElf = Join-Path $OutDir 'h7s_fiber_test_Boot.jsoncheck.elf'
$OutMap = Join-Path $OutDir 'h7s_fiber_test_Boot.jsoncheck.map'
$OutJson = Join-Path $OutDir 'h7s_fiber_test_Boot.json'

if (-not (Test-Path $OutDir)) {
  New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

if (-not $Gcc -or -not (Test-Path $Gcc)) {
  $gccCommand = Get-Command arm-none-eabi-g++.exe -ErrorAction SilentlyContinue
  if ($gccCommand) { $Gcc = $gccCommand.Source }
}

if ((-not $Gcc -or -not (Test-Path $Gcc)) -and (Test-Path 'C:\ST')) {
  $gccMatch = Get-ChildItem 'C:\ST' -Filter 'arm-none-eabi-g++.exe' -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -like '*gnu-tools-for-stm32*' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
  if ($gccMatch) { $Gcc = $gccMatch.FullName }
}

if (-not $Gcc -or -not (Test-Path $Gcc)) {
  throw 'arm-none-eabi-g++.exe was not found. Put it on PATH or set $env:STM32_GCC.'
}

foreach ($path in @($Gcc, (Join-Path $Drop 'ld.exe'), $ObjectsList, $LinkerScript)) {
  if (-not (Test-Path $path)) {
    throw "Required file is missing: $path"
  }
}

foreach ($path in @($OutElf, $OutMap, $OutJson)) {
  if (Test-Path $path) { Remove-Item -Force $path }
}

Push-Location $ProjectDebug
try {
  & $Gcc `
    '-o' $OutElf `
    "@$ObjectsList" `
    '-mcpu=cortex-m7' `
    "-T$LinkerScript" `
    '--specs=nosys.specs' `
    "-Wl,-Map=$OutMap" `
    '-Wl,--gc-sections' `
    '-static' `
    "-B$($Drop.Replace('\', '/'))/" `
    '-Wl,-v' `
    "-Wl,--dump-script-json=$($OutJson.Replace('\', '/'))" `
    '--specs=nano.specs' `
    '-mfpu=fpv5-d16' `
    '-mfloat-abi=hard' `
    '-mthumb' `
    '-Wl,--start-group' `
    '-lc' `
    '-lm' `
    '-lstdc++' `
    '-lsupc++' `
    '-Wl,--end-group'

  if ($LASTEXITCODE -ne 0) {
    throw "Smoke test link failed with exit code $LASTEXITCODE"
  }
}
finally {
  Pop-Location
}

if (-not (Test-Path $OutJson)) {
  throw "JSON output was not produced: $OutJson"
}

[PSCustomObject]@{
  Json = $OutJson
  Elf = $OutElf
  Map = $OutMap
  Drop = $Drop
}

