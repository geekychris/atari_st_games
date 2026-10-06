#!/bin/sh
# Debugger: breakpoint on a C function, inspect registers/stack/memory,
# single-step, continue.  The click that triggers it completes afterwards.
. "$(dirname "$0")/lib.sh"
require_app

addr=$(console_all | tr -d '\r' | grep '^INTERACT SYMBOL on_button' | tail -1 | cut -d' ' -f4)
[ -n "$addr" ] || fail "app didn't report on_button address"

step "Breakpoint on on_button() at $addr"
post /debug/breakpoints -d "addr=$addr" | jq -c '.breakpoints'

step "Click 'Green' in the background, wait for the CPU to stop"
mark
click_on Green &
CLICK=$!
stop=$(post "/debug/wait?timeout_ms=10000")
echo "$stop" | jq -e '.stopped' >/dev/null || fail "breakpoint not hit"
info "stopped: $(echo "$stop" | jq -c '.stop')"
info "next instructions:"
echo "$stop" | jq -r '.disasm[].text' | sed 's/^/      /'

step "Inspect: argument on the stack is the button id (Green = 1)"
sp=$(echo "$stop" | jq -r .regs.a7)
arg=$(get "/mem?addr=$((sp + 4))&len=4" | jq -r .hex)
info "a7=$sp  4(a7)=0x$arg"
[ "$arg" = 00000001 ] || fail "expected id 1"
ok "function argument read from emulated stack"

step "Single-step 3 instructions"
for i in 1 2 3; do
	post /debug/step | jq -r '"      " + .pc + "  " + .disasm[0].text'
done

step "Remove breakpoint & continue"
del "/debug/breakpoints?index=all" >/dev/null
post /debug/continue | jq -c '{state}'
wait $CLICK
info "$(wait_for '^INTERACT CLICK Green')"
ok "program continued normally after debugging"
