# Windows build wrapper for RTS0.
# Usage: powershell -File tools\build.ps1 -Target windows
# Requires: $env:GODOT pointing to Godot .NET editor exe, plus matching templates (docs/BUILD.md).
param(
    [Parameter(Position = 0)]
    [ValidateSet("windows", "linux", "android", "ios")]
    [string]$Target = "windows"
)

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectDir "build"

if (-not $env:GODOT) {
    Write-Error "Set `$env:GODOT to the Godot .NET editor executable."
}

Write-Host "== Importing project resources =="
& $env:GODOT --headless --path $projectDir --import
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

function Export-Preset([string]$Preset, [string]$OutPath) {
    Write-Host "== Exporting: $Preset -> $OutPath =="
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutPath) | Out-Null
    & $env:GODOT --headless --path $projectDir --export-release $Preset $OutPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

switch ($Target) {
    "windows" { Export-Preset "Windows Desktop" (Join-Path $outDir "windows\RTS0.exe") }
    "linux"   { Export-Preset "Linux"           (Join-Path $outDir "linux\RTS0.x86_64") }
    "android" { Export-Preset "Android"         (Join-Path $outDir "android\RTS0.apk") }
    "ios"     { Export-Preset "iOS"             (Join-Path $outDir "ios\RTS0") }
}

Write-Host "== Done. Output in $outDir =="
