#!/bin/bash
# Comprueba si malloc/newlib tienen bloqueos entre hilos en el toolchain de PS2
. /etc/profile.d/ps2dev.sh
for lib in "$PS2SDK/ee/lib/libcglue.a" "$PS2DEV/ee/mips64r5900el-ps2-elf/lib/libc.a" "$PS2SDK/ee/lib/libpthreadglue.a"; do
  echo "== $lib"
  mips64r5900el-ps2-elf-nm -A "$lib" 2>/dev/null | grep -E ' [TW] (__malloc_lock|__malloc_unlock|__retarget_lock_acquire_recursive|__retarget_lock_init_recursive)$' | head
done
echo "== relive ELF"
mips64r5900el-ps2-elf-nm /root/build-ps2/Source/relive/relive | grep -E ' (__malloc_lock|__retarget_lock_acquire_recursive)$'
mips64r5900el-ps2-elf-objdump -d /root/build-ps2/Source/relive/relive --no-show-raw-insn | awk '/<__retarget_lock_acquire_recursive>:/{f=1} f{print} f&&/jr\s+ra/{exit}' | head -30
