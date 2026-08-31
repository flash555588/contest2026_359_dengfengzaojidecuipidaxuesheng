param(
    [string]$Port = "COM7",
    [int]$Baud = 921600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$image = Join-Path $PSScriptRoot "nuttx-lvgl-unified-ui.bin"
$expected = "54B94CF0AACF95A0E0ADC41214274E3186CB40ED2A85BF30251F773540C92560"

if (-not (Test-Path -LiteralPath $image -PathType Leaf)) {
    throw "Firmware not found: $image"
}

$actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $image).Hash
if ($actual -ne $expected) {
    throw "SHA256 mismatch. Expected $expected, got $actual"
}

& py -m esptool `
    --chip esp32p4 `
    --port $Port `
    --baud $Baud `
    --before default-reset `
    --after hard-reset `
    write-flash `
    --flash-size 16MB `
    --flash-mode dio `
    --flash-freq 80m `
    0x2000 $image

if ($LASTEXITCODE -ne 0) {
    throw "esptool failed with exit code $LASTEXITCODE"
}

Write-Host "Flash and verification completed. UART0 is 115200 baud."
