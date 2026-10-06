#!/bin/sh
# Snapshots: save state, change things, restore, compare screens.
. "$(dirname "$0")/lib.sh"
require_app

state=$OUTDIR/interact.sav
set -- $(layout canvas); region="$1 $2 $3 $4"

step "Save snapshot"
post /state/save --data-urlencode "path=$state" | jq -c '{ok}'
before=$($PNGTOOL hash "$(shot snap-before)" $region)
info "canvas hash before: $before"

step "Change things: clear canvas, scribble in red"
mark
click_on Clear; wait_for '^INTERACT CLICK Clear' >/dev/null
click_on Red;   wait_for '^INTERACT COLOR 2' >/dev/null
set -- $region
mouse_to $(( $1 + 20 )) $(( $2 + 20 )); button down; frames 2
mouse_to $(( $1 + 120 )) $(( $2 + 90 )); button up; frames 3
changed=$($PNGTOOL hash "$(shot snap-changed)" $region)
info "canvas hash changed: $changed"
[ "$changed" != "$before" ] || fail "canvas didn't change"

step "Restore snapshot"
post /state/load --data-urlencode "path=$state" | jq -c '{ok,state}'
frames 5
after=$($PNGTOOL hash "$(shot snap-after)" $region)
info "canvas hash after:  $after"
[ "$after" = "$before" ] || fail "restored canvas differs"
ok "snapshot restore brought back the exact screen"
