#!/bin/bash
# Muestra qué drivers del IOP carga init_ps2_filesystem_driver (símbolos que usa su objeto).
. /etc/profile.d/ps2dev.sh
lib=/usr/local/ps2dev/ps2sdk/ports/lib/libps2_drivers.a
mips64r5900el-ps2-elf-nm -A "$lib" 2>/dev/null | grep -E 'init_ps2_filesystem_driver|init_only_boot' | head
obj=$(mips64r5900el-ps2-elf-nm -A "$lib" 2>/dev/null | grep ' T init_ps2_filesystem_driver' | cut -d: -f2)
echo "== objeto: $obj"
mips64r5900el-ps2-elf-nm -A "$lib" 2>/dev/null | grep "^$lib:$obj:" | grep -E ' [TUD] '
