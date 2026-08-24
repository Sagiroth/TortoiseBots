#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

fail() {
    echo "TortoiseBots surface check failed: $*" >&2
    exit 1
}

test -f data/sql/world/20260824090000_world.sql || fail "world migration is missing"
test -f data/sql/char/20260824090001_char.sql || fail "character migration is missing"
test -f data/sql/world/20260824090002_world.sql || fail "world compatibility migration is missing"
test -f data/sql/char/20260824090002_char.sql || fail "character compatibility migration is missing"
test -f data/sql/world/20260824090003_world.sql || fail "world cleanup migration is missing"
test -f data/sql/char/20260824090003_char.sql || fail "character cleanup migration is missing"
test ! -e data/sql/World || fail "uppercase World migration directory remains"
test ! -e data/sql/Char || fail "uppercase Char migration directory remains"

grep -q 'template_changed' data/sql/world/20260824090002_world.sql \
    || fail "help schema lacks template_changed"
grep -q 'scale_32' data/sql/char/20260824090002_char.sql \
    || fail "item-info schema lacks scale_32"
grep -q 'ai_playerbot_zone_level' data/sql/world/20260824090002_world.sql \
    || fail "zone-level schema is not owned by World migration"
grep -q 'ADD COLUMN IF NOT EXISTS' data/sql/world/20260824090002_world.sql \
    || fail "World compatibility migration is not additive"
grep -q 'scale_32' data/sql/char/20260824090002_char.sql \
    || fail "Char compatibility migration lacks scale_32"
grep -q 'DROP TABLE IF EXISTS' data/sql/world/20260824090003_world.sql \
    || fail "World cleanup migration lacks explicit dead-table cleanup"
grep -q 'DROP TABLE IF EXISTS' data/sql/char/20260824090003_char.sql \
    || fail "Char cleanup migration lacks explicit dead-table cleanup"

if rg -n -i 'rtsc|see spell|bossaura|ai_playerbot_(random_bots|rpg_races|tele_cache|rarity_cache)' \
    ai host runtime commands conf; then
    fail "removed diagnostic/unused donor surface is still referenced"
fi

if rg -n -i 'CREATE[[:space:]]+TABLE' ai/playerbot/TravelMgr.cpp ai/playerbot/TravelNode.cpp; then
    fail "travel startup still owns runtime table creation"
fi

if rg -n 'BUILD_PLAYERBOTS[[:space:]]*=[[:space:]]*1' TortoiseBots.cmake; then
    fail "native module forces the legacy BUILD_PLAYERBOTS option"
fi

grep -q 'GetMotionMaster()->GetCurrent' ai/playerbot/ServerFacade.cpp \
    || fail "chase inspection fell back to a non-native victim/default path"
grep -q 'GetChannelEntryFor' ai/cmangos-compat-shim.h \
    || fail "chat-channel compatibility proxy is not backed by core data"

for id in \
    27023 27025 27045 27051 27101 27151 27152 27153 34600 \
    42985 49055 49056 49057 49058 49059 49060 49061 49062 49063 \
    49064 49065 49066 49067 49071 49383 54403 \
    21990 21991 22103 22521 22522 23528 23529 \
    23827 28420 28421 30311 30312 30313 30314 30315 30316 30317 \
    30318 36889 36892 37807 37845 37864 38607 38631 39192 39193 \
    39548 39549 40582 43230 43231 43232 43233 43234 43235 47132 47904; do
    if rg -n -w "$id" ai/playerbot; then
        fail "known-absent later-expansion ID remains in module source: $id"
    fi
done

for path in \
    ai/playerbot/strategy/actions/RtscAction.cpp \
    ai/playerbot/strategy/actions/SeeSpellAction.cpp \
    ai/playerbot/strategy/generic/RTSCStrategy.cpp \
    ai/playerbot/strategy/values/RTSCValues.cpp \
    ai/playerbot/strategy/actions/BossAuraActions.cpp \
    ai/playerbot/strategy/triggers/BossAuraTriggers.cpp; do
    test ! -e "$path" || fail "removed donor/test file remains: $path"
done

echo "Tortoise WoW 1.18.1 module surface: OK"
