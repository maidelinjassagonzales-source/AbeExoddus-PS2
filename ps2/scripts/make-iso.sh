#!/bin/bash
# Crea una ISO de PS2 del port de Abe's Exoddus, para instalarla en el disco duro con
# HDL Batch Installer (o ponerla en un USB/SMB) y cargarla con OPL como cualquier juego.
#   SYSTEM.CNF  -> arranca cdrom0:\SLUS_999.98;1
#   SLUS_999.98 -> el port (versión para PS2 real: out/usb/EXODDUS.ELF)
#   DATA\*.LVL, *.DDV, SOUNDS.DAT -> datos del juego de Steam (fuera de la raíz: la PS2 no lee
#   bien una raíz con ~150 entradas)
set -e
GAME="/mnt/c/Program Files (x86)/Steam/steamapps/common/Oddworld Abes Exoddus"
ELF=${ELF:-/mnt/c/Users/dgvpr/OneDrive/Escritorio/PS2Exoddus/out/usb/EXODDUS.ELF}
OUTDIR=/mnt/c/Users/dgvpr/PS2Exoddus-ISO
ID=${ID:-SLUS_999.98}
ISO="$OUTDIR/$ID.ABE_EXODDUS_PS2.iso"
STAGE=/root/iso_stage

rm -rf "$STAGE" && mkdir -p "$STAGE/DATA" "$OUTDIR"

printf 'BOOT2 = cdrom0:\\%s;1\r\nVER = 1.00\r\nVMODE = %s\r\n' "$ID" "${VMODE:-NTSC}" > "$STAGE/SYSTEM.CNF"
cp "$ELF" "$STAGE/$ID"

# Datos: nombres en mayúsculas (ISO 9660 nivel 1, todos caben en 8.3)
# Varios idiomas: si existe $LANGS (carpetas EN, ES, FR, DE, IT con los datos de Steam en ese idioma),
# cada una va a DATA\<XX>; el juego elige la del idioma de la consola. Los archivos idénticos entre
# idiomas (músicas, sonidos...) son enlaces duros: genisoimage los guarda una sola vez en el disco.
LANGS=${LANGS:-/mnt/c/Users/dgvpr/PS2Exoddus-idiomas}
copy_set() { # $1 origen, $2 destino
  local n=0
  mkdir -p "$2"
  while IFS= read -r -d '' f; do
    name=$(basename "$f" | tr '[:lower:]' '[:upper:]')
    dup=""
    for other in "$STAGE"/DATA/*/"$name"; do
      [ -f "$other" ] && cmp -s "$f" "$other" && { dup=$other; break; }
    done
    if [ -n "$dup" ]; then ln "$dup" "$2/$name"; else cp "$f" "$2/$name"; fi
    n=$((n+1))
  done < <(find "$1" -maxdepth 1 -type f \( -iname '*.lvl' -o -iname '*.ddv' -o -iname 'sounds.dat' \) -print0)
  echo "$2: $n archivos"
}
langs=0
for L in EN ES FR DE IT; do
  if [ -f "$LANGS/$L/st.lvl" ] || [ -f "$LANGS/$L/ST.LVL" ]; then copy_set "$LANGS/$L" "$STAGE/DATA/$L"; langs=$((langs+1)); fi
done
[ $langs -gt 0 ] || copy_set "$GAME" "$STAGE/DATA"

# Como un disco de PS2 de verdad: SYSTEM.CNF y el ejecutable al principio, ISO 9660 + UDF (DVD).
printf '%s 3000
%s 2000
' "$STAGE/SYSTEM.CNF" "$STAGE/$ID" > /root/iso_sort.txt
genisoimage -quiet -iso-level 1 -cache-inodes -udf -sort /root/iso_sort.txt -V ABE_EXODDUS -sysid PLAYSTATION -A PLAYSTATION -o "$ISO" "$STAGE"
rm -rf "$STAGE"
ls -la "$ISO"
