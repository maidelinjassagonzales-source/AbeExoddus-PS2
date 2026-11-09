# Solo lectura: lista juegos y particiones del disco PS2 (hdd0:). Salida en %TEMP%\hdl_leer.txt
$hdl = (Get-ChildItem (Join-Path $PSScriptRoot '..\herramientas\HDL') -Recurse -Filter 'HDL.EXE' | Select-Object -First 1).FullName
$out = Join-Path $env:TEMP 'hdl_leer.txt'
"== query" | Out-File $out -Encoding utf8
& $hdl query 2>&1 | Out-File $out -Append -Encoding utf8
"== hdl_toc" | Out-File $out -Append -Encoding utf8
& $hdl hdl_toc hdd0: 2>&1 | Out-File $out -Append -Encoding utf8
"== toc" | Out-File $out -Append -Encoding utf8
& $hdl toc hdd0: 2>&1 | Out-File $out -Append -Encoding utf8
"== info" | Out-File $out -Append -Encoding utf8
& $hdl info hdd0: "Abe's Exoddus (Port PS2)" 2>&1 | Out-File $out -Append -Encoding utf8
