#!/bin/bash
f=/usr/local/ps2dev/ps2sdk/common/include/ee_cop0_defs.h
grep -n -i 'level2\|exc2\|debug\|EXC_\|EXCODE' "$f" | head -40
grep -rn -i 'level2\|LEVEL_2' /usr/local/ps2dev/ps2sdk/ee/include/ee_debug.h /usr/local/ps2dev/ps2sdk/common/include/ps2_debug.h | head
