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
count=0
while IFS= read -r -d '' f; do
  name=$(basename "$f" | tr '[:lower:]' '[:upper:]')
  cp "$f" "$STAGE/DATA/$name"
  count=$((count+1))
done < <(find "$GAME" -maxdepth 1 -type f \( -iname '*.lvl' -o -iname '*.ddv' -o -iname 'sounds.dat' \) -print0)
echo "archivos de datos: $count"

# Como un disco de PS2 de verdad: SYSTEM.CNF y el ejecutable al principio, ISO 9660 + UDF (DVD).
printf '%s 3000
%s 2000
' "$STAGE/SYSTEM.CNF" "$STAGE/$ID" > /root/iso_sort.txt
genisoimage -quiet -iso-level 1 -udf -sort /root/iso_sort.txt -V ABE_EXODDUS -sysid PLAYSTATION -A PLAYSTATION -o "$ISO" "$STAGE"
rm -rf "$STAGE"
ls -la "$ISO"
