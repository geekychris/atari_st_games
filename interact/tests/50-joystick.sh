#!/bin/sh
# Joystick port 1: hold directions for N frames, press fire.
. "$(dirname "$0")/lib.sh"
require_app

ball() { console_all | tr -d '\r' | grep '^INTERACT JOY ' | tail -1 | sed 's/.*BALL //'; }

step "Move the ball: right 40 frames, down 25, then up+left diagonally 20"
mark
joy right 40;  info "$(wait_for '^INTERACT JOY 00 ')"
start=$(ball)
mark
joy down 25;   info "$(wait_for '^INTERACT JOY 00 ')"
mark
joy up,left 20; info "$(wait_for '^INTERACT JOY 00 ')"
info "ball now at $(ball)"
[ "$(ball)" != "$start" ] || fail "ball didn't move"
ok "directions work"

step "Fire button changes the ball color"
# With the mouse enabled, the ST reports joystick 1 fire as the right
# mouse button (same hardware line), so the app sees a right click.
mark
joy fire 10
info "$(wait_for '^INTERACT FIRE ')"
frames 5
f=$(shot joystick)
info "screenshot: $f"
ok "fire works"

step "Latched joystick: hold 'left' until released"
mark
before=$(ball)
joy left
info "$(wait_for '^INTERACT JOY 04 ')"
frames 30
joy none
info "$(wait_for '^INTERACT JOY 00 ')"
[ "$(ball)" != "$before" ] || fail "ball didn't move while left was held"
ok "latched input works"
