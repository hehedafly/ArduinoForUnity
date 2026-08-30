# deploy3.ps1 - write a local file to pyboard via chunked-hex REPL, waiting for the prompt
# usage: pwsh -File deploy3.ps1 -File <local> -Remote /flash/<name>
param(
    [string]$Port = "COM7",
    [string]$File,
    [string]$Remote,
    [int]$ChunkHex = 220
)
$ErrorActionPreference = 'Stop'

function ReadAvailable {
    $raw = New-Object System.Collections.Generic.List[byte]
    $buf = New-Object byte[] 2048
    $avail = $sp.BytesToRead
    while ($avail -gt 0) {
        $n = $sp.Read($buf, 0, [Math]::Min($buf.Length, $avail))
        for ($i = 0; $i -lt $n; $i++) { $raw.Add($buf[$i]) }
        $avail = $sp.BytesToRead
    }
    return $raw.ToArray()
}

function Send-Line([string]$line) {
    $b = [System.Text.Encoding]::ASCII.GetBytes($line + "`r")
    $sp.Write($b, 0, $b.Length)
    # wait for a prompt (">>> " or "... ") with timeout
    $deadline = (Get-Date).AddSeconds(3)
    $acc = New-Object System.Collections.Generic.List[byte]
    $buf = New-Object byte[] 1024
    while ((Get-Date) -lt $deadline) {
        $avail = $sp.BytesToRead
        if ($avail -gt 0) {
            $n = $sp.Read($buf, 0, [Math]::Min($buf.Length, $avail))
            for ($i = 0; $i -lt $n; $i++) { $acc.Add($buf[$i]) }
        }
        $s = [System.Text.Encoding]::ASCII.GetString($acc.ToArray())
        if ($s -match '>>>\s*$') { break }
        Start-Sleep -Milliseconds 10
    }
}

$content = [System.IO.File]::ReadAllText($File)
$content = $content -replace "`r`n", "`n"
$content = [System.Text.Encoding]::ASCII.GetString([System.Text.Encoding]::ASCII.GetBytes($content))
$rawBytes = [System.Text.Encoding]::ASCII.GetBytes($content)
$hex = -join ($rawBytes | ForEach-Object { $_.ToString('x2') })

$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One))
$sp.ReadTimeout = 1500
$sp.WriteTimeout = 2000
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.Open()

try {
    $sp.Write([byte[]](0x03), 0, 1)
    Start-Sleep -Milliseconds 300
    $null = ReadAvailable
    $sp.Write([byte[]](0x0D), 0, 1)
    Start-Sleep -Milliseconds 200
    $null = ReadAvailable

    Send-Line "_s = ''"
    $n = [Math]::Ceiling($hex.Length / $ChunkHex)
    for ($i = 0; $i -lt $hex.Length; $i += $ChunkHex) {
        $chunk = $hex.Substring($i, [Math]::Min($ChunkHex, $hex.Length - $i))
        Send-Line ("_s += '" + $chunk + "'")
    }
    Send-Line ("f=open('$Remote','wb'); f.write(ubinascii.unhexlify(_s)); f.close()")
    # verify
    Send-Line ("print('VERIFY ' + repr('$Remote') + ' ' + str(os.stat('$Remote')[6]))")
    Start-Sleep -Milliseconds 300
    $out = ReadAvailable
    $text = [System.Text.Encoding]::ASCII.GetString($out)
    if ($text -match 'VERIFY\s+[^\r\n]*') {
        foreach ($m in [regex]::Matches($text, 'VERIFY\s+[^\r\n]*')) { Write-Output $m.Value }
    } else {
        Write-Output ("tail: " + $text.Substring([Math]::Max(0, $text.Length - 200)))
    }
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
}
