#!/bin/bash
# Traduce direcciones del ELF de PS2 a función y línea. Uso: addr2line.sh 0x00123456 [...]
. /etc/profile.d/ps2dev.sh
mips64r5900el-ps2-elf-addr2line -f -C -p -e /root/build-ps2/Source/relive/relive "$@" | sed 's|/mnt/c/Users/dgvpr/OneDrive/Escritorio/PS2Exoddus/alive_reversing/||'
