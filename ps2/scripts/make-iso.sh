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

# Como un disco de PS2 de verdad: SYSTEM.CNF y el ejecutable al principio, solo ISO 9660.
# En un DVD, la BIOS de la PS2 no lee la tabla de rutas donde dice el descriptor del volumen:
# la busca en el sector 257, donde la pone el CD/DVD Generator de Sony. genisoimage la deja en
# otro sitio y la BIOS no encontraba el ejecutable ("open fail"). Se reserva ese sector con
# PAD.BIN (el primer archivo del disco) y luego se copia ahí la tabla y se apunta el descriptor.
head -c $((300*2048)) /dev/zero > "$STAGE/PAD.BIN"
printf '%s 4000\n%s 3000\n%s 2000\n' "$STAGE/PAD.BIN" "$STAGE/SYSTEM.CNF" "$STAGE/$ID" > /root/iso_sort.txt
genisoimage -quiet -iso-level 1 -cache-inodes -sort /root/iso_sort.txt -V ABE_EXODDUS -sysid PLAYSTATION -A PLAYSTATION -o "$ISO" "$STAGE"
python3 - "$ISO" <<'EOF'
import struct, sys
f = open(sys.argv[1], "r+b")
f.seek(16 * 2048); pvd = bytearray(f.read(2048))
ptsize, ptl = struct.unpack_from("<I", pvd, 132)[0], struct.unpack_from("<I", pvd, 140)[0]
root = struct.unpack_from("<I", pvd, 158)[0]
f.seek(root * 2048); d = f.read(2048); i = 0; pad = None
while i < len(d) and d[i]:
    n = d[i + 32]
    if d[i + 33:i + 33 + n].startswith(b"PAD.BIN"):
        pad = (struct.unpack_from("<I", d, i + 2)[0], struct.unpack_from("<I", d, i + 10)[0])
    i += d[i]
assert pad and ptsize <= 2048 and pad[0] <= 257 and (pad[0] * 2048 + pad[1]) >= 258 * 2048, (pad, ptsize)
f.seek(ptl * 2048); table = f.read(2048)
f.seek(257 * 2048); f.write(table)
struct.pack_into("<I", pvd, 140, 257)
f.seek(16 * 2048); f.write(pvd)
print("tabla de rutas: sector %d -> 257 (PAD.BIN en %d)" % (ptl, pad[0]))
EOF
rm -rf "$STAGE"
ls -la "$ISO"
