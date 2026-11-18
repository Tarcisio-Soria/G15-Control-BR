# Copia os executaveis compilados (bin\) para dist\ e grava as somas SHA-256.
# Rode depois do build.ps1, antes de commitar uma versao nova.
$root = Split-Path -Parent $PSScriptRoot
$bin  = Join-Path $root 'bin'
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null

$files = 'g15gui.exe', 'g15ctl.exe'
foreach ($f in $files) {
    $src = Join-Path $bin $f
    if (-not (Test-Path $src)) { throw "nao encontrei $src (rode build.ps1 antes)" }
    Copy-Item $src (Join-Path $dist $f) -Force
}

$ver = (Select-String -Path (Join-Path $root 'src\Version.h') -Pattern 'G15_VERSION\s+"([^"]+)"').Matches[0].Groups[1].Value
$lines = @("# G15 Control BR v$ver - SHA-256")
foreach ($f in $files) {
    $h = (Get-FileHash (Join-Path $dist $f) -Algorithm SHA256).Hash.ToLower()
    $lines += "$h  $f"
}
[IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS.txt'), (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))
Write-Host "dist\ atualizado para a versao $ver"
$lines | ForEach-Object { Write-Host $_ }
