# Sends an .nds to DSi Link over Wi-Fi. DSi Link saves it to Homebrew/Dev/Received on the card;
# start it from Pico Launcher. -Run also asks DSi Link to boot it through Pico Loader, which is
# experimental and does not work yet. Wire format: source/link_protocol.h.
param(
    [Parameter(Mandatory=$true)][string]$Address,
    [Parameter(Mandatory=$true)][string]$File,
    [string]$Name,
    [int]$Port = 17494,
    [switch]$Run,
    [switch]$NoRun,   # accepted for older commands; saving only is the default
    [int]$TimeoutSeconds = 15
)
$ErrorActionPreference = 'Stop'

if (-not ('DsiLinkCrc' -as [type])) {
    Add-Type -TypeDefinition @'
public static class DsiLinkCrc {
    static readonly uint[] Table = Build();
    static uint[] Build() { var t = new uint[256]; for (uint i = 0; i < 256; i++) { uint c = i; for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } return t; }
    public static uint Compute(byte[] data) { uint c = 0xFFFFFFFFu; foreach (byte b in data) c = Table[(c ^ b) & 0xFF] ^ (c >> 8); return ~c; }
}
'@
}

$path = (Resolve-Path $File).Path
if (-not $Name) { $Name = Split-Path -Leaf $path }
if ($Name -notmatch '^[A-Za-z0-9_\-][A-Za-z0-9 ._\-]{0,58}\.[Nn][Dd][Ss]$' -or $Name.Contains('..')) {
    throw "Name '$Name' must be a plain .nds file name (letters, digits, space, dot, dash, underscore; up to 63 characters)."
}
$data = [IO.File]::ReadAllBytes($path)
if ($data.Length -eq 0 -or $data.Length -gt 32MB) { throw "File size $($data.Length) is outside 1 byte to 32 MiB." }
$crc = [DsiLinkCrc]::Compute($data)
$nameBytes = [Text.Encoding]::ASCII.GetBytes($Name)

$header = New-Object byte[] 20
[Text.Encoding]::ASCII.GetBytes('DSLK').CopyTo($header, 0)
[BitConverter]::GetBytes([uint32]($(if ($Run -and -not $NoRun) { 1 } else { 0 }))).CopyTo($header, 4)
[BitConverter]::GetBytes([uint32]$data.Length).CopyTo($header, 8)
[BitConverter]::GetBytes([uint32]$crc).CopyTo($header, 12)
[BitConverter]::GetBytes([uint16]$nameBytes.Length).CopyTo($header, 16)

function Read-Reply([IO.Stream]$stream) {
    $bytes = New-Object Collections.Generic.List[byte]
    while ($true) {
        $b = $stream.ReadByte()
        if ($b -lt 0) { break }
        if ($b -eq 10) { break }
        $bytes.Add([byte]$b)
        if ($bytes.Count -gt 200) { break }
    }
    [Text.Encoding]::ASCII.GetString($bytes.ToArray())
}

$client = New-Object Net.Sockets.TcpClient
try {
    $connect = $client.BeginConnect($Address, $Port, $null, $null)
    if (-not $connect.AsyncWaitHandle.WaitOne($TimeoutSeconds * 1000)) { throw "No answer from $Address`:$Port. Open DSi Link on the DSi and check the address it shows." }
    $client.EndConnect($connect)
    $client.ReceiveTimeout = $TimeoutSeconds * 1000
    $client.SendTimeout = $TimeoutSeconds * 1000
    $stream = $client.GetStream()
    $stream.Write($header, 0, $header.Length)
    $stream.Write($nameBytes, 0, $nameBytes.Length)
    $ready = Read-Reply $stream
    if ($ready -ne 'READY') { throw "DSi Link refused the file: $ready" }

    $chunk = 16384
    for ($offset = 0; $offset -lt $data.Length; $offset += $chunk) {
        $count = [Math]::Min($chunk, $data.Length - $offset)
        $stream.Write($data, $offset, $count)
        Write-Progress -Activity "Sending $Name to $Address" -PercentComplete ([int](100 * ($offset + $count) / $data.Length))
    }
    Write-Progress -Activity "Sending $Name to $Address" -Completed
    $result = Read-Reply $stream
    if (-not $result.StartsWith('OK')) { throw "DSi Link reported: $result" }
    Write-Output ("{0}: {1} ({2} bytes, CRC-32 {3:X8})" -f $Address, $result, $data.Length, $crc)
}
finally {
    $client.Close()
}
