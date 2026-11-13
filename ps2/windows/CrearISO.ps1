# Abe's Exoddus PS2 - ISO builder for Windows (no extra software needed).
# Finds your Steam copy of Oddworld: Abe's Exoddus and builds a PS2 ISO with it and EXODDUS.ELF.
# Usage: double-click CrearISO.bat, or:
#   powershell -ExecutionPolicy Bypass -File CrearISO.ps1 [-Game <folder>] [-Extra EN=<folder>,FR=<folder>] [-Out <file.iso>]
param(
    [string]$Game = "",
    [string[]]$Extra = @(),
    [string]$Out = "",
    [string]$Elf = ""
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Elf) { $Elf = Join-Path $here 'EXODDUS.ELF' }
if (-not $Out) { $Out = Join-Path $here 'SLUS_999.98.ABE_EXODDUS_PS2.iso' }
if (-not (Test-Path $Elf)) { throw "EXODDUS.ELF not found next to this script ($Elf)." }

$langCodes = @{ english = 'EN'; spanish = 'ES'; french = 'FR'; german = 'DE'; italian = 'IT' }

function Find-SteamGame {
    $roots = @()
    foreach ($k in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
        try {
            $p = (Get-ItemProperty $k -ErrorAction Stop)
            if ($p.SteamPath) { $roots += $p.SteamPath }
            if ($p.InstallPath) { $roots += $p.InstallPath }
        } catch {}
    }
    $libs = @()
    foreach ($r in ($roots | Select-Object -Unique)) {
        $libs += $r
        $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) { $libs += ($m.Groups[1].Value -replace '\\\\', '\') }
        }
    }
    foreach ($l in ($libs | Select-Object -Unique)) {
        $manifest = Join-Path $l 'steamapps\appmanifest_15710.acf'
        $dir = Join-Path $l 'steamapps\common\Oddworld Abes Exoddus'
        if ((Test-Path $manifest) -and (Test-Path $dir)) {
            $lang = 'english'
            $mm = [regex]::Match((Get-Content $manifest -Raw), '"language"\s+"([^"]+)"')
            if ($mm.Success) { $lang = $mm.Groups[1].Value }
            return @{ Dir = $dir; Lang = $lang }
        }
    }
    return $null
}

$sets = [ordered]@{}
if ($Game) {
    $sets['EN'] = $Game
} else {
    $found = Find-SteamGame
    if (-not $found) { throw "Oddworld: Abe's Exoddus (Steam) not found. Run with -Game <game folder>." }
    $code = $langCodes[$found.Lang]
    if (-not $code) { $code = 'EN' }
    $sets[$code] = $found.Dir
    Write-Host "Steam copy: $($found.Dir) (language: $($found.Lang) -> DATA\$code)"
}
foreach ($e in ($Extra | ForEach-Object { $_ -split ',' } | Where-Object { $_ })) {
    $kv = $e -split '=', 2
    $sets[$kv[0].ToUpper()] = $kv[1]
}

Add-Type -TypeDefinition (Get-Content (Join-Path $here 'Ps2Iso.cs') -Raw) -Language CSharp

$builder = New-Object Ps2Iso
$builder.AddFile('', 'SYSTEM.CNF', [Text.Encoding]::ASCII.GetBytes("BOOT2 = cdrom0:\SLUS_999.98;1`r`nVER = 1.00`r`nVMODE = NTSC`r`n"))
$builder.AddFilePath('', 'SLUS_999.98', (Resolve-Path $Elf).Path)
foreach ($code in $sets.Keys) {
    $src = $sets[$code]
    $files = Get-ChildItem -LiteralPath $src -File | Where-Object { $_.Extension -match '^\.(lvl|ddv)$' -or $_.Name -ieq 'sounds.dat' }
    if (-not ($files | Where-Object { $_.Name -ieq 'st.lvl' })) { throw "$src does not look like the Abe's Exoddus folder (st.lvl missing)." }
    foreach ($f in $files) { $builder.AddFilePath("DATA/$code", $f.Name.ToUpper(), $f.FullName) }
    Write-Host "DATA\$code : $($files.Count) files"
}
Write-Host "Writing $Out ..."
$builder.Write($Out)
Write-Host ("Done: {0:N0} MB" -f ((Get-Item $Out).Length / 1MB))
