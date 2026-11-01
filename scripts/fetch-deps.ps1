# Vendors third-party sources into third_party/ (not committed).
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$third = Join-Path $root 'third_party'
New-Item -ItemType Directory -Path $third -Force | Out-Null

function Get-Dep($name, $url, $ref) {
    $dest = Join-Path $third $name
    if (Test-Path (Join-Path $dest '.git')) {
        Write-Host "== $name already present, skipping"
        return
    }
    Write-Host "== cloning $name ($ref)"
    & git clone --quiet --depth 1 --branch $ref $url $dest
    if ($LASTEXITCODE -ne 0) { throw "git clone failed for $name" }
}

Get-Dep 'imgui' 'https://github.com/ocornut/imgui.git' 'v1.91.5'
Get-Dep 'glfw'  'https://github.com/glfw/glfw.git'   '3.4'

$jsonDir = Join-Path $third 'nlohmann\nlohmann'
New-Item -ItemType Directory -Path $jsonDir -Force | Out-Null
$jsonPath = Join-Path $jsonDir 'json.hpp'
if (-not (Test-Path $jsonPath)) {
    Write-Host '== downloading nlohmann/json single header'
    & curl.exe -sSL --max-time 120 -o $jsonPath 'https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp'
    if ($LASTEXITCODE -ne 0) { throw 'json.hpp download failed' }
}

Write-Host 'Dependencies ready.'
