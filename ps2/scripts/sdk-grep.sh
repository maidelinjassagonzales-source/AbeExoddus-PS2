#!/bin/bash
# Busca un patrón en las cabeceras del PS2SDK. Uso: sdk-grep.sh <patrón> [subcarpeta]
. /etc/profile.d/ps2dev.sh
grep -rn -E "$1" "$PS2SDK/${2:-ee/include}" 2>/dev/null | head -40
