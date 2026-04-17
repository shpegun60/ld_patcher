$ErrorActionPreference = 'Stop'

$PatcherRoot = Split-Path -Parent $PSScriptRoot
$Drop = if ($env:LDPATCHER_DROP_DIR) { $env:LDPATCHER_DROP_DIR } else { throw 'LDPATCHER_DROP_DIR is required.' }
$CubeIdePath = $env:LDPATCHER_CUBEIDE_PATH
$VerifyOutDir = Join-Path $Drop '_verify_smoke_self'
$WorkDir = Join-Path $VerifyOutDir 'work'
$AssetsRoot = Join-Path $PatcherRoot 'verify_assets\self_contained_smoke'
$LinkerScript = Join-Path $AssetsRoot 'self_smoke.ld'
$OutElf = Join-Path $VerifyOutDir 'ldpatcher_self_smoke.elf'
$OutMap = Join-Path $VerifyOutDir 'ldpatcher_self_smoke.map'
$OutJson = Join-Path $VerifyOutDir 'ldpatcher_self_smoke.json'
$MainObj = Join-Path $WorkDir 'main.o'
$HelperObj = Join-Path $WorkDir 'helper.o'
$UnusedObj = Join-Path $WorkDir 'unused.o'

function Test-HasProperty {
  param(
    [Parameter(Mandatory = $true)] $Object,
    [Parameter(Mandatory = $true)] [string] $Name
  )

  return $null -ne $Object -and $null -ne $Object.PSObject.Properties[$Name]
}

function Assert-HasProperty {
  param(
    [Parameter(Mandatory = $true)] $Object,
    [Parameter(Mandatory = $true)] [string] $Name,
    [Parameter(Mandatory = $true)] [string] $Context
  )

  if (-not (Test-HasProperty $Object $Name)) {
    throw "Missing property '$Name' in $Context"
  }
}

function Get-NormalizedCubeIdeSearchRoot {
  param([string] $Path)

  if ([string]::IsNullOrWhiteSpace($Path)) {
    return $null
  }

  try {
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
  } catch {
    return $null
  }

  if (-not $item.PSIsContainer) {
    $item = $item.Directory
  }

  if ($null -eq $item) {
    return $null
  }

  if ($item.Name -ieq 'STM32CubeIDE') {
    return $item.FullName
  }

  $nestedCubeIde = Join-Path $item.FullName 'STM32CubeIDE'
  if (Test-Path -LiteralPath $nestedCubeIde -PathType Container) {
    return (Get-Item -LiteralPath $nestedCubeIde).FullName
  }

  return $item.FullName
}

function Find-CubeIdeCompiler {
  param([string] $Path)

  $searchRoot = Get-NormalizedCubeIdeSearchRoot $Path
  if ([string]::IsNullOrWhiteSpace($searchRoot)) {
    return $null
  }

  $pluginNeedle = 'com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.'
  $preferredMatches = Get-ChildItem -LiteralPath $searchRoot -Filter 'arm-none-eabi-g++.exe' -Recurse -ErrorAction SilentlyContinue |
    Where-Object {
      $_.FullName -like "*$pluginNeedle*" -and
      ($_.FullName -match '[\\/]+tools[\\/]+bin[\\/]+arm-none-eabi-g\+\+\.exe$')
    } |
    Sort-Object FullName -Descending

  if ($preferredMatches) {
    return @{
      Path = $preferredMatches[0].FullName
      Source = "CubeIDE path: $searchRoot"
    }
  }

  $fallbackMatches = Get-ChildItem -LiteralPath $searchRoot -Filter 'arm-none-eabi-g++.exe' -Recurse -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending
  if ($fallbackMatches) {
    return @{
      Path = $fallbackMatches[0].FullName
      Source = "Compiler root fallback: $searchRoot"
    }
  }

  return $null
}

function Resolve-ArmGcc {
  if ($env:STM32_GCC -and (Test-Path -LiteralPath $env:STM32_GCC)) {
    return @{
      Path = (Get-Item -LiteralPath $env:STM32_GCC).FullName
      Source = 'STM32_GCC'
    }
  }

  $gccCommand = Get-Command arm-none-eabi-g++.exe -ErrorAction SilentlyContinue
  if ($gccCommand) {
    return @{
      Path = $gccCommand.Source
      Source = 'PATH'
    }
  }

  $cubeIdeCompiler = Find-CubeIdeCompiler $CubeIdePath
  if ($cubeIdeCompiler) {
    return $cubeIdeCompiler
  }

  foreach ($root in @('C:\ST', 'C:\Program Files\STMicroelectronics')) {
    if (-not (Test-Path -LiteralPath $root)) {
      continue
    }

    $candidate = Find-CubeIdeCompiler $root
    if ($candidate) {
      return $candidate
    }
  }

  return $null
}

function Invoke-Checked {
  param(
    [Parameter(Mandatory = $true)] [string] $Tool,
    [Parameter(Mandatory = $true)] [string[]] $Arguments,
    [Parameter(Mandatory = $true)] [string] $Description
  )

  & $Tool @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Description failed with exit code $LASTEXITCODE"
  }
}

Write-Output 'LDPATCHER_PROGRESS 5 Preparing self-contained smoke test'
$gccInfo = Resolve-ArmGcc
if ($null -eq $gccInfo) {
  throw 'arm-none-eabi-g++.exe was not found. Put it on PATH, set STM32_GCC, or point ld_patcher at your CubeIDE / compiler root.'
}
$Gcc = $gccInfo.Path

