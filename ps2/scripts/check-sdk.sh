#!/bin/bash
. /etc/profile.d/ps2dev.sh
echo "PS2SDK=$PS2SDK"
mips64r5900el-ps2-elf-g++ --version | head -1
echo "--- ports/lib"; ls "$PS2SDK/ports/lib"
echo "--- ports/lib/cmake"; ls "$PS2SDK/ports/lib/cmake" 2>/dev/null
echo "--- SDL"; ls -d "$PS2SDK"/ports/include/SDL* 2>/dev/null
echo "--- samples cmake toolchain"; find "$PS2DEV" -name "*.cmake" -path "*toolchain*" 2>/dev/null | head
