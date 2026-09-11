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
echo "=== 3. Running Decision Trail Self-Test ==="
python3 tools/check_decision_trail.py --self-test
echo "✓ Decision trail self-test verified."

CORE_PATH="${1:-}"
if [[ -z "${CORE_PATH}" && -d "../tortoise-wow" ]]; then
    CORE_PATH="../tortoise-wow"
fi

if [[ -n "${CORE_PATH}" && -d "${CORE_PATH}" ]]; then
    echo ""
    echo "=== 4. Verifying Host Contract (${CORE_PATH}) ==="
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
