param(
    [Parameter(Mandatory=$true)][string]$Port,
    [Parameter(Mandatory=$true)][string]$Command
)
$ErrorActionPreference = 'Stop'
$serial = [IO.Ports.SerialPort]::new($Port, 115200, [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
$serial.Handshake = [IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 1500
$serial.WriteTimeout = 1500
$serial.NewLine = "`n"
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.WriteLine($Command.TrimEnd([char[]]"`r`n"))
    Write-Output ($serial.ReadLine().TrimEnd([char[]]"`r"))
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
