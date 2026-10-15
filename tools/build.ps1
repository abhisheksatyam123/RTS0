# Wrapper — delegates to cross-platform Python driver (Vulkan: forward_plus).
# Usage: powershell -File tools\build.ps1 -Target windows
param(
    [Parameter(Position = 0)]
    [ValidateSet("windows", "linux", "android", "macos", "ios", "all")]
    [string]$Target = "windows"
)

& python "$PSScriptRoot/build.py" $Target
exit $LASTEXITCODE
