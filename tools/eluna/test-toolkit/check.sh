#!/bin/sh
# Syntax check (no execution of the scripts) + the mock test in tests/: luac -p for Lua 5.1 and 5.4 (Eluna builds 5.2 by
# default; code is kept valid for 5.1-5.4) and luacheck when installed.
set -eu
cd "$(dirname "$0")"
rc=0
for f in lua_scripts/tt/*; do
    for c in luac5.1 luac5.4 luac; do
        if command -v "$c" >/dev/null 2>&1; then
            "$c" -p - < "$f" || { echo "FAIL $c $f"; rc=1; }
        fi
    done
done
if command -v luacheck >/dev/null 2>&1; then
    luacheck --config .luacheckrc lua_scripts/tt/* || rc=1
fi
# mock tests (plain Lua, no server): quiet/clearto dz window, skip filter, friendly rule; q los
for c in lua5.4 lua5.1 lua; do
    if command -v "$c" >/dev/null 2>&1; then
        for t in tests/mock_window_test.lua tests/mock_los_test.lua; do
            "$c" "$t" >/dev/null || { echo "FAIL mock test ($c $t)"; rc=1; }
        done
        break
    fi
done
exit $rc
