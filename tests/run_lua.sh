#!/bin/sh
# Runs the GLua regression suites with a stock Lua 5.1 interpreter.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
lua=${LUA:-lua5.1}
status=0
for suite in runtime runtime_v3 acceptance management console productization; do
    [ -f "$root/tests/$suite.lua" ] || continue
    if ! "$lua" "$root/tests/$suite.lua" "$root"; then
        echo "FAIL $suite"
        status=1
    fi
done
exit $status
