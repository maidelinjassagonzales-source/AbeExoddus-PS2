# Genera la portada (ART) y la ficha (CFG) de OPL para SLUS_999.98. Diseño propio, solo texto y degradados.
Add-Type -AssemblyName System.Drawing
$dst = Join-Path $PSScriptRoot '..\out\OPL'
New-Item -ItemType Directory -Force (Join-Path $dst 'ART'), (Join-Path $dst 'CFG') | Out-Null
$id = 'SLUS_999.98'

function New-Img($w, $h, [scriptblock]$draw, $file) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
    & $draw $g $w $h
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
}
function Text($g, $s, $font, $color, $x, $y, $w, $h) {
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment = 'Center'; $sf.LineAlignment = 'Center'
    $r = New-Object System.Drawing.RectangleF $x, $y, $w, $h
    $shadow = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(200, 0, 0, 0))
    $g.DrawString($s, $font, $shadow, (New-Object System.Drawing.RectangleF ($x + 2), ($y + 2), $w, $h), $sf)
    $g.DrawString($s, $font, (New-Object System.Drawing.SolidBrush $color), $r, $sf)
}
$gold  = [System.Drawing.Color]::FromArgb(255, 235, 190, 80)
$green = [System.Drawing.Color]::FromArgb(255, 120, 220, 140)
$white = [System.Drawing.Color]::White

$cover = {
    param($g, $w, $h)
    $bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Point 0, 0), (New-Object System.Drawing.Point 0, $h), ([System.Drawing.Color]::FromArgb(255, 10, 40, 25)), ([System.Drawing.Color]::FromArgb(255, 70, 15, 10))
    $g.FillRectangle($bg, 0, 0, $w, $h)
    # Luna y siluetas de colinas
    $g.FillEllipse((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(90, 230, 230, 200))), [int]($w*0.55), [int]($h*0.08), [int]($w*0.35), [int]($w*0.35))
    $hill = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 15, 10, 8))
    $pts = @(); for ($x = 0; $x -le $w; $x += [int]($w/14)) { $pts += New-Object System.Drawing.Point $x, ([int]($h*0.78 + [Math]::Sin($x/($w/6.0))*$h*0.03)) }
    $pts += New-Object System.Drawing.Point $w, $h; $pts += New-Object System.Drawing.Point 0, $h
    $g.FillPolygon($hill, [System.Drawing.Point[]]$pts)
    # Banda superior "PS2"
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 20, 20, 30))), 0, 0, $w, [int]($h*0.09))
    Text $g 'PlayStation 2  -  PORT' (New-Object System.Drawing.Font 'Arial', ([float]($h*0.035)), ([System.Drawing.FontStyle]::Bold)) $white 0 0 $w ([int]($h*0.09))
    Text $g "ABE'S" (New-Object System.Drawing.Font 'Georgia', ([float]($h*0.075)), ([System.Drawing.FontStyle]::Bold)) $gold 0 ([int]($h*0.24)) $w ([int]($h*0.1))
    Text $g 'EXODDUS' (New-Object System.Drawing.Font 'Georgia', ([float]($h*0.08)), ([System.Drawing.FontStyle]::Bold)) $gold 0 ([int]($h*0.33)) $w ([int]($h*0.13))
    Text $g "60 FPS  -  2 JUGADORES" (New-Object System.Drawing.Font 'Arial', ([float]($h*0.032)), ([System.Drawing.FontStyle]::Bold)) $green 0 ([int]($h*0.52)) $w ([int]($h*0.07))
    Text $g "Port por`nAroy24 & William_piraton" (New-Object System.Drawing.Font 'Arial', ([float]($h*0.03)), ([System.Drawing.FontStyle]::Bold)) $white 0 ([int]($h*0.84)) $w ([int]($h*0.14))
}
New-Img 140 200 $cover (Join-Path $dst "ART\${id}_COV.png")
New-Img 64 64 { param($g,$w,$h)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 10, 40, 25))
    Text $g "ABE`nPS2" (New-Object System.Drawing.Font 'Georgia', 15, ([System.Drawing.FontStyle]::Bold)) $gold 0 0 $w $h } (Join-Path $dst "ART\${id}_ICO.png")

$cfg = @"
CfgVersion=8
`$ConfigSource=1
Title=Abe's Exoddus (Port PS2)
Description=Port no oficial para PlayStation 2 de Oddworld: Abe's Exoddus, basado en el proyecto R.E.L.I.V.E. Novedades de la version PS2: 60 FPS, modo cooperativo para 2 jugadores con dos Abes (el jugador 2 pulsa START en el mando 2 para unirse y tambien puede poseer criaturas), colores corregidos y partidas guardadas en la tarjeta de memoria. Port creado por Aroy24 y William_piraton.
Developer=Aroy24 & William_piraton (port) / Oddworld Inhabitants
Release=2026
Genre=Plataformas / Puzles
Players=players/2
Parental=parental/t
Vmode=vmode/ntsc
Scan=scan/480
Aspect=aspect/s
"@
[IO.File]::WriteAllText((Join-Path $dst "CFG\$id.cfg"), $cfg.Replace("`r`n", "`n"), (New-Object System.Text.UTF8Encoding $false))
Get-ChildItem $dst -Recurse -File | Select-Object FullName, Length