foreach ($path in @($Gcc, (Join-Path $Drop 'ld.exe'), $AssetsRoot, $LinkerScript)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Required file is missing: $path"
  }
}

Write-Output "Compiler: $Gcc"
Write-Output "Compiler source: $($gccInfo.Source)"
Write-Output "CubeIDE / compiler root: $(if ([string]::IsNullOrWhiteSpace($CubeIdePath)) { '<auto>' } else { $CubeIdePath })"

Write-Output 'LDPATCHER_PROGRESS 12 Resetting previous self-contained artifacts'
if (Test-Path -LiteralPath $VerifyOutDir) {
  Remove-Item -LiteralPath $VerifyOutDir -Recurse -Force
}
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null

$commonCompileArgs = @(
  '-std=gnu++17',
  '-mcpu=cortex-m7',
  '-mthumb',
  '-Os',
  '-g0',
  '-ffreestanding',
  '-fno-exceptions',
  '-fno-rtti',
  '-fno-threadsafe-statics',
  '-fno-use-cxa-atexit',
  '-fno-unwind-tables',
  '-fno-asynchronous-unwind-tables',
  '-ffunction-sections',
  '-fdata-sections',
  '-fno-common'
)

Write-Output 'LDPATCHER_PROGRESS 25 Compiling main.cpp'
Invoke-Checked -Tool $Gcc -Arguments ($commonCompileArgs + @('-c', (Join-Path $AssetsRoot 'main.cpp'), '-o', $MainObj)) -Description 'main.cpp compilation'

Write-Output 'LDPATCHER_PROGRESS 40 Compiling helper.cpp'
Invoke-Checked -Tool $Gcc -Arguments ($commonCompileArgs + @('-c', (Join-Path $AssetsRoot 'helper.cpp'), '-o', $HelperObj)) -Description 'helper.cpp compilation'

Write-Output 'LDPATCHER_PROGRESS 55 Compiling unused.cpp'
Invoke-Checked -Tool $Gcc -Arguments ($commonCompileArgs + @('-c', (Join-Path $AssetsRoot 'unused.cpp'), '-o', $UnusedObj)) -Description 'unused.cpp compilation'

Write-Output 'LDPATCHER_PROGRESS 68 Linking with patched linker'
Invoke-Checked -Tool $Gcc -Arguments @(
  '-mcpu=cortex-m7',
  '-mthumb',
  '-nostdlib',
  '-nostartfiles',
  '-nodefaultlibs',
  $MainObj,
  $HelperObj,
  $UnusedObj,
  "-T$LinkerScript",
  '-Wl,--entry=main',
  '-Wl,--gc-sections',
  "-Wl,-Map=$OutMap",
  "-Wl,--dump-script-json=$($OutJson.Replace('\', '/'))",
  "-B$($Drop.Replace('\', '/'))/",
  '-o',
  $OutElf
) -Description 'self-contained smoke link'

if (-not (Test-Path -LiteralPath $OutJson)) {
  throw "JSON output was not produced: $OutJson"
}

Write-Output 'LDPATCHER_PROGRESS 82 Parsing produced JSON'
$json = Get-Content -LiteralPath $OutJson -Raw | ConvertFrom-Json

foreach ($topLevel in @('format', 'output', 'memory_regions', 'output_sections', 'input_sections', 'discarded_input_sections', 'symbols')) {
  Assert-HasProperty $json $topLevel 'root JSON object'
}

if (Test-HasProperty $json 'script_variables') {
  throw 'Unexpected legacy field script_variables is still present in JSON output.'
}

foreach ($field in @('name', 'major', 'minor')) {
  Assert-HasProperty $json.format $field 'format'
}

Assert-HasProperty $json.output 'entry_symbol' 'output'

foreach ($collection in @('memory_regions', 'output_sections', 'input_sections', 'symbols')) {
  $node = $json.$collection
  foreach ($field in @('items', 'by_name', 'null_name_ids')) {
    Assert-HasProperty $node $field $collection
  }
}

$requiredRegions = @('FLASH', 'RAM')
foreach ($name in $requiredRegions) {
  if (-not $json.memory_regions.by_name.$name) {
    throw "Required memory region '$name' was not found"
  }
}

$requiredSections = @('.text', '.data', '.bss')
foreach ($name in $requiredSections) {
  if (-not $json.output_sections.by_name.$name) {
    throw "Required output section '$name' was not found"
  }
}

$symbolsByName = $json.symbols.by_name
foreach ($symbolName in @('main', 'helper_add', '_sidata', '_estack')) {
  if (-not $symbolsByName.$symbolName) {
    throw "Required symbol '$symbolName' was not found"
  }
}

$discardedCount = @($json.discarded_input_sections).Count
if ($discardedCount -lt 1) {
  throw 'Expected at least one discarded input section from unused.cpp, but none were recorded.'
}

Write-Output 'LDPATCHER_PROGRESS 100 Self-contained smoke test completed'
[PSCustomObject]@{
  Json = $OutJson
  Compiler = $Gcc
  CompilerSource = $gccInfo.Source
  Format = '{0} {1}.{2}' -f $json.format.name, $json.format.major, $json.format.minor
  EntrySymbol = $json.output.entry_symbol
  MemoryRegions = $json.memory_regions.items.Count
  OutputSections = $json.output_sections.items.Count
  InputSections = $json.input_sections.items.Count
  DiscardedInputSections = @($json.discarded_input_sections).Count
  Symbols = $json.symbols.items.Count
}
