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
echo "=== 4. Running Engine Unit Tests (AoE Density, Walk Gating & Disabled World Updates) ==="
python3 -m unittest tools/test_log_open_rotation.py
python3 -m unittest tools/test_attackers_aoe_density.py
python3 -m unittest tools/test_engine_walk_gating.py
python3 -m unittest tools/test_instance_grind_target.py
python3 -m unittest tools/test_disabled_world_update.py
python3 -m unittest tools/test_loot_content.py
python3 -m unittest tools/test_flee_selection.py
python3 -m unittest tools/test_hazard_waypoint.py
python3 -m unittest tools/test_talent_link_validation.py
python3 -m unittest tools/test_threat_history.py
python3 -m unittest tools/test_history_sampling.py
python3 -m unittest tools/test_my_threat_target.py
python3 -m unittest tools/test_friendly_threat_target.py
python3 -m unittest tools/test_talent_shift_budget.py
python3 -m unittest tools/test_mana_percentage.py
python3 -m unittest tools/test_hazard_chase_motion.py
python3 -m unittest tools/test_travel_route_distance.py
python3 -m unittest tools/test_outfit_item_parser.py
echo "✓ Engine unit tests passed."

echo ""
echo "=== 5. Running Standalone Policy Tests ==="
if command -v g++ >/dev/null 2>&1; then
    g++ -std=c++17 -Wall -Wextra -pedantic tools/test_log_file_rotation.cpp -o "${TMPDIR:-/tmp}/test_log_file_rotation"
    "${TMPDIR:-/tmp}/test_log_file_rotation" "${TMPDIR:-/tmp}"
    for policy_test in test_pool_reset_policy test_pool_pass_rotation test_travel_pick_slice test_adaptive_budget test_player_bot_classification test_hire_deletion_policy test_hire_departure_policy test_hunter_pet_policy test_start_zone_balance test_nearby_service_policy test_grind_spot_policy test_travel_instance_policy test_hire_spec_policy test_pull_firing_policy test_taker_reachability_cache test_death_cluster_policy test_vendor_trip_policy test_long_stuck_rescue_policy test_starter_kit_policy test_grind_hostility_policy test_profession_grant_policy test_vendor_weapon_upgrade_policy test_shaman_imbue_policy test_travel_repick_policy test_pull_regen_policy test_quest_objective_level_policy test_quest_taker_level_policy test_combat_stuck_policy test_ghost_stall_policy test_owned_bot_qol_policy test_survive_policy test_spell_rank_policy test_aoe_fear_policy test_class_consumable_policy test_mimic_consumable_policy test_quest_log_triage_policy test_loot_roll_policy test_skinning_loot_policy test_trading_lease_policy test_pet_upkeep_policy test_gather_tool_policy test_point_danger_policy test_flight_errand_policy test_fishing_spot_policy test_hire_intro_policy test_world_buff_policy test_ah_buyer_policy test_quest_stall_policy test_local_pick_policy test_vendor_buy_policy test_rpg_mixer_policy test_lowbie_graveyard_policy test_zone_migrate_policy test_combat_spread_policy test_pool_bot_trade_policy test_group_buff_policy test_service_trip_policy test_spec_weapon_policy test_warlock_pet_policy test_pet_spell_rank_policy test_rogue_weapon_policy test_ammo_cheat_policy test_hunter_switch_policy test_graveyard_teleport_policy test_ah_seller_teleport_policy test_healing_cast_policy test_healer_mana_policy test_claimed_bot_policy test_player_lag_window test_trade_trainer_policy test_work_idle_policy test_quest_giver_stall_policy; do
        policy_bin="${TMPDIR:-/tmp}/${policy_test}"
        g++ -std=c++17 -Wall -Wextra "tools/${policy_test}.cpp" -o "${policy_bin}"
        "${policy_bin}"
    done
    echo "✓ Standalone policy tests verified."
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
echo "=========================================="
echo "🎉 ALL TORTOISEBOTS CHECKS PASSED CLEANLY!"
echo "=========================================="
