#!/usr/bin/env bash
set -euo pipefail

fail() {
    echo "TortoiseBots surface check failed: $*" >&2
    exit 1
}

command -v rg >/dev/null 2>&1 \
    || fail "ripgrep (rg) is required to verify the Turtle module surface"

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

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

# Player-facing convenience transitions have their own module. BotManager owns
# only Headless lifecycle, bot records and AI attachment; do not let short-lived
# movement/combat state machines grow back into the lifecycle owner.
test -f behavior/PlayerConvenience.cpp || fail "player-convenience module is missing"
test -f behavior/PlayerConvenience.h || fail "player-convenience interface is missing"
grep -q 'behavior/PlayerConvenience.cpp' TortoiseBots.cmake \
    || fail "player-convenience module is absent from the native source graph"
if rg -n 'RequestPullback|RequestSummon|UpdatePullbacks|UpdateSummons|m_pullbacks|m_summons|PullbackState|SummonState' \
    runtime/BotManager.cpp runtime/BotManager.h; then
    fail "player convenience behavior leaked into BotManager lifecycle ownership"
fi
if rg -n 'BindBotMaster|record->masterGuid[[:space:]]*=' behavior/PlayerConvenience.cpp; then
    fail "player convenience mutates durable master binding directly"
fi
grep -q 'SetBotFollow' behavior/PlayerConvenience.cpp \
    || fail "summon completion no longer restores follow through the mature path"

# The core's Headless manager owns construction, login dispatch, pending state,
# reclaim and destruction. The module must use only the generic World façade.
grep -q 'StartHeadlessSession' host/BotSessionAdapter.cpp \
    || fail "bot session adapter does not start through the World façade"
grep -q 'StopHeadlessSession' host/BotSessionAdapter.cpp \
    || fail "bot session adapter does not stop through the World façade"
grep -q 'GetHeadlessSessionState' host/BotSessionAdapter.cpp \
    || fail "bot session adapter does not query core Headless state"
if rg -n 'AddHeadlessSession|FindHeadlessSession|HasPendingHeadlessSession|CancelPendingHeadlessSession|RemoveHeadlessSession|CreateHeadlessSession|LogoutHeadlessSession' \
    host runtime commands; then
    fail "module bypasses the generic Headless lifecycle façade"
fi

# A controllable snapshot requires the full module readiness chain, not just
# an in-world Player and a Headless transport.
grep -q 'IsUsable' runtime/PlayerbotAIAdapter.h runtime/PlayerbotAIAdapter.cpp \
    || fail "AI adapter readiness is not exposed"
grep -q 'IsControllableBot' runtime/BotManager.h runtime/BotManager.cpp \
    || fail "BotManager has no centralized controllable-bot gate"
grep -q 'entry.aiAdapter->IsUsable' runtime/BotManager.cpp \
    || fail "BotManager does not require a usable adapter for live bots"
grep -q 'forceActivity' ai/playerbot/PlayerbotAI.h ai/playerbot/PlayerbotAI.cpp \
    || fail "explicit command activity override is missing"
grep -q 'explicitActivityOverride' ai/playerbot/PlayerbotAI.cpp \
    || fail "activity override is not scoped to one explicit decision"
grep -q 'minimal tick defers low-relevance queue' ai/playerbot/strategy/Engine.cpp \
    || fail "minimal engine path can revisit the same ineligible queue head"

# Vetted player controls must stay mapped to fixed mature actions. Do not turn
# the public shell into an unrestricted forwarding path while adding aliases.
for control in guard free ready attack formation status; do
    grep -q "cmd == \"$control\"" commands/BotCommands.cpp \
        || fail "public player control is missing: $control"
done
grep -q 'guard chat shortcut' commands/BotCommands.cpp \
    || fail "guard is no longer mapped to its mature shortcut action"
grep -q 'free chat shortcut' commands/BotCommands.cpp \
    || fail "free is no longer mapped to its mature shortcut action"
