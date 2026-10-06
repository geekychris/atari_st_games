#!/bin/sh
# Mouse clicks on GEM-drawn buttons, verified via program output and pixels.
. "$(dirname "$0")/lib.sh"
require_app

swatch_rgb() {
	set -- $(layout Clear)
	f=$(shot buttons)
	$PNGTOOL pixel "$f" $(( $1 + $3 + 20 )) $(( $2 + $4 / 2 ))
}

for b in Red:2:"255 0 0" Green:3:"0 255 0" Blue:4:"0 0 255"; do
	name=${b%%:*}; rest=${b#*:}; pen=${rest%%:*}; rgb=${rest#*:}
	step "Click '$name' at $(center "$name")"
	mark
	click_on "$name"
	info "$(wait_for "^INTERACT CLICK $name")"
	info "$(wait_for "^INTERACT COLOR $pen")"
	frames 5
	got=$(swatch_rgb)
	info "swatch is now RGB $got"
	# compare the dominant channel rather than exact palette values
	python3 - "$name" $got <<'PY' || fail "swatch color doesn't look $name"
import sys
name, r, g, b = sys.argv[1], *map(int, sys.argv[2:])
dom = {"Red": r > g and r > b, "Green": g > r and g > b, "Blue": b > r and b > g}[name]
sys.exit(0 if dom else 1)
PY
	ok "$name button works (program output + screen pixel)"
done
