#!/usr/bin/env bash
set -euo pipefail

fail() {
    echo "TortoiseBots host-contract check failed: $*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: tools/verify_penqle_host_contract.sh --core <path-to-Penqle-core>

Read-only source check for the generic host interfaces required by TortoiseBots.
Run it after rebasing the module onto the merged #411/#416 core pair and before
claiming compatibility. It neither configures nor builds the supplied core.
EOF
}

command -v rg >/dev/null 2>&1 \
    || fail "ripgrep (rg) is required to verify the generic host contract"

CORE_DIR=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --core)
            [[ $# -ge 2 ]] || fail "--core requires a path"
            CORE_DIR=$2
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument: $1"
            ;;
    esac
done

[[ -n "$CORE_DIR" ]] || { usage >&2; exit 2; }
[[ -d "$CORE_DIR" ]] || fail "core directory does not exist: $CORE_DIR"
[[ -d "$CORE_DIR/src/game" ]] || fail "not a Tortoise core source tree: $CORE_DIR"

require_file() {
    [[ -f "$CORE_DIR/$1" ]] || fail "required core file is missing: $1"
}

require_pattern() {
    local pattern=$1
    shift
    rg -n --quiet "$pattern" "$@" \
        || fail "required generic host contract is missing: $pattern"
}

require_file src/game/SessionTransport.h
require_file src/game/HeadlessSessionMgr.h
require_file src/game/World.h
require_file src/game/WorldSession.h
require_file src/game/Handlers/CharacterCreation.h
require_file src/game/LFT/LFTMgr.h
require_file src/game/Battlegrounds/BattleGroundMgr.h

# #411: transport is generic, immutable and World owns GUID-keyed Headless
# construction, login dispatch, update, reclaim and lifetime. These façade
# names intentionally match the module's calls.
require_pattern 'SessionTransport' "$CORE_DIR/src/game/SessionTransport.h" "$CORE_DIR/src/game/WorldSession.h"
require_pattern 'Headless' "$CORE_DIR/src/game/SessionTransport.h"
require_pattern 'Network' "$CORE_DIR/src/game/SessionTransport.h"
require_pattern 'IsHeadless\(' "$CORE_DIR/src/game/WorldSession.h"
require_pattern 'InitHeadlessSession\(' "$CORE_DIR/src/game/WorldSession.h"
for method in StartHeadlessSession StopHeadlessSession GetHeadlessSessionState; do
    require_pattern "$method" "$CORE_DIR/src/game/World.h"
done

# #416: generic participant primitives. Core remains the owner of character
# persistence, LFT offers/queues and battleground queue state.
require_pattern 'CreateCharacter\(' "$CORE_DIR/src/game/Handlers/CharacterCreation.h"
for method in GetQueuedPlayers QueuePlayer LeaveQueue IsQueued IsInOffer AcceptOffer; do
    require_pattern "$method" "$CORE_DIR/src/game/LFT/LFTMgr.h"
done
require_pattern 'GetQueuedParticipants' "$CORE_DIR/src/game/Battlegrounds/BattleGroundMgr.h"

# The target core may understand a generic Headless transport, but normal game
# code must not regain the legacy PlayerBots object/manager coupling.
if rg -n 'GetBot\(|SetBot\(|\bm_bot\b|sPlayerBotMgr|PlayerBotEntry' "$CORE_DIR/src/game"; then
    fail "legacy PlayerBots coupling remains in normal core gameplay code"
fi

if git -C "$CORE_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    CORE_REVISION=$(git -C "$CORE_DIR" rev-parse HEAD)
else
    CORE_REVISION=unknown
fi

echo "TortoiseBots generic Penqle host contract: OK (core ${CORE_REVISION})"
