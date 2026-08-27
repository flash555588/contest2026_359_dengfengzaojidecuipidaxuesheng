param(
    [string]$Port = "COM7",
    [int]$Baud = 921600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$image = Join-Path $PSScriptRoot "nuttx.bin"
$expected = "FDD73E4CEB43BFC7F3CBA030A3A0CA3A0A99784B78A6BDD1121BA59A9D75F62B"

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
    --flash-size 4MB `
    --flash-mode dio `
    --flash-freq 80m `
    0x2000 $image

if ($LASTEXITCODE -ne 0) {
    throw "esptool failed with exit code $LASTEXITCODE"
}

Write-Host "Flash and verification completed. UART0 is 115200 baud."
