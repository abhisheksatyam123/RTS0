# Wrapper — delegates to cross-platform Python driver (Vulkan: forward_plus).
# Usage: powershell -File tools\build.ps1 -Target windows
param(
    [Parameter(Position = 0)]
    [ValidateSet("windows", "linux", "android", "ios", "all")]
    [string]$Target = "windows"
)
$dir = Split-Path -Parent $PSScriptRoot
& python "$dir/build.py" $Target
exit $LASTEXITCODE
