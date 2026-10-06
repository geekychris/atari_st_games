#!/bin/sh
# Start the app (if needed) and run all feature tests.
cd "$(dirname "$0")" || exit 1
# fresh emulator + app for reproducible results
./start.sh --restart || exit 1
failed=0
for t in [0-9][0-9]-*.sh; do
	if ! "./$t"; then
		echo "*** $t failed"
		failed=$((failed + 1))
	fi
done
echo
[ $failed = 0 ] && echo "All tests passed. Screenshots in $(pwd)/out/" || echo "$failed test(s) failed"
exit $failed
