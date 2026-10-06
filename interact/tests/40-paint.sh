#!/bin/sh
# Mouse drag painting: hold the button, move to absolute positions, release.
. "$(dirname "$0")/lib.sh"
require_app

set -- $(layout canvas); cx=$1; cy=$2; cw=$3; ch=$4

# stroke X1 Y1 X2 Y2 ... : drag through canvas-relative points
stroke() {
	mouse_to $(( cx + $1 )) $(( cy + $2 ))
	shift 2
	button down
	frames 2
	while [ $# -ge 2 ]; do
		mouse_to $(( cx + $1 )) $(( cy + $2 ))
		shift 2
	done
	button up
	frames 3
}

step "Clear canvas, choose blue, draw a house"
mark
click_on Clear; wait_for '^INTERACT CLICK Clear' >/dev/null
click_on Blue;  wait_for '^INTERACT COLOR 4' >/dev/null
stroke 40 100  40 60  100 60  100 100  40 100      # walls
stroke 35 63  70 25  105 63                         # roof
info "$(wait_for '^INTERACT PAINT ')"

step "Red door and a green sun"
click_on Red; wait_for '^INTERACT COLOR 2' >/dev/null
stroke 62 100  62 80  78 80  78 100
click_on Green; wait_for '^INTERACT COLOR 3' >/dev/null
stroke 150 20  162 26  168 38  162 50  150 56  138 50  132 38  138 26  150 20
info "$(wait_for "^INTERACT PAINT 9 .* $((cx + 150)),$((cy + 20))\$")"

f=$(shot paint)
info "screenshot: $f"
# exact palette RGB of 'blue': read it from the swatch after selecting Blue
click_on Blue; wait_for '^INTERACT COLOR 4' >/dev/null; frames 3
set -- $(layout Clear)
rgb=$($PNGTOOL pixel "$(shot swatch)" $(( $1 + $3 + 20 )) $(( $2 + $4 / 2 )))
info "blue pen is RGB $rgb"
blue=$($PNGTOOL count "$f" $rgb $cx $cy $cw $ch)
info "blue pixels in canvas: $blue"
[ "${blue:-0}" -gt 100 ] || fail "house not drawn"
ok "painting via mouse drag works"
