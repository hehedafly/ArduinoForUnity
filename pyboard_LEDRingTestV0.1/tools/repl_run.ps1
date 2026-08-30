# repl_run.ps1 — 通过 REPL 粘贴模式运行一个本地脚本（开发期工具，不保存到板子）
# 用法: pwsh -File repl_run.ps1 -File <本地脚本路径> [-WaitMs 3000]
param(
    [string]$Port = "COM7",
    [string]$File,
    [int]$WaitMs = 3000
)
$ErrorActionPreference = 'Stop'

function ReadBytes($ms) {
    $deadline = (Get-Date).AddMilliseconds($ms)
    $raw = New-Object System.Collections.Generic.List[byte]
    $buf = New-Object byte[] 4096
    while ((Get-Date) -lt $deadline) {
        $avail = $sp.BytesToRead
        if ($avail -gt 0) {
            $n = $sp.Read($buf, 0, [Math]::Min($buf.Length, $avail))
            for ($i = 0; $i -lt $n; $i++) { $raw.Add($buf[$i]) }
        }
        Start-Sleep -Milliseconds 15
    }
    return $raw.ToArray()
}

$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One))
$sp.ReadTimeout = 2000
$sp.WriteTimeout = 2000
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.Open()

try {
    # Ctrl-C 清场
    $sp.Write([byte[]](0x03), 0, 1)
    Start-Sleep -Milliseconds 250
    $null = ReadBytes 300

    # 进入粘贴模式
    $sp.Write([byte[]](0x05), 0, 1)
    Start-Sleep -Milliseconds 500
    $null = ReadBytes 300

    $content = [System.IO.File]::ReadAllText($File)
    # 统一换行为 LF
    $content = $content -replace "`r`n", "`n"
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($content)
    $sp.Write($bytes, 0, $bytes.Length)
    Start-Sleep -Milliseconds 250
    # Ctrl-D 结束粘贴并执行
    $sp.Write([byte[]](0x04), 0, 1)

    $out = ReadBytes $WaitMs
    Write-Output ([System.Text.Encoding]::UTF8.GetString($out))
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
}
