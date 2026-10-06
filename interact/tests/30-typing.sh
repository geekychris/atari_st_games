#!/bin/sh
# Keyboard: typing text, editing with Backspace, submitting with Return.
. "$(dirname "$0")/lib.sh"
require_app

step "Type 'Hello, Atari!' (shifted characters & punctuation)"
mark
type_text 'Hello, Atari!'
info "$(wait_for '^INTERACT KEY !')"
ok "keys arrived"

step "Fix a typo: type 'xx', then two Backspaces"
type_text 'xx'
key backspace
key backspace
frames 5
f=$(shot typing)
info "screenshot: $f"

step "Submit with Return"
key return
line=$(wait_for '^INTERACT TEXT ')
info "$line"
[ "$line" = "INTERACT TEXT Hello, Atari!" ] || fail "unexpected text line"
ok "text field received exactly 'Hello, Atari!'"

step "Key combos: Return via scancode 0x1c"
mark
type_text 'scancode'
post /input/key -d key=0x1c >/dev/null
info "$(wait_for '^INTERACT TEXT scancode')"
ok "raw scancode works"
