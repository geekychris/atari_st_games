#!/bin/sh
# Screenshots: native resolution vs. full host window, pixel inspection.
. "$(dirname "$0")/lib.sh"
require_app

step "Native screenshot (emulated resolution, coordinates = mouse coordinates)"
f=$(shot screen-native)
set -- $($PNGTOOL size "$f")
info "$f: ${1}x$2"
[ "$1" = 320 ] && [ "$2" = 200 ] || fail "expected 320x200 in ST low res"
ok "native screenshot is ${1}x$2"

step "Full screenshot (host window: zoom, borders, statusbar)"
f=$(shot screen-full full=1)
info "$f: $($PNGTOOL size "$f" | tr ' ' x)"
ok "full screenshot saved"

step "Pixel inspection: the color swatch next to the buttons"
set -- $(layout Clear)
sx=$(( $1 + $3 + 20 )); sy=$(( $2 + $4 / 2 ))
info "swatch pixel at $sx,$sy = RGB $($PNGTOOL pixel "$OUTDIR/screen-native.png" $sx $sy)"
ok "pixel read"
