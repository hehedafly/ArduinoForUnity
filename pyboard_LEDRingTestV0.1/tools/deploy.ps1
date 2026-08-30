# deploy.ps1 — 通过 REPL 粘贴模式把本地文件写到 pyboard /flash（开发期工具）
# 用法: pwsh -File deploy.ps1 -File <本地文件> -Remote /flash/<目标名>
param(
    [string]$Port = "COM7",
    [string]$File,
    [string]$Remote,
    [int]$WaitMs = 4000
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
    $content = [System.IO.File]::ReadAllText($File)
    $content = $content -replace "`r`n", "`n"   # 统一 LF
    # 非 ASCII（中文注释/文档串）在 REPL 粘贴模式下会损坏传输，转成 ASCII 再发（功能不受影响）
    $content = [System.Text.Encoding]::ASCII.GetString([System.Text.Encoding]::ASCII.GetBytes($content))

    # 粘贴脚本：用 r'''...''' 原始三引号嵌入内容，保 backslash 字面量（文件内无 '''，安全）
    $script = "f = open('$Remote', 'w')`n" +
              "f.write(r'''" + $content + "''')`n" +
              "f.close()`n" +
              "print('WROTE ' + repr('$Remote') + ' len=' + str(len(open('$Remote').read())))`n"

    # Ctrl-C 清场
    $sp.Write([byte[]](0x03), 0, 1)
    Start-Sleep -Milliseconds 250
    $null = ReadBytes 300

    # 进入粘贴模式
    $sp.Write([byte[]](0x05), 0, 1)
    Start-Sleep -Milliseconds 500
    $null = ReadBytes 300

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($script)
    # 分块发送 + 短暂延迟，避免板子 REPL 输入缓冲溢出丢字节
    $chunkSize = 256
    for ($i = 0; $i -lt $bytes.Length; $i += $chunkSize) {
        $len = [Math]::Min($chunkSize, $bytes.Length - $i)
        $sp.Write($bytes, $i, $len)
        Start-Sleep -Milliseconds 15
    }
    Start-Sleep -Milliseconds 300
    $sp.Write([byte[]](0x04), 0, 1)   # Ctrl-D 执行

    $out = ReadBytes $WaitMs
    Write-Output ([System.Text.Encoding]::UTF8.GetString($out))
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
}
