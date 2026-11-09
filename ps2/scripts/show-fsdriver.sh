#!/bin/bash
d=/usr/local/ps2dev/ps2sdk/ports/include
for h in ps2_filesystem_driver.h ps2_usb_driver.h ps2_bdm_driver.h; do echo "== $h"; grep -v '^\s*$\|^ \*\|^/\*' "$d/$h" | head -30; done
