# Instala la ISO del port en el disco duro de PS2 (hdd0:) con hdl_dump, para cargarla con OPL.
# Necesita permisos de administrador (acceso directo al disco). Escribe el resultado en %TEMP%\hdl_inject.txt
$ErrorActionPreference = 'Continue'
$hdl = (Get-ChildItem (Join-Path $PSScriptRoot '..\herramientas\HDL') -Recurse -Filter 'HDL.EXE' | Select-Object -First 1).FullName
$iso = 'C:\Users\dgvpr\PS2Exoddus-ISO\SLUS_999.98.ABE_EXODDUS_PS2.iso'
$out = Join-Path $env:TEMP 'hdl_inject.txt'

"== query" | Out-File $out -Encoding utf8
& $hdl query 2>&1 | Out-File $out -Append -Encoding utf8

# Seguridad: solo seguimos si hdd0: es un disco formateado para PS2.
$q = Get-Content $out -Raw
if ($q -notmatch 'hdd0:.*formatted Playstation 2 HDD') {
    "ABORTADO: hdd0: no es un disco de PS2" | Out-File $out -Append -Encoding utf8
    exit 1
}

# Reinstalación: borra solo la versión anterior del port (su propia partición), si está.
if ((& $hdl hdl_toc hdd0: 2>&1 | Out-String) -match 'SLUS_999\.98') {
    "== delete" | Out-File $out -Append -Encoding utf8
    & $hdl delete hdd0: "Abe's Exoddus (Port PS2)" 2>&1 | Out-File $out -Append -Encoding utf8
}

"== inject" | Out-File $out -Append -Encoding utf8
& $hdl inject_cd hdd0: "Abe's Exoddus (Port PS2)" $iso SLUS_999.98 '*u4' 2>&1 | Out-File $out -Append -Encoding utf8

"== check" | Out-File $out -Append -Encoding utf8
& $hdl hdl_toc hdd0: 2>&1 | Select-String 'SLUS_999' | Out-File $out -Append -Encoding utf8
& $hdl toc hdd0: 2>&1 | Select-String '\+OPL|__common' | Out-File $out -Append -Encoding utf8
