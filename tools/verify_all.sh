#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${HERE}/.." && pwd)"
cd "${ROOT_DIR}"

echo "=== 1. Verifying Open Knowledge Format (OKF) Docs ==="
python3 tools/verify_okf.py
echo "✓ OKF docs verified."

echo ""
echo "=== 2. Verifying Tortoise Core Module Surface ==="
bash tools/verify_tortoise_surface.sh
echo "✓ Module surface verified."

echo ""
echo "=== 3. Verifying Action & Trigger Wiring ==="
python3 tools/verify_action_trigger_wiring.py | grep -E "^(queued=|known|skip)"
echo "✓ Action and trigger wiring verified (0 missing live creators)."

echo ""
echo "=== 4. Running Engine Unit Tests (AoE Density & Walk Gating) ==="
python3 -m unittest tools/test_attackers_aoe_density.py
python3 -m unittest tools/test_engine_walk_gating.py
echo "✓ Engine unit tests passed."

echo ""
echo "=== 5. Running Decision Trail Self-Test ==="
python3 tools/check_decision_trail.py --self-test
echo "✓ Decision trail self-test verified."

DBC_PATH="${DBC_PATH:-}"
if [[ -z "${DBC_PATH}" && -d "../tortoise-docker-penqle/data/dbc" ]]; then
    DBC_PATH="../tortoise-docker-penqle/data/dbc"
fi

if [[ -n "${DBC_PATH}" && -d "${DBC_PATH}" ]]; then
    echo ""
    echo "=== 6. Validating Premade Talent Presets against DBC ==="
    python3 tools/talents/validate_presets.py --dbc "${DBC_PATH}" --conf ai/playerbot/aiplayerbot.conf.dist.in
    echo "✓ Talent presets verified."
else
    echo ""
    echo "ℹ Skipping talent presets check (no DBC_PATH or ../tortoise-docker-penqle/data/dbc not found)."
fi

CORE_PATH="${1:-}"
if [[ -z "${CORE_PATH}" && -d "../tortoise-wow" ]]; then
    CORE_PATH="../tortoise-wow"
fi

if [[ -n "${CORE_PATH}" && -d "${CORE_PATH}" ]]; then
    echo ""
    echo "=== 7. Verifying Host Contract (${CORE_PATH}) ==="
    bash tools/verify_penqle_host_contract.sh --core "${CORE_PATH}"
    echo "✓ Host contract verified."
else
    echo ""
    echo "ℹ Skipping host contract check (no --core path provided or ../tortoise-wow not found)."
fi

echo ""
echo "=========================================="
echo "🎉 ALL TORTOISEBOTS CHECKS PASSED CLEANLY!"
echo "=========================================="
