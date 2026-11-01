# Builds the host into build/ using a 64-bit mingw-w64 toolchain.
param(
    [string]$Toolchain = '',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'

function Find-Toolchain {
    param([string]$explicit)

    if ($explicit) { return (Split-Path -Parent $explicit) }

    $onPath = Get-Command g++ -ErrorAction SilentlyContinue
    if ($onPath) {
        $machine = & $onPath.Source -dumpmachine 2>$null
        if ($machine -like 'x86_64*') { return (Split-Path -Parent $onPath.Source) }
    }

    $winlibs = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Directory -Filter 'BrechtSanders.WinLibs*' -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'mingw64\bin' } |
        Where-Object { Test-Path (Join-Path $_ 'g++.exe') } |
        Select-Object -First 1
    if ($winlibs) { return $winlibs }

    foreach ($candidate in @('C:\msys64\mingw64\bin', 'C:\mingw64\bin')) {
        if (Test-Path (Join-Path $candidate 'g++.exe')) { return $candidate }
    }

    throw 'No 64-bit mingw-w64 g++ found. Pass -Toolchain C:\path\to\g++.exe'
}

$bin = Find-Toolchain -explicit $Toolchain
Write-Host "toolchain: $bin"

$env:PATH = "$bin;$env:PATH"
$ninja = Join-Path $bin 'ninja.exe'
$generator = if (Test-Path $ninja) { 'Ninja' } else { 'MinGW Makefiles' }

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

& cmake -S $root -B $build -G $generator `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER="$bin\gcc.exe" `
    -DCMAKE_CXX_COMPILER="$bin\g++.exe"
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

& cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Write-Host "built: $(Join-Path $build 'StreamingEnabled.exe')"
