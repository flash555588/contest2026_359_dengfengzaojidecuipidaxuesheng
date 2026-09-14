$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementPublisher, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$publisher = New-Object Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementPublisher
$publisher.Advertisement.LocalName = 'VelaDesk Name Test'
try {
    $publisher.Start()
    Start-Sleep -Seconds 1
    Write-Output ('Publisher status: ' + $publisher.Status)
    if ($publisher.Status -ne 'Started' -and $publisher.Status -ne 'Waiting') {
        throw 'Local adapter cannot publish a BLE test advertisement.'
    }
    for ($advertiseSeconds = 0; $advertiseSeconds -lt 50; $advertiseSeconds++) {
        Start-Sleep -Seconds 1
    }
} finally {
    $publisher.Stop()
    Write-Output ('Publisher stopped: ' + $publisher.Status)
}