grep -q '"ready check"' commands/BotCommands.cpp \
    || fail "ready is no longer mapped to the mature ready-check action"
grep -q '"attack my target"' commands/BotCommands.cpp \
    || fail "attack is no longer mapped to the mature selected-target action"
grep -q 'DoSpecificAction("formation"' commands/BotCommands.cpp \
    || fail "formation is no longer mapped to the mature formation action"
grep -q 'formation == "shield"' commands/BotCommands.cpp \
    || fail "formation no longer uses the reviewed finite enum"

# CC executor selection must come from the mature action graph, not a second
# class-to-spell table that can silently omit a Vanilla-capable class.
grep -q 'GetSupportedActions' commands/BotCommandContext.cpp \
    || fail "CC executor selection no longer queries the mature action graph"
if rg -n 'GetBotCcSpell|cls[[:space:]]*==[[:space:]]*CLASS_' commands/BotCommandContext.cpp; then
    fail "CC executor selection regressed to a hard-coded class table"
fi
for cc_marker_file in \
    ai/playerbot/strategy/actions/GenericSpellActions.h \
    ai/playerbot/strategy/hunter/HunterActions.h \
    ai/playerbot/strategy/paladin/PaladinActions.h \
    ai/playerbot/strategy/rogue/RogueActions.h; do
    rg -q 'IsCrowdControlAction' "$cc_marker_file" \
        || fail "mature CC action marker is missing from $cc_marker_file"
done

if rg -n 'Get(MaxEntry|NumRows)\(\) const \{ return (10000|100000|1500|200000);' \
    ai/cmangos-compat-shim.h; then
    fail "store compatibility proxy still uses a fixed donor-era upper bound"
fi

if rg -n 'setAreaCost|\.setArea\(' ai/playerbot; then
    fail "module still calls the pinned core's no-op PathInfo area-filter API"
fi

if rg -n 'addStrategy\("avoid mobs"\)|addStrategies\([^\n]*"avoid mobs"' ai/playerbot/AiFactory.cpp; then
    fail "default AI still enables unsupported mob-avoidance behavior"
fi

if rg -n 'EmotesTextSoundEntry|FindTextSoundEmoteFor|getAreaOverride\(\) const \{ return ""' \
    ai; then
    fail "module still hides emote/area presentation behind an empty compatibility stub"
fi

if rg -n 'NONE_PERMISSION' ai/playerbot/strategy/values/LootValues.cpp; then
    fail "loot status still asks the core for its intentionally empty NONE_PERMISSION view"
fi

grep -q 'CheckCast(true)' ai/playerbot/strategy/actions/UseItemAction.cpp \
    || fail "item casts are not using the native core pre-cast contract"

grep -q 'GetMotionMaster()->GetCurrent' ai/playerbot/ServerFacade.cpp \
    || fail "chase inspection fell back to a non-native victim/default path"
grep -q 'GetChannelEntryFor' ai/cmangos-compat-shim.h \
    || fail "chat-channel compatibility proxy is not backed by core data"
grep -q 'GenerateFishLocations", false' ai/playerbot/PlayerbotAIConfig.cpp \
    || fail "fish-location generation is not disabled by default"
grep -q 'GetTaxi().GetTaxiPath' ai/playerbot/strategy/actions/GoAction.cpp \
    || fail "taxi position reporting is not backed by the native PlayerTaxi route"

if rg -n 'IsInBlackstoneOrThalassian|GetTransportAnimInfo|TransportAnimation|FormationSlotData|WorldSessionStateStub|InstanceTemplate' \
    ai; then
    fail "removed or unsupported compatibility surface is still active in the module"
fi

for id in \
    27023 27025 27045 27051 27101 27151 27152 27153 34600 \
    42985 49055 49056 49057 49058 49059 49060 49061 49062 49063 \
    49064 49065 49066 49067 49071 49383 54403 \
    21990 21991 22044 22103 22147 22148 22521 22522 23528 23529 30330 33312 34976 39253 39645 44452 47436 52566 \
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
