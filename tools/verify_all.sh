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
python3 -m unittest tools/test_action_counts.py
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
python3 -m unittest tools/test_queue_prerequisite_merge.py
python3 -m unittest tools/test_tactical_stay_release.py
python3 -m unittest tools/test_stay_anchor_wiring.py
python3 -m unittest tools/test_attack_order_wiring.py
python3 -m unittest tools/test_keepout_set_aside_wiring.py
python3 -m unittest tools/test_pull_end_engaged.py
python3 -m unittest tools/test_priest_fade_flash_wiring.py
python3 -m unittest tools/test_boss_tactics_sql.py
python3 -m unittest tools/test_dungeon_plan_sql.py
echo "✓ Engine unit tests passed."

echo ""
echo "=== 5. Running Standalone Policy Tests ==="
if command -v g++ >/dev/null 2>&1; then
    g++ -std=c++17 -Wall -Wextra -pedantic tools/test_log_file_rotation.cpp -o "${TMPDIR:-/tmp}/test_log_file_rotation"
    "${TMPDIR:-/tmp}/test_log_file_rotation" "${TMPDIR:-/tmp}"
    POLICY_TESTS=(
        test_adaptive_budget
        test_ah_buyer_policy
        test_ah_seller_teleport_policy
        test_ammo_cache_policy
        test_ammo_cheat_policy
        test_ammo_stack_policy
        test_anubrekhan_swarm_policy
        test_aoe_fear_policy
        test_aoe_off_policy
        test_arcane_rupture_policy
        test_attack_order_policy
        test_auto_tools_kit_policy
        test_autonomous_bg_policy
        test_avoid_aoe_policy
        test_battle_shout_policy
        test_blessing_coverage_policy
        test_bg_force_reset_policy
        test_bomb_runout_policy
        test_boss_tactics_policy
        test_bot_chat_gate
        test_bot_command_queue
        test_bot_turn_scheduler
        test_bwl_bundle1_policy
        test_caster_mana_policy
        test_chromaggus_sand_policy
        test_claimed_bot_policy
        test_class_consumable_policy
        test_combat_spread_policy
        test_combat_stuck_policy
        test_commander_telemetry
        test_creature_aura_keepout_policy
        test_death_cluster_policy
        test_debuff_spread_policy
        test_devour_magic_policy
        test_dispel_target_policy
        test_dps_target_policy
        test_dual_wield_policy
        test_dungeon_follow_hold_policy
        test_dungeon_plan_policy
        test_dungeon_follow_stall_policy
        test_equip_threshold_policy
        test_fishing_spot_policy
        test_flamestrike_window_policy
        test_flight_errand_policy
        test_force_rebuff_policy
        test_four_horsemen_rotation_policy
        test_gather_spot_policy
        test_gather_tool_policy
        test_geddon_inferno_policy
        test_ghost_stall_policy
        test_gluth_kite_policy
        test_golemagg_policy
        test_graveyard_teleport_policy
        test_grind_hostility_policy
        test_grind_spot_policy
        test_grobbulus_cloud_policy
        test_group_heal_policy
        test_group_hygiene_policy
        test_heal_reach_policy
        test_heal_downrank_policy
        test_healer_mana_policy
        test_healing_cast_policy
        test_health_funnel_policy
        test_heigan_dance_policy
        test_hire_deletion_policy
        test_hire_departure_policy
        test_hire_fee_policy
        test_hire_gear_policy
        test_hire_intro_policy
        test_hire_level_policy
        test_hire_respec_policy
        test_hire_quest_policy
        test_hire_spec_policy
        test_hire_tier_request_policy
        test_hire_unlock_group_credit
        test_hire_unlock_policy
        test_hire_unlock_sync
        test_hot_streak_policy
        test_human_bg_demand_policy
        test_hunter_pet_policy
        test_hunter_switch_policy
        test_hunter_viper_policy
        test_instance_guard_policy
        test_interrupt_casting_policy
        test_judgement_upkeep_policy
        test_jump_policy
        test_kelthuzad_adds_policy
        test_ledge_step_down_policy
        test_loatheb_spores_policy
        test_local_pick_policy
        test_long_stuck_rescue_policy
        test_loot_roll_policy
        test_loot_window_policy
        test_lowbie_graveyard_policy
        test_main_tank_policy
        test_mc_garr_shazzrah_policy
        test_mimic_consumable_policy
        test_movement_mode_policy
        test_named_location_index
        test_nearby_service_policy
        test_onyxia_breath_policy
        test_onyxia_whelp_egg_policy
        test_ooc_rebirth_policy
        test_ossirian_crystal_policy
        test_owned_bot_qol_policy
        test_pet_revive_policy
        test_pet_spell_rank_policy
        test_pet_taunt_policy
        test_pet_upkeep_policy
        test_player_bot_classification
        test_player_lag_window
        test_point_danger_policy
        test_pool_bot_trade_policy
        test_pool_flee_policy
        test_pool_reset_policy
        test_profession_grant_policy
        test_protection_potion_policy
        test_pull_firing_policy
        test_pull_opening_policy
        test_pull_join_window_policy
        test_pull_refusal_policy
        test_pull_regen_policy
        test_quest_giver_stall_policy
        test_quest_log_triage_policy
        test_quest_objective_level_policy
        test_quest_reward_policy
        test_quest_stall_policy
        test_quest_taker_level_policy
        test_queued_order_policy
        test_raid_framework_policy
        test_raid_tank_role_policy
        test_ranged_approach_policy
        test_razorgore_policy
        test_ready_rebuff_policy
        test_rear_flank_policy
        test_remote_commander_enter
        test_remote_commander_policy
        test_remote_commander_tasks
        test_remote_commander_view
        test_resist_aura_policy
        test_retaliation_policy
        test_rogue_threat_policy
        test_rogue_weapon_policy
        test_rpg_mixer_policy
        test_rpg_relevance_weight_policy
        test_sapphiron_ice_policy
        test_seduction_policy
        test_service_trip_policy
        test_shaman_earth_shock_policy
        test_shaman_fire_gate_policy
        test_shaman_healer_dps_policy
        test_shaman_imbue_policy
        test_shaman_mana_loop_policy
        test_shaman_recall_policy
        test_shaman_situational_totem_policy
        test_shaman_totem_group_policy
        test_shaman_stoneclaw_policy
        test_skinning_loot_policy
        test_spec_weapon_policy
        test_spell_rank_policy
        test_spread_toggle_policy
        test_start_zone_balance
        test_starter_kit_policy
        test_summon_restore_policy
        test_survive_policy
        test_taker_reachability_cache
        test_taxi_path_policy
        test_thaddius_polarity_policy
        test_trade_lockbox_policy
        test_trade_trainer_policy
        test_trading_lease_policy
        test_travel_instance_policy
        test_travel_pick_slice
        test_travel_repick_policy
        test_travel_route_cache_policy
        test_travel_route_policy
        test_trinket_use_policy
        test_totem_command_policy
        test_undercity_lift_policy
        test_use_item_name_policy
        test_vael_policy
        test_value_refresh_policy
        test_vendor_buy_policy
        test_vendor_trip_policy
        test_vendor_weapon_upgrade_policy
        test_voidwalker_policy
        test_wand_policy
        test_warlock_pet_policy
        test_warlock_stone_policy
        test_warlock_tap_policy
        test_weighted_shuffle_policy
        test_work_idle_policy
        test_world_buff_policy
        test_world_thread_queue
        test_zone_migrate_policy
    )
    for policy_test in "${POLICY_TESTS[@]}"; do
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
