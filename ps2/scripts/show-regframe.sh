#!/bin/bash
# Muestra la estructura EE_RegFrame de ee_debug.h (registros guardados en una excepción).
. /etc/profile.d/ps2dev.sh
f=$(find "$PS2SDK" -name ee_debug.h | head -1)
awk '/typedef struct/{buf=""; on=1} on{buf=buf $0 "\n"} /EE_RegFrame;/{print buf; on=0}' "$f"
