[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'Both')]
    [string]$Configuration = 'Release',

    [string]$BuildDir,
    [string]$QMakePath,
    [int]$Jobs = 1,
    [switch]$ForceLibzip,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
if ($null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue)) {
    $PSNativeCommandUseErrorActionPreference = $false
}

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Assert-LastExitCode {
    param([string]$Description)

    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
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

    $buildRoot = Join-Path $repoDir 'build'
    Ensure-Command -Path $buildRoot -Name 'build root'

    $candidates = Get-ChildItem -LiteralPath $buildRoot -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'Makefile') }

    if (-not $candidates) {
        throw "No Qt build directory with a Makefile was found under: $buildRoot"
    }

    $projectCandidates = @(
        $candidates | Where-Object {
            $makefile = Join-Path $_.FullName 'Makefile'
            $projectRef = Get-MakeVariable -MakefilePath $makefile -VariableName 'QMAKE'
            $projectLine = Select-String -LiteralPath $makefile -Pattern '^\# Project:\s+.+ld_patcher\.pro$' -Quiet
            $projectRef -and $projectLine
        }
    )

    if ($projectCandidates.Count -eq 1) {
        return $projectCandidates[0].FullName
    }

    $selected = @($projectCandidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1)
    if (-not $selected) {
        $selected = @($candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1)
    }

    return $selected[0].FullName
}

function Resolve-QMakePath {
    param([string]$ResolvedBuildDir)

    if ($QMakePath) {
        Ensure-Command -Path $QMakePath -Name 'qmake'
        return (Resolve-Path -LiteralPath $QMakePath).Path
    }

    $makefileQMake = Get-MakeVariable -MakefilePath (Join-Path $ResolvedBuildDir 'Makefile') -VariableName 'QMAKE'
    if ($makefileQMake -and (Test-Path -LiteralPath $makefileQMake)) {
        return (Resolve-Path -LiteralPath $makefileQMake).Path
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

function Resolve-MingwMakePath {
    param([string]$ResolvedQMake)

    $command = Get-Command 'mingw32-make.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $command.Source) {
        return $command.Source
    }

    $qtRoot = Get-QtRootFromQMake -ResolvedQMake $ResolvedQMake
    $toolCandidates = Get-ChildItem -LiteralPath (Join-Path $qtRoot 'Tools') -Directory -Filter 'mingw*' -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'bin\\mingw32-make.exe' } |
        Where-Object { Test-Path -LiteralPath $_ } |
        Sort-Object -Descending

    if ($toolCandidates) {
        return $toolCandidates[0]
    }

    throw 'Unable to resolve mingw32-make from PATH or the Qt Tools directory.'
}

function Resolve-WindeployQtPath {
    param([string]$QtBinDir)

    $candidate = Join-Path $QtBinDir 'windeployqt.exe'
    if (Test-Path -LiteralPath $candidate) {
        return $candidate
    }

    $command = Get-Command 'windeployqt.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $command.Source) {
        return $command.Source
    }

    throw 'Unable to resolve windeployqt.exe from the active Qt installation.'
}

