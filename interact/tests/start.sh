#!/bin/sh
# Start Hatari (ST, EmuTOS) with the agent API, mount build/ as C: and
# autostart INTERACT.PRG.  Returns once the app reported READY.
#   start.sh [--restart] [extra hatari options]
. "$(dirname "$0")/lib.sh"

if [ "$1" = "--restart" ]; then
	shift
	post /emu/quit >/dev/null 2>&1
	sleep 1
fi

PRG=$TESTS/../build/INTERACT.PRG
[ -f "$PRG" ] || fail "build INTERACT.PRG first (make)"

if get /status >/dev/null 2>&1; then
	if console_all | grep -q "^INTERACT READY"; then
		info "already running"
		exit 0
	fi
	fail "another emulator answers at $API (use --restart)"
fi

step "Starting Hatari"
AGENT_TOS=${AGENT_TOS:-$TOP/roms/emutos/emutos-192k-1.4/etos192us.img} \
	"$TOP/tools/agent/hatari-agent-run.sh" --machine st --natfeats on \
	--harddrive "$(cd "$TESTS/../build" && pwd)" --auto 'C:\INTERACT.PRG' \
	--fast-forward on --sound off "$@" || fail "Hatari didn't start"
wait_for "^INTERACT READY" 60 >/dev/null
post /emu/fastforward -d on=0 >/dev/null
ok "INTERACT is up"
console_all | tr -d '\r' | grep "^INTERACT \(SCREEN\|LAYOUT\|SYMBOL\)" | sed 's/^/   /'
