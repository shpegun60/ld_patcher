[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [string]$BuildDir,
    [string]$QMakePath,
    [string]$RuntimeDir,
    [string]$StampPath,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
if ($null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue)) {
    $PSNativeCommandUseErrorActionPreference = $false
}

function Assert-LastExitCode {
    param([string]$Description)

    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

function Get-LatestWriteTimeUtc {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return [datetime]::MinValue
    }

    return (Get-ChildItem -LiteralPath $Path -Recurse -File | Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum
}

function Ensure-Command {
    param(
        [string]$Path,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Name not found: $Path"
    }
}

function Convert-ToCMakePath {
    param([string]$Path)

    return $Path -replace '\\', '/'
}

function Get-MakeVariable {
    param(
        [string]$MakefilePath,
        [string]$VariableName
    )

    if (-not (Test-Path -LiteralPath $MakefilePath)) {
        return $null
    }

    $match = Select-String -LiteralPath $MakefilePath `
        -Pattern ("^{0}\s*=\s*(.+)$" -f [regex]::Escape($VariableName)) |
        Select-Object -First 1
    if (-not $match) {
        return $null
    }

    return $match.Matches[0].Groups[1].Value.Trim()
}

function Prepend-PathEntry {
    param([string]$Entry)

    if (-not $Entry) {
        return
    }

    $resolved = [System.IO.Path]::GetFullPath($Entry)
    $parts = @($env:PATH -split ';' | Where-Object { $_ })
    if ($parts -contains $resolved) {
        return
    }

    $env:PATH = "$resolved;$env:PATH"
}

function Resolve-BuildDir {
    if ($BuildDir) {
        Ensure-Command -Path $BuildDir -Name 'build directory'
        return (Resolve-Path -LiteralPath $BuildDir).Path
    }

    if ($RuntimeDir) {
        return (Resolve-Path -LiteralPath (Split-Path -Parent $RuntimeDir)).Path
    }

    $buildRoot = Join-Path $repoDir 'build'
    Ensure-Command -Path $buildRoot -Name 'build root'

    $candidates = Get-ChildItem -LiteralPath $buildRoot -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'Makefile') }

    if (-not $candidates) {
        throw "No Qt build directory with a Makefile was found under: $buildRoot"
    }

    return @($candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1)[0].FullName
}

function Resolve-QMakePath {
    param([string]$ResolvedBuildDir)

    if ($QMakePath) {
        Ensure-Command -Path $QMakePath -Name 'qmake'
        return (Resolve-Path -LiteralPath $QMakePath).Path
    }

    foreach ($makefileName in @('Makefile', 'Makefile.Debug', 'Makefile.Release')) {
        $makefilePath = Join-Path $ResolvedBuildDir $makefileName
        $makefileQMake = Get-MakeVariable -MakefilePath $makefilePath -VariableName 'QMAKE'
        if ($makefileQMake -and (Test-Path -LiteralPath $makefileQMake)) {
            return (Resolve-Path -LiteralPath $makefileQMake).Path
        }
    }

    foreach ($candidate in @('qmake.exe', 'qmake6.exe', 'qmake')) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command -and $command.Source) {
            return $command.Source
        }
    }

    throw 'Unable to resolve qmake from the project build, environment, or PATH.'
}

function Get-QMakeQueryValue {
    param(
        [string]$ResolvedQMake,
        [string]$Key
    )

    $value = & $ResolvedQMake -query $Key
    if ($LASTEXITCODE -ne 0 -or -not $value) {
        throw "qmake -query $Key failed"
    }

    return $value.Trim()
}

function Get-QtRootFromQMake {
    param([string]$ResolvedQMake)

    $qtPrefix = Get-QMakeQueryValue -ResolvedQMake $ResolvedQMake -Key 'QT_INSTALL_PREFIX'
    $cursor = $qtPrefix
    while ($cursor) {
        if (Test-Path -LiteralPath (Join-Path $cursor 'Tools')) {
            return $cursor
        }

        $parent = Split-Path -Parent $cursor
        if (-not $parent -or $parent -eq $cursor) {
            break
        }
        $cursor = $parent
    }

    throw "Unable to infer the Qt root from qmake prefix: $qtPrefix"
}

function Find-FirstExistingPath {
    param([string[]]$Candidates)

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Resolve-ToolPath {
    param(
        [string]$CommandName,
        [string[]]$FallbackCandidates,
        [string]$DisplayName,
        [switch]$PreferFallback
    )

    if ($PreferFallback) {
        $fallback = Find-FirstExistingPath -Candidates $FallbackCandidates
        if ($fallback) {
            return $fallback
        }
    }
    else {
        $command = Get-Command $CommandName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command -and $command.Source) {
            return $command.Source
        }

        $fallback = Find-FirstExistingPath -Candidates $FallbackCandidates
        if ($fallback) {
            return $fallback
        }
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $command.Source) {
        return $command.Source
    }

    throw "$DisplayName not found"
}

function Resolve-ZlibArtifacts {
    param([string]$PreferredPrefix)

    $runtimeCandidates = New-Object System.Collections.Generic.List[string]
    $includeCandidates = New-Object System.Collections.Generic.List[string]
    $importCandidates = New-Object System.Collections.Generic.List[string]
    $prefixRoots = New-Object System.Collections.Generic.List[string]

    if ($PreferredPrefix) {
        $prefixRoots.Add($PreferredPrefix)
    }

    foreach ($fallbackRoot in @('C:\msys64\mingw64', 'C:\msys64\ucrt64')) {
        if ($fallbackRoot -ne $PreferredPrefix) {
            $prefixRoots.Add($fallbackRoot)
        }
    }

    foreach ($prefixRoot in @($prefixRoots | Where-Object { $_ } | Select-Object -Unique)) {
        $runtimeCandidates.Add((Join-Path $prefixRoot 'bin\zlib1.dll'))
        $includeCandidates.Add((Join-Path $prefixRoot 'include'))
        $importCandidates.Add((Join-Path $prefixRoot 'lib\libz.dll.a'))
    }

    foreach ($envVar in @('ZLIB_RUNTIME_DLL', 'ZLIB_DLL')) {
        $value = [Environment]::GetEnvironmentVariable($envVar)
        if ($value) {
            $runtimeCandidates.Add($value)
        }
    }
    $includeFromEnv = [Environment]::GetEnvironmentVariable('ZLIB_INCLUDE_DIR')
    if ($includeFromEnv) {
        $includeCandidates.Add($includeFromEnv)
    }
    foreach ($envVar in @('ZLIB_LIBRARY', 'ZLIB_IMPORT_LIB')) {
        $value = [Environment]::GetEnvironmentVariable($envVar)
        if ($value) {
            $importCandidates.Add($value)
        }
    }

    try {
        $whereRuntime = & where.exe zlib1.dll 2>$null
        if ($LASTEXITCODE -eq 0) {
            foreach ($entry in $whereRuntime) {
                $runtimeCandidates.Add($entry)
            }
        }
    }
    catch {
    }

    foreach ($envVar in @('MSYSTEM_PREFIX', 'MINGW_PREFIX')) {
        $value = [Environment]::GetEnvironmentVariable($envVar)
        if ($value) {
            $prefixRoots.Add($value)
        }
    }

    foreach ($runtimePath in $runtimeCandidates.ToArray()) {
        if (Test-Path -LiteralPath $runtimePath) {
            $prefixRoots.Add((Split-Path -Parent (Split-Path -Parent $runtimePath)))
        }
    }

    $prefixRoots = @($prefixRoots | Where-Object { $_ } | Select-Object -Unique)
    foreach ($prefixRoot in $prefixRoots) {
        $runtimeCandidates.Add((Join-Path $prefixRoot 'bin\zlib1.dll'))
        $includeCandidates.Add((Join-Path $prefixRoot 'include'))
        $importCandidates.Add((Join-Path $prefixRoot 'lib\libz.dll.a'))
    }

    $runtimeDll = Find-FirstExistingPath -Candidates $runtimeCandidates.ToArray()
    $includeDir = Find-FirstExistingPath -Candidates $includeCandidates.ToArray()
    $importLib = Find-FirstExistingPath -Candidates $importCandidates.ToArray()

    Ensure-Command -Path $runtimeDll -Name 'zlib runtime dll'
    Ensure-Command -Path $includeDir -Name 'zlib include directory'
    Ensure-Command -Path $importLib -Name 'zlib import library'

    return @{
        RuntimeDll = $runtimeDll
        IncludeDir = $includeDir
        ImportLib = $importLib
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoDir = $scriptDir

$libzipSourceDir = Join-Path $repoDir 'third_party\libzip'

Ensure-Command -Path $libzipSourceDir -Name 'libzip source tree'

$qtBuildDir = Resolve-BuildDir
$qmake = Resolve-QMakePath -ResolvedBuildDir $qtBuildDir
$qtBin = Get-QMakeQueryValue -ResolvedQMake $qmake -Key 'QT_INSTALL_BINS'
$qtRoot = Get-QtRootFromQMake -ResolvedQMake $qmake

$cmakeCandidates = Get-ChildItem -LiteralPath (Join-Path $qtRoot 'Tools') -Directory -Filter 'CMake*' -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName 'bin\cmake.exe' } |
    Sort-Object -Descending
$mingwBinCandidates = Get-ChildItem -LiteralPath (Join-Path $qtRoot 'Tools') -Directory -Filter 'mingw*' -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName 'bin' } |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_ 'gcc.exe')) -and
        (Test-Path -LiteralPath (Join-Path $_ 'mingw32-make.exe'))
    } |
    Sort-Object -Descending

$mingwMake = Resolve-ToolPath -CommandName 'mingw32-make.exe' -FallbackCandidates @($mingwBinCandidates | ForEach-Object { Join-Path $_ 'mingw32-make.exe' }) -DisplayName 'mingw32-make' -PreferFallback
$gcc = Resolve-ToolPath -CommandName 'gcc.exe' -FallbackCandidates @($mingwBinCandidates | ForEach-Object { Join-Path $_ 'gcc.exe' }) -DisplayName 'gcc' -PreferFallback
$cmake = Resolve-ToolPath -CommandName 'cmake.exe' -FallbackCandidates $cmakeCandidates -DisplayName 'cmake' -PreferFallback

$mingwBin = Split-Path -Parent $gcc
Prepend-PathEntry -Entry $mingwBin
Prepend-PathEntry -Entry $qtBin

$preferredZlibPrefix = $null
if ($gcc -match '\\ucrt64\\bin\\gcc\.exe$') {
    $preferredZlibPrefix = 'C:\msys64\ucrt64'
}
elseif ($gcc -match '\\(mingw64|mingw1310_64)\\bin\\gcc\.exe$') {
    $preferredZlibPrefix = 'C:\msys64\mingw64'
}

$zlib = Resolve-ZlibArtifacts -PreferredPrefix $preferredZlibPrefix
$gccCMake = Convert-ToCMakePath $gcc
$zlibIncludeCMake = Convert-ToCMakePath $zlib.IncludeDir
$zlibImportLibCMake = Convert-ToCMakePath $zlib.ImportLib

if (-not $RuntimeDir) {
    $RuntimeDir = Join-Path $qtBuildDir $Configuration.ToLowerInvariant()
}

$libzipBuildDir = Join-Path $qtBuildDir 'libzip-shared'
$libzipCache = Join-Path $libzipBuildDir 'CMakeCache.txt'
$libzipDll = Join-Path $libzipBuildDir 'lib\libzip.dll'

function Test-LibzipNeedsBuild {
    if ($Force) {
        return $true
    }

    if (-not (Test-Path -LiteralPath $libzipCache) -or -not (Test-Path -LiteralPath $libzipDll)) {
        return $true
    }

    $sourceTime = Get-LatestWriteTimeUtc -Path $libzipSourceDir
    $dllTime = (Get-Item -LiteralPath $libzipDll).LastWriteTimeUtc
    return $sourceTime -gt $dllTime
}

if (Test-LibzipNeedsBuild) {
    $cmakeArgs = @(
        '-S', $libzipSourceDir,
        '-B', $libzipBuildDir,
        '-G', 'MinGW Makefiles',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_SHARED_LIBS=ON',
        '-DBUILD_TOOLS=OFF',
        '-DBUILD_REGRESS=OFF',
        '-DBUILD_OSSFUZZ=OFF',
        '-DBUILD_EXAMPLES=OFF',
        '-DBUILD_DOC=OFF',
        '-DLIBZIP_DO_INSTALL=OFF',
        '-DENABLE_COMMONCRYPTO=OFF',
        '-DENABLE_GNUTLS=OFF',
        '-DENABLE_OPENSSL=OFF',
        '-DENABLE_BZIP2=OFF',
        '-DENABLE_LZMA=OFF',
        '-DENABLE_ZSTD=OFF',
        ('-DCMAKE_C_COMPILER={0}' -f $gccCMake),
        ('-DZLIB_INCLUDE_DIR={0}' -f $zlibIncludeCMake),
        ('-DZLIB_LIBRARY={0}' -f $zlibImportLibCMake)
    )

    & $cmake @cmakeArgs
    Assert-LastExitCode 'cmake configure for libzip'

    Push-Location $libzipBuildDir
    try {
        & $mingwMake -j4
        Assert-LastExitCode 'libzip build'
    }
    finally {
        Pop-Location
    }
}

Ensure-Command -Path $libzipDll -Name 'built libzip.dll'

New-Item -ItemType Directory -Force -Path $RuntimeDir | Out-Null
Copy-Item -LiteralPath $libzipDll -Destination (Join-Path $RuntimeDir 'libzip.dll') -Force
Copy-Item -LiteralPath $zlib.RuntimeDll -Destination (Join-Path $RuntimeDir 'zlib1.dll') -Force

if ($StampPath) {
    $stampDir = Split-Path -Parent $StampPath
    if ($stampDir) {
        New-Item -ItemType Directory -Force -Path $stampDir | Out-Null
    }
    Set-Content -LiteralPath $StampPath -Value ([datetime]::UtcNow.ToString('o'))
}
