#!/bin/bash
# Copia la portada, el icono y la ficha de OPL a la partición +OPL del disco de PS2 (uso: copiar-portada-hdd.sh /dev/sdX)
DEV=${1:?falta el dispositivo}
S=/mnt/c/Users/dgvpr/OneDrive/Escritorio/PS2Exoddus/out/OPL
P=/root/pfsshell/build/pfsshell
rm -rf /root/oplart && mkdir -p /root/oplart && cp "$S"/ART/* "$S"/CFG/* /root/oplart/ && cd /root/oplart || exit 1
printf '%s\n' "device $DEV" "mount +OPL" "mkdir ART" "mkdir CFG" \
  "cd ART" "put SLUS_999.98_COV.png" "put SLUS_999.98_ICO.png" "ls" \
  "cd .." "cd CFG" "put SLUS_999.98.cfg" "ls" "umount" "exit" | "$P" 2>&1
