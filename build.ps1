<#
.SYNOPSIS
    Configure + build FittingRoom.dll using CMakePresets.json (Ninja + MSVC + vcpkg manifest).

.EXAMPLE
    .\build.ps1                                  # release build (build\release)
    .\build.ps1 -Preset debug                    # debug build   (build\debug)
    .\build.ps1 -Preset body-studio-dev          # Body Studio dev channel (build\body-studio-dev)
    .\build.ps1 -Clean                           # delete the build folder, then rebuild
    .\build.ps1 -All                             # also build the unit test executables
    .\build.ps1 -OutputDir "D:\MO2\mods\Fitting Room"
                                                 # deploy DLL + translation + data into that mod folder
                                                 # (default deploy: build\deploy, never the game)

    Unlike the template projects, Fitting Room does not use ..\REF\vcpkg_installed: it builds against the
    x64-windows-static-md triplet and takes CommonLibSSE-NG from the extern\CommonLibSSE-NG submodule, so its
    vcpkg packages live in build\<preset>\vcpkg_installed. The first build downloads them (git + internet).
#>
param(
    [ValidateSet("release", "debug", "body-studio-dev")]
    [string]$Preset = "release",
    [switch]$Clean,
    [switch]$All,
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$pluginName = "FittingRoom"
$buildDir = Join-Path $PSScriptRoot "build\$Preset"

# ---------------------------------------------------- #
#                 Required envs check                  #
# ---------------------------------------------------- #

if (-not $env:VCPKG_ROOT -and (Test-Path "C:\vcpkg")) { $env:VCPKG_ROOT = "C:\vcpkg" }
if (-not $env:VCPKG_ROOT -or -not (Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake")) {
    throw "Environment variable VCPKG_ROOT is missing or does not point to a vcpkg checkout."
}

# A zip download of the repo leaves the submodule folder empty.
$commonlib = Join-Path $PSScriptRoot "extern\CommonLibSSE-NG"
if (-not (Test-Path (Join-Path $commonlib "CMakeLists.txt"))) {
    Write-Host "extern\CommonLibSSE-NG is empty; cloning it (branch ng)..." -ForegroundColor Yellow
    if (Test-Path $commonlib) { Remove-Item -Recurse -Force $commonlib }
    git clone --depth 1 --branch ng --recurse-submodules https://github.com/alandtse/CommonLibSSE-NG.git $commonlib
    if ($LASTEXITCODE -ne 0) { throw "Could not clone CommonLibSSE-NG." }
}

# ---------------------------------------------------- #
#            Load MSVC x64 developer environment       #
# ---------------------------------------------------- #

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vcvars = $null

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) { $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat" }
    }

    # Fallback: vswhere doesn't always list Insiders/Preview installs, so search the usual folders.
    if (-not $vcvars -or -not (Test-Path $vcvars)) {
        $vcvars = Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat",
                                "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat" -ErrorAction SilentlyContinue |
                  Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $vcvars) { throw "No Visual Studio installation with C++ tools found." }

    Write-Host "Loading MSVC environment from $vcvars" -ForegroundColor Cyan
    # vcvars overwrites VCPKG_ROOT with the copy bundled in Visual Studio; keep ours.
    $userVcpkgRoot = $env:VCPKG_ROOT
    # 2>&1 inside cmd: vcvars prints harmless warnings to stderr, which PowerShell 5.1 would raise as errors.
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match "^([^=]+)=(.*)$") { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
    $env:VCPKG_ROOT = $userVcpkgRoot
}
Write-Host "Using vcpkg at $env:VCPKG_ROOT" -ForegroundColor Cyan

foreach ($tool in "cmake", "ninja") {
    if (-not (Get-Command "$tool.exe" -ErrorAction SilentlyContinue)) { throw "$tool.exe not found on PATH." }
}
# CMakePresets.json hardcodes VS Community's ninja.exe; use whichever one is on PATH instead.
$ninja = Get-Command ninja.exe

# ---------------------------------------------------- #
#                   Configure + Build                  #
# ---------------------------------------------------- #

# A cache configured against a different vcpkg can't be reused; start fresh.
$cache = Join-Path $buildDir "CMakeCache.txt"
if (-not $Clean -and (Test-Path $cache)) {
    $expected = ($env:VCPKG_ROOT -replace '\\', '/').TrimEnd('/')
    $cached = Select-String -Path $cache -Pattern '^CMAKE_TOOLCHAIN_FILE:[^=]*=(.*)$' | Select-Object -First 1
    if ($cached -and -not $cached.Matches[0].Groups[1].Value.StartsWith($expected, [StringComparison]::OrdinalIgnoreCase)) {
        Write-Host "Existing build cache uses a different vcpkg; cleaning." -ForegroundColor Yellow
        $Clean = $true
    }
}

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Removing $buildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

# A recompaction that was interrupted (a scanner or editor holding .ninja_log) leaves this behind, and the
# next configure fails with "ninja: error: failed recompaction: Permission denied".
Remove-Item (Join-Path $buildDir ".ninja_log.restat") -Force -ErrorAction SilentlyContinue

# OUTPUT_FOLDER and FR_BODY_STUDIO are cached, so they are passed on every configure (as tools\build.bat
# does): the deploy stays quarantined in build\deploy unless -OutputDir names a mod folder.
$deploy = if ($OutputDir) { $OutputDir } else { Join-Path $PSScriptRoot "build\deploy" }
$deploy = $deploy -replace '\\', '/'

Write-Host "Configuring ($Preset), deploy to $deploy..." -ForegroundColor Cyan
cmake --preset $Preset "-DCMAKE_MAKE_PROGRAM=$($ninja.Source)" "-DOUTPUT_FOLDER=$deploy" "-DFR_BODY_STUDIO=ON"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

Write-Host "Building ($Preset)..." -ForegroundColor Cyan
if ($All) {
    cmake --build $buildDir
} else {
    cmake --build $buildDir --target $pluginName
}
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

$dll = Get-ChildItem $buildDir -Recurse -Filter "$pluginName.dll" -ErrorAction SilentlyContinue |
       Where-Object { $_.FullName -notmatch '\\vcpkg_installed\\' } | Select-Object -First 1
if (-not $dll) { throw "Build finished but $pluginName.dll was not found under $buildDir." }
Write-Host "Build succeeded: $($dll.FullName)" -ForegroundColor Green
# The POST_BUILD step in CMakeLists.txt has copied the DLL, translation file and SKSE\Plugins\FittingRoom data.
Write-Host "Deployed to $deploy" -ForegroundColor Green
