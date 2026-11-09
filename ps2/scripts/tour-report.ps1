# Resume una visita por todos los niveles (ps2tour.txt): estado de cada parada, FPS medios y
# problemas (DeleteSema(0), cuelgues, falta de memoria). Uso: tour-report.ps1 [ruta del log]
param([string]$LogPath = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PCSX2\logs\emulog.txt'))

$lines = Get-Content $LogPath
$stops = @()
$current = $null
foreach ($line in $lines) {
    if ($line -match "Tour: going to stop (\d+) '([^']+)'") {
        if ($current) { $stops += $current }
        $current = [pscustomobject]@{ Parada = [int]$Matches[1]; Nivel = $Matches[2]; Estado = '?'; Frames = @(); Problemas = 0 }
    }
    elseif ($current -and $line -match "Tour: stop \d+ '[^']+' (OK|WRONG PLACE)") { $current.Estado = $Matches[1] }
    elseif ($current -and $line -match '\[prof\].* frame=([\d.]+)') { $current.Frames += [double]$Matches[1] }
    elseif ($current -and $line -match '\[sema\] \*\*\*|CRASH|HANG|OUT OF MEMORY') { $current.Problemas++ }
}
if ($current) { $stops += $current }

$stops | ForEach-Object {
    # Las primeras muestras tras el salto incluyen la carga del nivel: se ignoran.
    $f = $_.Frames | Select-Object -Skip 3
    $ms = if ($f) { ($f | Measure-Object -Average).Average } else { 0 }
    [pscustomobject]@{
        Parada    = $_.Parada
        Nivel     = $_.Nivel
        Estado    = $_.Estado
        'ms/frame'= [math]::Round($ms, 1)
        FPS       = if ($ms -gt 0) { [math]::Round(1000 / $ms) } else { '-' }
        Problemas = $_.Problemas
    }
} | Format-Table -AutoSize | Out-String -Width 200

"Finalizada: " + [bool]($lines | Select-String -SimpleMatch 'Tour: finished')
"DeleteSema(0) total: " + ($lines | Select-String -SimpleMatch '[sema] ***').Count
