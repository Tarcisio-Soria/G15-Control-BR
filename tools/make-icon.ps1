# G15 Control BR - tools\make-icon.ps1
# Gera res\g15.ico (icone do app: janela, bandeja e executavel).
# Desenha por codigo (System.Drawing), entao nao depende de arquivo de imagem externo.
# Uso: powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1 [-Preview caminho.png]
param([string]$Preview)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-IconBitmap([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.PixelOffsetMode   = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)

    # fundo: quadrado arredondado escuro
    $r = [single]($s * 0.22); $d = $r * 2; $m = [single]($s * 0.03); $w = $s - 2 * $m
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($m, $m, $d, $d, 180, 90)
    $path.AddArc($m + $w - $d, $m, $d, $d, 270, 90)
    $path.AddArc($m + $w - $d, $m + $w - $d, $d, $d, 0, 90)
    $path.AddArc($m, $m + $w - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $rect = New-Object System.Drawing.RectangleF(0, 0, $s, $s)
    $bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect,
        [System.Drawing.Color]::FromArgb(255, 30, 40, 58), [System.Drawing.Color]::FromArgb(255, 11, 15, 22), 90.0)
    $g.FillPath($bg, $path)

    # anel de medidor (270 graus), ciano -> lembra o gauge de temperatura/ventoinha
    $pw = [single][Math]::Max(1.6, $s * 0.085)
    $inset = [single]($s * 0.17)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 46, 196, 240), $pw)
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $g.DrawArc($pen, $inset, $inset, $s - 2 * $inset, $s - 2 * $inset, 135, 270)
    # trecho "quente" no fim do arco
    $pen2 = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 245, 96, 96), $pw)
    $pen2.StartCap = 'Round'; $pen2.EndCap = 'Round'
    $g.DrawArc($pen2, $inset, $inset, $s - 2 * $inset, $s - 2 * $inset, 350, 55)

    # letra G no centro
    $font = New-Object System.Drawing.Font('Segoe UI', [single]($s * 0.36), [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $trect = New-Object System.Drawing.RectangleF(0, [single]($s * -0.02), $s, $s)
    $g.DrawString('G', $font, [System.Drawing.Brushes]::White, $trect, $fmt)

    $g.Dispose()
    return $bmp
}

# Entrada BMP (DIB 32 bits, de baixo para cima, + mascara AND zerada)
function Get-DibBytes([System.Drawing.Bitmap]$bmp) {
    $s = $bmp.Width
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    $bw.Write([int]40); $bw.Write([int]$s); $bw.Write([int]($s * 2))
    $bw.Write([int16]1); $bw.Write([int16]32); $bw.Write([int]0)
    $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
    for ($y = $s - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $s; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A)
        }
    }
    $rowBytes = [int]([Math]::Ceiling($s / 32.0) * 4)
    $bw.Write((New-Object byte[] ($rowBytes * $s)))
    $bw.Flush()
    return ,$ms.ToArray()
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$images = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    if ($s -eq 256) {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bytes = $ms.ToArray()
        if ($Preview) { $bmp.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png) }
    } else {
        $bytes = Get-DibBytes $bmp
    }
    $images += ,@($s, $bytes)
    $bmp.Dispose()
}

$root = Split-Path $PSScriptRoot -Parent
$res = Join-Path $root 'res'
New-Item -ItemType Directory -Force -Path $res | Out-Null
$out = Join-Path $res 'g15.ico'

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([int16]0); $bw.Write([int16]1); $bw.Write([int16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($im in $images) {
    $s = $im[0]; $len = $im[1].Length
    $dim = [byte]$(if ($s -ge 256) { 0 } else { $s })
    $bw.Write($dim); $bw.Write($dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([int16]1); $bw.Write([int16]32); $bw.Write([int]$len); $bw.Write([int]$offset)
    $offset += $len
}
foreach ($im in $images) { $bw.Write([byte[]]$im[1]) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($out, $ms.ToArray())
Write-Host "OK: $out ($($ms.Length) bytes, tamanhos: $($sizes -join ', '))"
