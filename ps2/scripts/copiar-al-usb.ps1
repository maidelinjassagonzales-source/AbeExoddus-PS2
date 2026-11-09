# Copia OPL y el port de Abe's Exoddus a un USB para jugarlo en una PS2 real.
# Uso:  .\copiar-al-usb.ps1 -Unidad E
#
# El USB debe estar en FAT32 (la PS2 no lee NTFS ni exFAT). El script no formatea nada:
# si el USB no es FAT32 se para y te lo dice.
param([Parameter(Mandatory = $true)][string]$Unidad)

$ErrorActionPreference = 'Stop'
$Unidad = $Unidad.TrimEnd(':', '\')
$raiz   = Split-Path $PSScriptRoot -Parent
$origen = Join-Path $raiz 'USB_PS2'
$juego  = 'C:\Program Files (x86)\Steam\steamapps\common\Oddworld Abes Exoddus'

$vol = Get-Volume -DriveLetter $Unidad
if ($vol.DriveType -ne 'Removable') { throw "La unidad ${Unidad}: no es un USB extraíble. Por seguridad no copio nada." }
if ($vol.FileSystem -ne 'FAT32') { throw "El USB ${Unidad}: está en $($vol.FileSystem). La PS2 solo lee FAT32: formatéalo en FAT32 y vuelve a ejecutar el script." }

# Datos del juego: niveles, vídeos y sonidos (no hace falta el .exe de PC).
$datos = Get-ChildItem $juego -File | Where-Object { $_.Extension -in '.lvl', '.ddv', '.dat' -or $_.Extension -ieq '.LVL' -or $_.Extension -ieq '.DDV' }
$necesario = ($datos | Measure-Object Length -Sum).Sum + (Get-ChildItem $origen -Recurse -File | Measure-Object Length -Sum).Sum
if ($vol.SizeRemaining -lt $necesario) { throw ("No cabe: hacen falta {0:N0} MB y quedan {1:N0} MB libres." -f ($necesario / 1MB), ($vol.SizeRemaining / 1MB)) }

$destino = "${Unidad}:\"
Write-Host "Copiando OPL, el idioma español y el port..."
Copy-Item (Join-Path $origen '*') $destino -Recurse -Force

$carpetaJuego = "${Unidad}:\APPS\EXODDUS"
Write-Host ("Copiando {0} archivos del juego ({1:N0} MB), tarda unos minutos..." -f $datos.Count, (($datos | Measure-Object Length -Sum).Sum / 1MB))
$i = 0
foreach ($f in $datos) {
    $i++
    Write-Progress -Activity 'Copiando datos de Abe''s Exoddus' -Status $f.Name -PercentComplete ($i * 100 / $datos.Count)
    Copy-Item $f.FullName $carpetaJuego -Force
}
Write-Progress -Activity 'Copiando datos de Abe''s Exoddus' -Completed

Write-Host ""
Write-Host "Listo. En el USB:"
Write-Host "  ${Unidad}:\OPNPS2LD.ELF            -> Open PS2 Loader"
Write-Host "  ${Unidad}:\LNG\                    -> OPL en español"
Write-Host "  ${Unidad}:\conf_apps.cfg           -> entrada del port en la lista de aplicaciones de OPL"
Write-Host "  ${Unidad}:\APPS\EXODDUS\EXODDUS.ELF -> el port, junto a los datos del juego"
