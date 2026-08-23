# Flash WSL-built ESP32-P4 NSH image from Windows COM7 (CP2102N).
# Usage (PowerShell):
#   powershell -File tools\flash_p4_nsh.ps1 -Variant v1.x -Port COM7
#
# Download mode: hold BOOT, tap RST, then run this script.

param(
    [ValidateSet("selected", "v1.x", "v3.2", "v3.2-usb")]
    [string]$Variant = "selected",
    [string]$Port = "COM7",
    [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$FirmwareRoot = Join-Path $Repo "firmware\esp32p4-nsh"
if ($Variant -eq "selected") {
    $Fw = Join-Path $FirmwareRoot "nuttx.bin"
} else {
    $Fw = Join-Path $FirmwareRoot "$Variant\esp32p4-nsh-$Variant.bin"
}
$IdfPython = "C:\Users\flash\.espressif\python_env\idf6.0_py3.13_env\Scripts\python.exe"

if (-not (Test-Path $Fw)) {
    Write-Error "Missing $Fw. Build in WSL first, then copy firmware."
}

# Apache NuttX Config.mk: ESP32-P4 simple-boot app offset is 0x2000 (ROM).
Write-Host "Flashing $Fw -> $Port (ESP32-P4 simple-boot @ 0x2000)"
& $IdfPython -m esptool --chip esp32p4 -p $Port -b $Baud --before default-reset --after hard-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x2000 $Fw
if ($LASTEXITCODE -ne 0) {
    Write-Host "If connect failed: hold BOOT, tap RST, retry."
    exit $LASTEXITCODE
}
Write-Host "Flash done. Open serial monitor on $Port 115200, look for nsh>."
