#!/bin/sh
# Runs the GLua regression suites with a stock Lua 5.1 interpreter.
set -e
here=$(cd "$(dirname "$0")" && pwd)
if [ -d "$here/../../garrysmod" ]; then
    root=$(cd "$here/../.." && pwd)
else
    root=$(cd "$here/.." && pwd)
fi
lua=${LUA:-lua5.1}
status=0
for suite in runtime runtime_v3 acceptance management console_ascii bootstrap gvm_runtime disk_protection gincy33 scheduler_bench; do
    [ -f "$here/$suite.lua" ] || continue
    if ! "$lua" "$here/$suite.lua" "$root"; then
        echo "FAIL $suite"
        status=1
    fi
done
exit $status
