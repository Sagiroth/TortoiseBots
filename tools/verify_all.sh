#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${HERE}/.." && pwd)"
cd "${ROOT_DIR}"

echo "=== 1. Verifying Open Knowledge Format (OKF) Docs ==="
python3 tools/verify_okf.py
python3 tools/verify_manual_value_types.py
echo "✓ OKF docs verified."

echo ""
echo "=== 1b. Verifying .bot Command Table Against Docs ==="
python3 tools/verify_command_docs.py
echo "✓ Command table documented."

echo ""
echo "=== 2. Verifying Tortoise Core Module Surface ==="
bash tools/verify_tortoise_surface.sh
echo "✓ Module surface verified."
python3 tools/check_eluna_guards.py

echo ""
echo "=== 3. Verifying Action & Trigger Wiring ==="
python3 tools/verify_action_trigger_wiring.py | grep -E "^(queued=|known|skip)"
echo "✓ Action and trigger wiring verified (0 missing live creators)."

echo ""
echo "=== 4. Running Engine Unit Tests (discovered: tools/test_*.py) ==="
python3 -m unittest discover -s tools -p 'test_*.py'
if [[ -z "${TORTOISE_CORE:-}" ]]; then
    echo "SKIPPED: core-side repop door checks (set TORTOISE_CORE=<core checkout>; the VM build gate runs them)"
fi
echo "✓ Engine unit tests passed."

echo ""
echo "=== 5. Running Standalone Policy Tests ==="
if command -v g++ >/dev/null 2>&1; then
    JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
    BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tortoisebots-verify-tests.XXXXXX")"
    trap 'rm -rf "${BUILD_DIR}"' EXIT
    export BUILD_DIR

    # Special case: needs a writable scratch directory as its argument.
    g++ -std=c++17 -Wall -Wextra -pedantic tools/test_log_file_rotation.cpp -o "${BUILD_DIR}/test_log_file_rotation"
    "${BUILD_DIR}/test_log_file_rotation" "${TMPDIR:-/tmp}"

    # Special case: the run trace's file writer builds from its own source and needs the thread library.
    g++ -std=c++17 -Wall -Wextra tools/test_run_trace_writer.cpp runtime/RunTraceWriter.cpp -pthread -o "${BUILD_DIR}/test_run_trace_writer"
    "${BUILD_DIR}/test_run_trace_writer" "${TMPDIR:-/tmp}"

    # Every other standalone test: discover the sources, compile and run in parallel.
    mapfile -t CPP_TESTS < <(printf '%s\n' tools/test_*.cpp | grep -v '^tools/test_log_file_rotation\.cpp$' | grep -v '^tools/test_run_trace_writer\.cpp$')

    compile_one() {
        local src="$1" name
        name="$(basename "${src}" .cpp)"
        if ! g++ -std=c++17 -Wall -Wextra "${src}" -o "${BUILD_DIR}/${name}" 2> "${BUILD_DIR}/${name}.compile.log"; then
            echo "COMPILE FAILED: ${name} (last 20 lines):"
            tail -n 20 "${BUILD_DIR}/${name}.compile.log" | sed 's/^/    /'
            return 1
        fi
        rm -f "${BUILD_DIR}/${name}.compile.log"
    }

    run_one() {
        local src="$1" name
        name="$(basename "${src}" .cpp)"
        [[ -x "${BUILD_DIR}/${name}" ]] || return 0 # compile failure already reported
        if ! "${BUILD_DIR}/${name}" > "${BUILD_DIR}/${name}.run.log" 2>&1; then
            echo "TEST FAILED: ${name} (output):"
            sed 's/^/    /' "${BUILD_DIR}/${name}.run.log"
            return 1
        fi
        rm -f "${BUILD_DIR}/${name}.run.log"
    }
    export -f compile_one run_one

    FAILED=0
    printf '%s\n' "${CPP_TESTS[@]}" | xargs -P "${JOBS}" -I{} bash -c 'compile_one "$@"' _ {} || FAILED=1
    printf '%s\n' "${CPP_TESTS[@]}" | xargs -P "${JOBS}" -I{} bash -c 'run_one "$@"' _ {} || FAILED=1

    if [[ "${FAILED}" -ne 0 ]]; then
        echo "✗ Standalone policy tests FAILED."
        exit 1
    fi
    echo "✓ $((${#CPP_TESTS[@]} + 2)) standalone policy tests verified (compiled and run in parallel, ${JOBS} jobs)."
else
    echo "ℹ Skipping standalone policy tests (no g++ on PATH)."
fi

echo ""
echo "=== 6. Running Decision Trail Self-Test ==="
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
echo "=== 8. Verifying Config Defaults vs Shipped Templates ==="
python3 tools/verify_config_defaults.py
python3 -m unittest tools/test_verify_config_defaults.py
echo "✓ Config defaults verified."

echo ""
echo "=== 8b. Verifying Config Keys Are Actually Used ==="
python3 tools/verify_config_usage.py
python3 -m unittest tools/test_verify_config_usage.py
echo "✓ Config usage verified."

echo ""
echo "=========================================="
echo "🎉 ALL TORTOISEBOTS CHECKS PASSED CLEANLY!"
echo "=========================================="
