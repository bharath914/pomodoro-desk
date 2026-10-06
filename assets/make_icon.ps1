# Regenerates the app icon from code (no external assets):
#   assets/icon.png        1024x1024 master (transparent background)
#   windows/pomodoro.ico   multi-size icon (16-256 px, PNG-compressed frames) embedded in pomodoro.exe
# Run from the repo root:  powershell -NoProfile -File assets/make_icon.ps1
Add-Type -AssemblyName System.Drawing

function New-Canvas([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    return @($bmp, $g)
}

function C([string]$hex, [int]$a = 255) {
    $c = [System.Drawing.ColorTranslator]::FromHtml($hex)
    return [System.Drawing.Color]::FromArgb($a, $c.R, $c.G, $c.B)
}

function Draw-Leaf($g, [float]$cx, [float]$cy, [float]$angleDeg, [float]$len, [float]$wid, $brush) {
    $state = $g.Save()
    $g.TranslateTransform($cx, $cy)
    $g.RotateTransform($angleDeg)
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddBezier(0, 0, ($len * 0.25), (-$wid), ($len * 0.75), (-$wid * 0.7), $len, 0)
    $p.AddBezier($len, 0, ($len * 0.75), ($wid * 0.7), ($len * 0.25), $wid, 0, 0)
    $p.CloseFigure()
    $g.FillPath($brush, $p)
    $p.Dispose()
    $g.Restore($state)
}

# ---- 1024 master ----
$S = 1024
$bmp, $g = New-Canvas $S


# body (slightly squat, wider than tall)
$body = New-Object System.Drawing.Drawing2D.GraphicsPath
$body.AddEllipse(92, 232, 840, 700)
$pg = New-Object System.Drawing.Drawing2D.PathGradientBrush $body
$pg.CenterPoint = New-Object System.Drawing.PointF 400, 480
$pg.CenterColor = C '#FF7A66'
$pg.SurroundColors = @((C '#D92D20'))
$g.FillPath($pg, $body)

# subtle top dip where the stem sits
$dip = New-Object System.Drawing.SolidBrush (C '#B3211A' 90)
$g.FillEllipse($dip, 372, 238, 280, 70)

# glossy highlight
$hi = New-Object System.Drawing.Drawing2D.GraphicsPath
$hi.AddEllipse(190, 330, 260, 150)
$hb = New-Object System.Drawing.Drawing2D.PathGradientBrush $hi
$hb.CenterColor = C '#FFFFFF' 150
$hb.SurroundColors = @((C '#FFFFFF' 0))
$state = $g.Save(); $g.TranslateTransform(320, 405); $g.RotateTransform(-28); $g.TranslateTransform(-320, -405)
$g.FillPath($hb, $hi)
$g.Restore($state)

# calyx (leaves)
$leafBrush = New-Object System.Drawing.SolidBrush (C '#3F9A2B')
$leafDark  = New-Object System.Drawing.SolidBrush (C '#2F7A1F')
$cx = 512; $cy = 292
foreach ($a in 20, 160) { Draw-Leaf $g $cx $cy $a 255 92 $leafDark }
foreach ($a in 52, 128) { Draw-Leaf $g $cx $cy $a 255 92 $leafBrush }
Draw-Leaf $g $cx $cy 90 215 84 $leafBrush
$g.FillEllipse($leafDark, $cx - 38, $cy - 30, 76, 60)

# stem
$stem = New-Object System.Drawing.Drawing2D.GraphicsPath
$stem.AddBezier(496, 300, 490, 240, 500, 190, 540, 128)
$stem.AddLine(540, 128, 592, 150)
$stem.AddBezier(592, 150, 566, 200, 556, 250, 556, 300)
$stem.CloseFigure()
$g.FillPath((New-Object System.Drawing.SolidBrush (C '#5E8F25')), $stem)

$g.Dispose()
$bmp.Save((Join-Path $PSScriptRoot 'icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)

# ---- ICO with PNG-compressed frames ----
$sizes = 256, 128, 64, 48, 32, 24, 16
$frames = @()
foreach ($sz in $sizes) {
    $fb, $fg = New-Canvas $sz
    $fg.DrawImage($bmp, 0, 0, $sz, $sz)
    $fg.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $fb.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += , @($sz, $ms.ToArray())
    $fb.Dispose(); $ms.Dispose()
}
$icoPath = Join-Path $PSScriptRoot '..\windows\pomodoro.ico'
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $dim = if ($f[0] -ge 256) { 0 } else { $f[0] }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$f[1].Length); $bw.Write([uint32]$offset)
    $offset += $f[1].Length
}
foreach ($f in $frames) { $bw.Write($f[1]) }
$bw.Close(); $fs.Close()
$bmp.Dispose()
"wrote assets/icon.png and windows/pomodoro.ico"
