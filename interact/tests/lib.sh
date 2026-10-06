# Shared helpers for the INTERACT agent API test scripts.  Source it:
#   . "$(dirname "$0")/lib.sh"

API=${HATARI_API:-http://127.0.0.1:7777}
TESTS=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$TESTS/../../.." && pwd)
OUTDIR=${OUTDIR:-$TESTS/out}
PNGTOOL="python3 $TESTS/pngtool.py"
mkdir -p "$OUTDIR"

# --- HTTP ------------------------------------------------------------

get()  { curl -s -m 30 "$API$1"; }
# post PATH [curl args...]
post() { p=$1; shift; curl -s -m 60 -X POST "$API$p" "$@"; }
# delete PATH
del()  { curl -s -m 30 -X DELETE "$API$1"; }

# --- reporting -------------------------------------------------------

step() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
info() { printf '   %s\n' "$*"; }
ok()   { printf '   \033[32mPASS\033[0m %s\n' "$*"; }
# fail also works inside $(...): it terminates the main script ($$)
fail() { printf '   \033[31mFAIL\033[0m %s\n' "$*" >&2; kill -TERM $$ 2>/dev/null; exit 1; }

# --- emulated time ---------------------------------------------------

# frames N: let N emulated frames pass (50/60 per second), keep running
frames() { post "/emu/run?frames=$1&pause=0" >/dev/null; }

# --- console (NatFeats output of the program) ------------------------

CONSOLE_POS=0
# mark: only look at console output produced from now on
mark() { CONSOLE_POS=$(get '/console?since=999999999' | jq -r .next); }
console_all() { get /console | jq -r .text; }

# wait_for REGEX [TIMEOUT_SECONDS]: wait until a console line produced
# since 'mark' matches, print that line.  Fails the test on timeout.
wait_for() {
	pattern=$1
	timeout=${2:-10}
	end=$(( $(date +%s) + timeout ))
	while [ "$(date +%s)" -le "$end" ]; do
		line=$(get "/console?since=$CONSOLE_POS" | jq -r .text | tr -d '\r' | grep -E -- "$pattern" | head -1)
		if [ -n "$line" ]; then
			echo "$line"
			return 0
		fi
		sleep 0.2
	done
	echo "   last console lines:" >&2
	get "/console?since=$CONSOLE_POS" | jq -r .text | tr -d '\r' | tail -5 | sed 's/^/      /' >&2
	fail "timed out waiting for console line /$pattern/"
}

# --- INTERACT app layout (reported by the app in screen coordinates) --

# layout NAME -> "x y w h" (latest report)
layout() {
	console_all | tr -d '\r' | grep "^INTERACT LAYOUT $1 " | tail -1 | cut -d' ' -f4-7
}
# center NAME -> "x y"
center() {
	set -- $(layout "$1")
	echo "$(( $1 + $3 / 2 )) $(( $2 + $4 / 2 ))"
}

# --- input -----------------------------------------------------------

click_at() { post "/input/click?x=$1&y=$2" >/dev/null; }
click_on() { set -- $(center "$1"); click_at "$1" "$2"; }
mouse_to() { post "/input/mouse?x=$1&y=$2&frames=${3:-2}" >/dev/null; }
button()   { post "/input/click?action=$1" >/dev/null; }   # down / up
type_text(){ post /input/type --data-binary "$1" >/dev/null; }
key()      { post /input/key --data-urlencode "key=$1" >/dev/null; }
joy()      { post "/input/joystick?port=1&dirs=$1&frames=${2:-0}" >/dev/null; }

# --- screen ----------------------------------------------------------

# shot NAME: save native screenshot to $OUTDIR/NAME.png, print path
shot() {
	f="$OUTDIR/$1.png"
	get "/screen${2:+?$2}" > "$f"
	echo "$f"
}

# require the INTERACT app to be up
require_app() {
	get /status >/dev/null 2>&1 || fail "no emulator at $API (run tests/start.sh)"
	console_all | grep -q "^INTERACT READY" || fail "INTERACT not running (run tests/start.sh)"
}
