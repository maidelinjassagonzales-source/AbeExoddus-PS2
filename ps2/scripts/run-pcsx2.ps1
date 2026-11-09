# Lanza EXODDUS.ELF en PCSX2 con los datos del juego de Steam y muestra el log de la consola EE.
param([int]$Seconds = 30, [switch]$Launch)

$ErrorActionPreference = 'Stop'
$root    = Split-Path $PSScriptRoot -Parent
$elf     = Join-Path $root 'out\EXODDUS.ELF'
$game    = 'C:\Program Files (x86)\Steam\steamapps\common\Oddworld Abes Exoddus'
$pcsx2   = 'C:\Program Files\PCSX2\pcsx2-qt.exe'
$docs    = [Environment]::GetFolderPath('MyDocuments')
$ini     = Join-Path $docs 'PCSX2\inis\PCSX2.ini'
$logFile = Join-Path $docs 'PCSX2\logs\emulog.txt'

# Copia de seguridad del ini la primera vez
if (-not (Test-Path "$ini.antes-ps2exoddus")) { Copy-Item $ini "$ini.antes-ps2exoddus" }

$text = Get-Content $ini -Raw
$text = $text -replace '(?m)^HostFs = false', 'HostFs = true'
$text = $text -replace '(?m)^EnableEEConsole = false', 'EnableEEConsole = true'
if ($text -notmatch '(?m)^EnableIOPConsole') { $text = $text -replace '(?m)^EnableEEConsole = true', "EnableEEConsole = true`r`nEnableIOPConsole = true" }
$text = $text -replace '(?m)^EnableIOPConsole = false', 'EnableIOPConsole = true'
Set-Content -Path $ini -Value $text -Encoding utf8 -NoNewline

# El ELF va junto a los .lvl: host: apunta a la carpeta del ELF
$target = Join-Path $game 'EXODDUS_PS2.ELF'
Copy-Item $elf $target -Force
Copy-Item (Join-Path $PSScriptRoot 'ps2input.txt') (Join-Path $game 'ps2input.txt') -Force
Remove-Item (Join-Path $game 'ps2shot_*.bmp') -ErrorAction SilentlyContinue

Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
# Minimizado: las pruebas corren en segundo plano sin quitarle la pantalla a nadie
$p = Start-Process -FilePath $pcsx2 -ArgumentList @('-fastboot', '-elf', "`"$target`"") -PassThru -WindowStyle Minimized
if ($Launch) { "PCSX2 lanzado (PID $($p.Id)), se deja abierto"; return }
Start-Sleep -Seconds $Seconds

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
# Pasa las capturas que hizo el juego a out\ como PNG
Add-Type -AssemblyName System.Drawing
Remove-Item (Join-Path $root 'out\ps2shot_*.png') -ErrorAction SilentlyContinue
Get-ChildItem $game -Filter 'ps2shot_*.bmp' -ErrorAction SilentlyContinue | ForEach-Object {
  # Una captura a medio escribir (PCSX2 cerrado mientras guardaba) no se puede abrir: se ignora
  try {
    $img = [System.Drawing.Image]::FromFile($_.FullName)
    $img.Save((Join-Path $root ('out\' + $_.BaseName + '.png')), [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose()
  } catch { }
  Remove-Item $_.FullName
}
# Solo lo que imprime el juego desde main() hasta que vuelve a la BIOS
$all = Get-Content $logFile
$i = ($all | Select-String -SimpleMatch '[relive-ps2] main reached' | Select-Object -Last 1).LineNumber
if (-not $i) { $all | Select-Object -Last 40; return }
$end = $all.Count
$osd = $all[$i..($all.Count-1)] | Select-String -SimpleMatch 'rom0:OSDSYS' | Select-Object -First 1
if ($osd) { $end = $i + $osd.LineNumber; "*** el juego salio a la BIOS ***" }
$all[($i-1)..($end-1)] | Where-Object { $_ -notmatch 'open (name|fd)|InitJoyStick|# Init|# Restart|UpdateVSync' } | Select-Object -Last 60
