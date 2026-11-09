#!/bin/bash
# Desensambla alrededor de una dirección del ELF. Uso: disasm-at.sh 0x0013BE50
. /etc/profile.d/ps2dev.sh
A=$(( $1 ))
mips64r5900el-ps2-elf-objdump -d -C --no-show-raw-insn --start-address=$((A-0x40)) --stop-address=$((A+0x20)) /root/build-ps2/Source/relive/relive | tail -n +7