function Test-QMakeRefreshNeeded {
    param(
        [string]$ResolvedBuildDir,
        [string]$ProjectFilePath,
        [string]$TargetConfig
    )

    $mainMakefile = Join-Path $ResolvedBuildDir 'Makefile'
    $configMakefile = if ($TargetConfig -eq 'Debug') {
        Join-Path $ResolvedBuildDir 'Makefile.Debug'
    } else {
        Join-Path $ResolvedBuildDir 'Makefile.Release'
    }

    if (-not (Test-Path -LiteralPath $mainMakefile) -or -not (Test-Path -LiteralPath $configMakefile)) {
        return $true
    }

    $projectTime = (Get-Item -LiteralPath $ProjectFilePath).LastWriteTimeUtc
    $makefileTime = (Get-Item -LiteralPath $mainMakefile).LastWriteTimeUtc
    return $projectTime -gt $makefileTime
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoDir = $scriptDir

if ($Jobs -lt 1) {
    throw 'Jobs must be at least 1.'
}

$appBuildDir = Resolve-BuildDir
$projectFile = Join-Path $repoDir 'ld_patcher.pro'
$ensureLibzipScript = Join-Path $repoDir 'ensure_libzip.ps1'
Ensure-Command -Path $ensureLibzipScript -Name 'ensure_libzip script'

$qmake = Resolve-QMakePath -ResolvedBuildDir $appBuildDir
Ensure-Command -Path $qmake -Name 'qmake'

$qtBin = Get-QMakeQueryValue -ResolvedQMake $qmake -Key 'QT_INSTALL_BINS'
$windeployqt = Resolve-WindeployQtPath -QtBinDir $qtBin
Ensure-Command -Path $windeployqt -Name 'windeployqt'
$mingwMake = Resolve-MingwMakePath -ResolvedQMake $qmake
Ensure-Command -Path $mingwMake -Name 'mingw32-make'

$mingwBin = Split-Path -Parent $mingwMake
Prepend-PathEntry -Entry $mingwBin
Prepend-PathEntry -Entry $qtBin

function Invoke-QMakeBuild {
    param(
        [ValidateSet('Release', 'Debug')]
        [string]$TargetConfig
    )

    $needsQMake = Test-QMakeRefreshNeeded -ResolvedBuildDir $appBuildDir -ProjectFilePath $projectFile -TargetConfig $TargetConfig

    if ($Clean) {
        if ($needsQMake) {
            Write-Step "Running qmake for $TargetConfig build"
            Push-Location $appBuildDir
            try {
                & $qmake $projectFile
                Assert-LastExitCode "qmake for $TargetConfig"
            }
            finally {
                Pop-Location
            }
            $needsQMake = $false
        }

        $makefile = if ($TargetConfig -eq 'Debug') { 'Makefile.Debug' } else { 'Makefile.Release' }
        Write-Step "Cleaning $TargetConfig build"
        Push-Location $appBuildDir
        try {
            & $mingwMake -f $makefile clean
            Assert-LastExitCode "$TargetConfig clean"
        }
        finally {
            Pop-Location
        }
        $needsQMake = $true
    }

    if ($needsQMake) {
        Write-Step "Running qmake for $TargetConfig build"
        Push-Location $appBuildDir
        try {
            & $qmake $projectFile
            Assert-LastExitCode "qmake for $TargetConfig"
        }
        finally {
            Pop-Location
        }
    } else {
        Write-Step "Using existing qmake-generated files for $TargetConfig build"
    }

    $makefile = if ($TargetConfig -eq 'Debug') { 'Makefile.Debug' } else { 'Makefile.Release' }
    Write-Step "Building ld_patcher ($TargetConfig)"
    Push-Location $appBuildDir
    try {
        $maxAttempts = 2
        for ($attempt = 1; $attempt -le $maxAttempts; ++$attempt) {
            & $mingwMake -f $makefile ("-j{0}" -f $Jobs)
            if ($LASTEXITCODE -eq 0) {
                break
            }

            if ($attempt -ge $maxAttempts) {
                Assert-LastExitCode "$TargetConfig build"
            }

            Write-Host "Retrying $TargetConfig build after an initial non-zero exit..." -ForegroundColor Yellow
            Start-Sleep -Milliseconds 250
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-WindeployQt {
    param(
        [ValidateSet('Release', 'Debug')]
        [string]$TargetConfig
    )

    $runtimeDir = if ($TargetConfig -eq 'Debug') {
        Join-Path $appBuildDir 'debug'
    } else {
        Join-Path $appBuildDir 'release'
    }

    $exePath = Join-Path $runtimeDir 'ld_patcher.exe'
    Ensure-Command -Path $exePath -Name "ld_patcher.exe ($TargetConfig)"

    $args = @(
        '--force',
        '--compiler-runtime',
        '--no-translations',
        '--dir', $runtimeDir
    )
    if ($TargetConfig -eq 'Debug') {
        $args += '--debug'
    } else {
        $args += '--release'
    }
    $args += $exePath

    Write-Step "Deploying Qt runtime ($TargetConfig)"
    & $windeployqt @args
    if ($LASTEXITCODE -ne 0) {
        if ($TargetConfig -eq 'Debug') {
            Write-Warning "windeployqt for Debug failed with exit code $LASTEXITCODE. The debug executable was built successfully, but standalone debug deployment is unavailable with the current Qt installation."
            return
        }
        Assert-LastExitCode "windeployqt for $TargetConfig"
    }
}

if ($ForceLibzip) {
    Write-Step "Preparing libzip shared runtime"
    $ensureBaseArgs = @(
        '-ExecutionPolicy', 'Bypass',
        '-File', $ensureLibzipScript,
        '-BuildDir', $appBuildDir,
        '-QMakePath', $qmake
    )
    function Invoke-EnsureLibzip {
        param([string]$TargetConfig)

        $args = @($ensureBaseArgs + @('-Configuration', $TargetConfig, '-Force'))
        & powershell @args
        Assert-LastExitCode "ensure_libzip for $TargetConfig"
    }
    if ($Configuration -eq 'Both') {
        Invoke-EnsureLibzip -TargetConfig 'Release'
        Invoke-EnsureLibzip -TargetConfig 'Debug'
    } else {
        Invoke-EnsureLibzip -TargetConfig $Configuration
    }
}

switch ($Configuration) {
    'Release' {
        Invoke-QMakeBuild -TargetConfig 'Release'
        Invoke-WindeployQt -TargetConfig 'Release'
    }
    'Debug' {
        Invoke-QMakeBuild -TargetConfig 'Debug'
        Invoke-WindeployQt -TargetConfig 'Debug'
    }
    'Both' {
        Invoke-QMakeBuild -TargetConfig 'Release'
        Invoke-WindeployQt -TargetConfig 'Release'
        Invoke-QMakeBuild -TargetConfig 'Debug'
        Invoke-WindeployQt -TargetConfig 'Debug'
    }
}

Write-Step "Build completed"
switch ($Configuration) {
    'Release' { Write-Host (Join-Path $appBuildDir 'release\ld_patcher.exe') }
    'Debug'   { Write-Host (Join-Path $appBuildDir 'debug\ld_patcher.exe') }
    'Both' {
        Write-Host (Join-Path $appBuildDir 'release\ld_patcher.exe')
        Write-Host (Join-Path $appBuildDir 'debug\ld_patcher.exe')
    }
}
