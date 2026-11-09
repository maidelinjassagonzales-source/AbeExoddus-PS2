#!/bin/bash
# Resume los errores de enlazado del último build
L=/root/build.log
grep -o "undefined reference to \`[^']*'" "$L" | sort | uniq -c | sort -rn | head -50
echo "total undefined: $(grep -c 'undefined reference' "$L")"
grep -v 'undefined reference' "$L" | grep -iE 'error|overflow|multiple def|relocation' | head -20
