#!/bin/bash
# Lista símbolos débiles sin definir (acaban siendo NULL) en el ELF para PS2 real.
. /etc/profile.d/ps2dev.sh
mips64r5900el-ps2-elf-nm /root/build-ps2-usb/Source/relive/relive | awk '$1=="w" || $1=="v" || $1=="U" {print}' | head -40
echo "---- cdfs / boot device"
mips64r5900el-ps2-elf-nm /root/build-ps2-usb/Source/relive/relive | grep -i -E 'cdfs|getBootDevice|init_ps2_filesystem|init_cdfs|init_memcard|init_hdd|init_dev9|init_mx4sio' | head -20
