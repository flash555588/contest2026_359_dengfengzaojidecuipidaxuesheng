# Flash WSL-built ESP32-P4 NSH image from Windows COM7 (CP2102N).
# Usage (PowerShell):
#   . C:\Users\flash\esp\activate-p4.ps1
#   powershell -File tools\flash_p4_nsh.ps1
#
# Download mode: hold BOOT, tap RST, then run this script.

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Fw = Join-Path $Repo "firmware\esp32p4-nsh\nuttx.bin"
$Port = "COM7"
$IdfPython = "C:\Users\flash\.espressif\python_env\idf6.0_py3.13_env\Scripts\python.exe"

if (-not (Test-Path $Fw)) {
    Write-Error "Missing $Fw. Build in WSL first, then copy firmware."
}

# Apache NuttX Config.mk: ESP32-P4 simple-boot app offset is 0x2000 (ROM).
Write-Host "Flashing $Fw -> $Port (ESP32-P4 simple-boot @ 0x2000)"
& $IdfPython -m esptool --chip esp32p4 -p $Port -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x2000 $Fw
if ($LASTEXITCODE -ne 0) {
    Write-Host "If connect failed: hold BOOT, tap RST, retry."
    exit $LASTEXITCODE
}
Write-Host "Flash done. Open serial monitor on $Port 115200, look for nsh>."
