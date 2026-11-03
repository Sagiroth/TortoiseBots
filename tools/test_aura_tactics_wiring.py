"""Wiring pins for the aura tactic kinds' shared ground: the row gates, the loader columns, the master key and the
channel-slot interrupt.

A row of `ai_boss_tactic` may carry a boss-health band (`hp_min`, `hp_max`), a bot aura (`bot_aura`), a per-kind mode
word (`tactic_mode`) and a linked_death window (`window_s`). Only a row a fight scan reads can answer a gate, and with
AiPlayerbot.DungeonRunAuraTactics off a gated row is closed and the six aura kinds are not read at all, so the table
behaves exactly as it did before them. The rules live in ai/playerbot/BossTacticsPolicy.h (RowGateSet, RowGateOpen,
ParseTacticMode, IsAuraTacticKind, LinkedDeathWindowMs; their behaviour is tested in
tools/test_boss_tactics_policy.cpp). These pins only prove the live code calls them:

 - the scan reads the boss entry's health and this bot's gate aura, through RowGateOpen;
 - the loader selects and parses the five columns and lets the policy refuse a bad row;
 - the master key is read from the config, reloaded with the other DungeonRun keys, and documented in the template;
 - FindBossInterrupt reads both cast slots, so a channelled rotation (Mar'li's Drain Life) can be kicked;
 - the linked_death latch takes the group's own window instead of the constant.

Run: python3 -m unittest tools/test_aura_tactics_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
LOADER = (ROOT / "ai/playerbot/BossTacticsMgr.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
POLICY_TEST = (ROOT / "tools/test_boss_tactics_policy.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
MOVEMENT = (ROOT / "ai/playerbot/strategy/actions/MovementActions.h").read_text()
DUNGEON_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.cpp").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/BossTacticsStrategy.cpp").read_text()
STRATEGY_H = (ROOT / "ai/playerbot/strategy/generic/BossTacticsStrategy.h").read_text()
TACTICS_STRATEGY = STRATEGY
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp").read_text()
TRIGGERS_H = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.h").read_text()
DUNGEON_TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/DungeonTriggers.cpp").read_text()
DPS_TARGET = (ROOT / "ai/playerbot/strategy/values/DpsTargetValue.cpp").read_text()

# Step 3 (dispel_priority): the dispel predicate, the cure multiplier, the AI-side hard never-dispel list and the
# fight strategy that owns the multiplier.
DISPEL_VALUE = (ROOT / "ai/playerbot/strategy/values/PartyMemberToDispel.cpp").read_text()
DISPEL_POLICY = (ROOT / "ai/playerbot/policy/heal/DispelTargetPolicy.h").read_text()
PLAYERBOT_AI_H = (ROOT / "ai/playerbot/PlayerbotAI.h").read_text()
PLAYERBOT_AI_SPELLS = (ROOT / "ai/playerbot/PlayerbotAISpells.cpp").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RowGateWiring(unittest.TestCase):
    def test_the_scan_gates_each_row_on_the_boss_health_and_the_bot_aura(self):
        # The band reads one unit of the boss entry, never an add's health (NX-46's "gate on the add's own health" is
        # deliberately not supported) and never whichever attacker the scan met first: PickBossGateUnit picks the
        # same-entry unit with the highest max health, so a low-health clone cannot open a phase row.
        scan = body(ACTIONS, "BossTacticsFight ai::ScanBossTactics(")
        self.assertIn("std::vector<std::vector<Unit*>> entryUnits;", scan)
        self.assertIn("gateUnit.maxHealth = unit->GetMaxHealth();", scan)
        self.assertIn("gateUnit.hpPct = static_cast<int>(unit->GetHealthPercent());", scan)
        self.assertIn("const int gatePick = PickBossGateUnit(gateUnits);", scan)
        self.assertIn("int const bossHpPct = gatePick < 0 ? 0 : gateUnits[gatePick].hpPct;", scan)
        # No row is gated on the loop's own unit any more (the old first-attacker pick).
        self.assertNotIn("int const bossHpPct = static_cast<int>(unit->GetHealthPercent());", scan)
        self.assertIn("bool const keyOn = sPlayerbotAIConfig.dungeonRunAuraTactics;", scan)
        self.assertIn("RowGateOpen(row, keyOn, bossHpPct, row.botAura != 0 && bot->HasAura(row.botAura))", scan)
        self.assertIn("fight.rows.push_back(&row);", scan)
        self.assertNotIn("GetHealthPercent()) && !RowGateOpen", scan)
        # The six aura kinds are the key's own: with it off their rows never reach a consumer.
        gated = scan[scan.index("for (const BossTacticRow& row : *rows)"):]
        self.assertIn("if (!keyOn && IsAuraTacticKind(row.kind))", gated)
        self.assertLess(gated.index("IsAuraTacticKind(row.kind)"), gated.index("RowGateOpen("))

    def test_the_policy_keeps_the_gate_pure(self):
        self.assertIn("inline bool RowGateSet(const BossTacticRow& row)", POLICY)
        self.assertIn("inline bool RowGateOpen(const BossTacticRow& row, bool keyOn, int bossHpPct, "
                      "bool botHasGateAura)", POLICY)
        gate = body(POLICY, "inline bool RowGateOpen(", "\n    }\n")
        self.assertIn("if (RowGateSet(row) && !keyOn)", gate)
        self.assertIn("if (bossHpPct < row.hpMin || bossHpPct > row.hpMax)", gate)
        self.assertIn("if (row.botAura != 0 && !botHasGateAura)", gate)
        self.assertIn("std::uint8_t hpMin = 0;", POLICY)
        self.assertIn("std::uint8_t hpMax = 100;", POLICY)
        self.assertIn("std::uint32_t botAura = 0;", POLICY)
        # The band's unit is picked from the entry's units, not from their order: max health, then current %,
        # then guid.
        self.assertIn("inline int PickBossGateUnit(const std::vector<BossGateUnit>& units)", POLICY)
        pick = body(POLICY, "inline bool BossGateUnitBefore(", "\n    }\n")
        self.assertIn("a.maxHealth > b.maxHealth", pick)
        self.assertIn("a.hpPct > b.hpPct", pick)
        self.assertIn("a.guid < b.guid", pick)
        # Both orders of the same two units must agree; a first-seen pick trips this.
        self.assertIn("CHECK(bossPick == 0);", POLICY_TEST)
        self.assertIn("CHECK(clonePick == 1);", POLICY_TEST)
        self.assertIn("CHECK(RowGateOpen(band, true, cloneFirst[0].hpPct, false));", POLICY_TEST)

    def test_only_a_scan_read_kind_may_gate(self):
        add = body(POLICY, "bool Add(const BossTacticRow& row)", "\n        }\n")
        self.assertIn("if (RowGateSet(row) && (row.kind == BossTacticKind::HoldKill || "
                      "row.kind == BossTacticKind::TankHold ||", add)
        self.assertIn("row.kind == BossTacticKind::OneAtATime || row.kind == BossTacticKind::LinkedDeath))", add)
        self.assertIn("if (row.hpMin > row.hpMax || row.hpMax > 100)", add)
        # The duplicate key includes the band, so one boss can carry the same kind once per band.
        self.assertIn("have.hpMin == row.hpMin && have.hpMax == row.hpMax", add)
        # ... and the test file pins it.
        self.assertIn("CHECK(table.Add(Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 0, 50)));",
                      POLICY_TEST)
        self.assertIn("CHECK(!table.Add(Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 0, 50)));",
                      POLICY_TEST)

    def test_new_kinds_and_modes_are_parsed_and_gated(self):
        self.assertIn("enum class TacticMode : std::uint8_t", POLICY)
        self.assertIn("inline TacticMode ParseTacticMode(BossTacticKind kind, const std::string& text)", POLICY)
        self.assertIn("inline bool IsAuraTacticKind(BossTacticKind kind)", POLICY)
        for name, value in (("dps_hold_after", 9), ("dispel_priority", 10), ("flee_on_aura", 11),
                            ("freeze_on_aura", 12), ("taunt_after_cast", 13), ("swap_on_aura", 14)):
            self.assertIn('if (text == "%s")' % name, POLICY)
            self.assertIn("%s = %d," % ("".join(part[:1].upper() + part[1:] for part in name.split("_")), value),
                          POLICY)
        self.assertIn("return TacticMode::Invalid;", POLICY)
        add = body(POLICY, "bool Add(const BossTacticRow& row)", "\n        }\n")
        self.assertIn("if (row.mode == TacticMode::Invalid)", add)
        self.assertIn("row.mode = ParseTacticMode(row.kind, fields[11].GetCppString());", LOADER)

    def test_the_map_flags_exist(self):
        self.assertIn("bool HasAuraTacticMap(std::uint32_t map) const", POLICY)
        self.assertIn("bool HasDispelMap(std::uint32_t map) const", POLICY)
        self.assertIn("if (IsAuraTacticKind(row.kind))", POLICY)
        self.assertIn("auraTacticMaps_[row.map] = true;", POLICY)
        self.assertIn("if (row.kind == BossTacticKind::DispelPriority)", POLICY)
        self.assertIn("dispelMaps_[row.map] = true;", POLICY)


class LoaderWiring(unittest.TestCase):
    def test_the_loader_selects_the_five_columns_after_burn_order(self):
        select = LOADER[LOADER.index("SELECT `map`"):LOADER.index("FROM `ai_boss_tactic`")]
        for column in ("burn_order", "hp_min", "hp_max", "bot_aura", "tactic_mode", "window_s"):
            self.assertIn("`%s`" % column, select)
        order = [select.index("`%s`" % column) for column in ("burn_order", "hp_min", "hp_max", "bot_aura",
                                                              "tactic_mode", "window_s")]
        self.assertEqual(sorted(order), order, "the SELECT's five new columns are not after burn_order")
        for index, field in ((8, "hp_min"), (9, "hp_max"), (10, "bot_aura"), (12, "window_s")):
            self.assertIn("fields[%d]" % index, LOADER)
        self.assertIn("row.windowSec = fields[12].GetFloat();", LOADER)
        self.assertIn("row.hpMin = fields[8].GetUInt8();", LOADER)
        self.assertIn("row.hpMax = fields[9].GetUInt8();", LOADER)
        self.assertIn("row.botAura = fields[10].GetUInt32();", LOADER)

    def test_the_skip_message_names_the_new_rules(self):
        message = LOADER[LOADER.index('sLog.outError("ai_boss_tactic:'):]
        message = message[:message.index("row.bossEntry")]
        # The message is one literal split over several lines: join it before matching a phrase.
        message = re.sub(r'"\s*(?:\n\s*)"', "", message)
        message = " ".join(message.split())
        for phrase in ("a health band outside 0-100", "a gate on a kind read straight from the table",
                       "a mode word its kind does not take", "linked_death window that is not positive",
                       "a second row of the key"):
            self.assertIn(phrase, message)


class MasterKeyWiring(unittest.TestCase):
    def test_the_key_is_read_reloaded_and_shipped(self):
        self.assertIn("bool dungeonRunAuraTactics = false;", CONFIG_H)
        load = body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()")
        self.assertIn('dungeonRunAuraTactics = config.GetBoolDefault("AiPlayerbot.DungeonRunAuraTactics", false);',
                      load)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.DungeonRunAuraTactics", false)', CONFIG_CPP)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunAuraTactics = 0$")
        self.assertRegex(TEMPLATE, r"(?m)^# 0 - off: .*\(Default\)$")
        # The key is read by the same block `.reload config` runs for the other DungeonRun keys.
        reload_block = body(CONFIG_CPP, "bool PlayerbotAIConfig::ReloadDungeonRunKeys()")
        self.assertIn("LoadDungeonRunKeys();", reload_block)

    def test_the_gated_rows_read_the_key_everywhere(self):
        for text, name in ((ACTIONS, "BossTacticsActions.cpp"), (POLICY, "BossTacticsPolicy.h")):
            self.assertNotIn("dungeonRunAuraTactics = true", text, name)
        self.assertIn("sPlayerbotAIConfig.dungeonRunAuraTactics", ACTIONS)


class ChannelInterruptWiring(unittest.TestCase):
    def test_find_boss_interrupt_reads_both_slots(self):
        fn = body(ACTIONS, "BossInterruptScan ai::FindBossInterrupt(")
        self.assertIn("CurrentCastIds(unit, genericId, channeledId);", fn)
        self.assertIn("IsInterruptListed(fight.rows, unit->GetEntry(), genericId)", fn)
        self.assertIn("IsInterruptListed(fight.rows, unit->GetEntry(), channeledId)", fn)
        # The generic slot is tried first, so one id in both slots resolves the same way for every bot.
        self.assertLess(fn.index("IsInterruptListed(fight.rows, unit->GetEntry(), genericId)"),
                        fn.index("IsInterruptListed(fight.rows, unit->GetEntry(), channeledId)"))
        ids = body(ACTIONS, "void CurrentCastIds(")
        self.assertIn("unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL)", ids)
        self.assertNotIn("CURRENT_GENERIC_SPELL) == nullptr", fn)


class LinkedDeathWindowWiring(unittest.TestCase):
    def test_the_latch_takes_the_group_window(self):
        self.assertIn("float windowSec = 7.0f;", POLICY)
        self.assertIn("inline std::uint32_t LinkedDeathWindowMs(float windowSec)", POLICY)
        latch = body(POLICY, "inline bool LinkedDeathLatchTick(", "\n    }\n")
        self.assertIn("std::uint32_t windowMs", latch)
        self.assertIn("if (latch.latched && nowMs - latch.sinceMs >= windowMs)", latch)
        self.assertIn("out.windowSec = row->windowSec;", POLICY)
        self.assertIn("out.windowSec = std::max(out.windowSec, other->windowSec);", POLICY)
        scan = body(ACTIONS, "BossLinkedDeathScan ai::ScanBossLinkedDeath(")
        self.assertIn("LinkedDeathWindowMs(group.windowSec)", scan)
        # The row's own window reaches the group, never the constant directly.
        self.assertNotIn("kLinkedDeathLatchMs)", scan)


class DpsHoldWiring(unittest.TestCase):
    def test_the_policy_holds_on_edges_and_caps_until_tank(self):
        self.assertIn("inline bool DpsHoldBlinked(float lastX, float lastY, float x, float y, "
                      "std::uint32_t dtMs)", POLICY)
        self.assertIn("constexpr float kDpsHoldBlinkYd = 10.0f;", POLICY)
        self.assertIn("constexpr std::uint32_t kDpsHoldBlinkMs = 1500;", POLICY)
        self.assertIn("constexpr std::uint32_t kDpsHoldStaleMs = 600000;", POLICY)
        tick = body(POLICY, "inline bool DpsHoldTick(", "\n    }\n")
        # Every trigger is an edge, never a level: a cast that stays visible and an open band that stays open do not
        # restart the hold on every read.
        self.assertIn("in.engageEnabled && in.engaged && !state.engageSeen", tick)
        self.assertIn("in.gateOpen && in.gateEnabled && !state.gateWasOpen", tick)
        self.assertIn("in.castEnabled && in.castOrAuraSeen && !me->castSeen", tick)
        self.assertIn("DpsHoldBlinked(state.lastX, state.lastY, in.x, in.y, nowMs - state.lastReadMs)", tick)
        # until_tank cannot deadlock: the cap is 3 * param from the latest trigger.
        self.assertIn("state.capMs = nowMs + 3 * holdMs;", tick)
        self.assertIn("in.untilTank && !in.victimIsTank && nowMs < state.capMs", tick)
        self.assertIn("CHECK(!DpsHoldTick(tank, in, 12000, nullptr));", POLICY_TEST)
        self.assertIn("CHECK(!DpsHoldTick(spell, in, 3100, nullptr));", POLICY_TEST)

    def test_the_action_and_trigger_are_registered_above_the_kill_first_pick(self):
        self.assertIn('creators["boss tactics dps hold"] = [](PlayerbotAI* ai) { return new BossTacticsDpsHoldAction(ai); };',
                      ACTION_CONTEXT)
        self.assertIn('creators["boss tactics dps hold"] = [](PlayerbotAI* ai) '
                      '{ return new BossTacticsDpsHoldTrigger(ai); };', TRIGGER_CONTEXT)
        self.assertIn('"boss tactics dps hold",', TACTICS_STRATEGY)
        self.assertIn('NextAction("boss tactics dps hold", ACTION_HIGH + 4)', TACTICS_STRATEGY)
        self.assertLess(TACTICS_STRATEGY.index("ACTION_HIGH + 3"),
                        TACTICS_STRATEGY.index('NextAction("boss tactics dps hold", ACTION_HIGH + 4)'))
        action = body(ACTIONS, "bool BossTacticsDpsHoldAction::Execute(")
        self.assertIn('AI_VALUE(Unit*, "current target")', action)
        self.assertIn("!IsDpsHeld(ai, bot, current)", action)
        # The stop is the hold-kill action's: the kill-first add stays allowed, else the pick is dropped and the
        # attack stopped.
        self.assertIn("FindBossKillFirst(ai, bot, ScanBossTactics(ai, bot))", action)
        self.assertIn('context->GetValue<Unit*>("current target")->Set(nullptr);', action)
        self.assertIn('context->GetValue<ObjectGuid>("attack target")->Set(ObjectGuid());', action)
        self.assertIn("bot->AttackStop();", action)
        trig = body(TRIGGERS, "bool BossTacticsDpsHoldTrigger::IsActive(")
        self.assertIn("current && IsDpsHeld(ai, bot, current)", trig)
        self.assertIn('BossTacticsDpsHoldTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics dps hold", 1)',
                      TRIGGERS_H)

    def test_the_hold_is_gated_by_the_key_and_the_map_flag(self):
        held = body(ACTIONS, "bool ai::IsDpsHeld(")
        self.assertIn("!sPlayerbotAIConfig.dungeonRunAuraTactics || "
                      "!GetBossTactics().HasAuraTacticMap(bot->GetMapId())", held)
        self.assertIn('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT) || !IsFreeDps(ai, bot)', held)
        self.assertIn("BossTacticKind::DpsHoldAfter", held)
        self.assertIn("ReadDpsHold(ai, bot, unit, row, bossUnit, &fired)", held)
        # The target picker consults the hold beside the hold_kill rows, so every dps picker skips the subject.
        target = body(DPS_TARGET, "bool HeldDpsTarget(", "\n    }\n")
        self.assertIn("if (IsDpsHeld(ai, bot, unit))", target)
        self.assertLess(target.index("IsDpsHeld"), target.index("BossTacticKind::HoldKill"))

    def test_the_multiplier_vetoes_area_and_dot_on_a_held_subject(self):
        self.assertIn("DpsHold,", ACTIONS)
        helper = body(ACTIONS, "bool DpsHoldSubjectWithin(")
        self.assertIn("BossTacticKind::DpsHoldAfter", helper)
        # Every same-entry unit is read (the veto is any held one in range), not only the last unit of the entry.
        self.assertIn("for (Unit* unit : subjects)", helper)
        self.assertIn("held.push_back(ReadDpsHold(ai, ai->GetBot(), unit, *row, bossUnit));", helper)
        self.assertIn("distance.push_back(around->GetDistance(unit));", helper)
        self.assertIn("DpsHoldVetoIndex(held, distance, maxDistance)", helper)
        read = body(ACTIONS, "bool ReadDpsHold(")
        self.assertIn("in.botEligible = row.botAura == 0 || botHasAura;", read)
        held = body(ACTIONS, "HoldReason HeldSubjectWithin(")
        self.assertIn("DpsHoldSubjectWithin(ai, around, fight, maxDistance, &held)", held)
        self.assertIn("return HoldReason::DpsHold;", held)
        multiplier = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(", "\n}\n")
        self.assertIn("const bool auraHolds = sPlayerbotAIConfig.dungeonRunAuraTactics", multiplier)
        self.assertIn("BossTacticKind::DpsHoldAfter", multiplier)
        self.assertIn("const bool area = (holdsRows || dpsHoldRows) && IsAreaDamageAction(ai, action);", multiplier)

    def test_the_hold_logs_one_row_per_trigger_and_bot(self):
        log = body(ACTIONS, "void LogDpsHold(")
        self.assertIn("g_dpsHoldLog[std::make_pair(bot->GetObjectGuid().GetRawValue(), key)]", log)
        self.assertIn('snprintf(info1, sizeof(info1), "subject=%u", subject->GetEntry());', log)
        self.assertIn('"trigger=%s,held_ms=%u"', log)
        self.assertIn("DpsHoldTriggerName(trigger)", log)
        read = body(ACTIONS, "bool ReadDpsHold(")
        self.assertIn("if (held && IsFreeDps(ai, bot))", read)
        self.assertIn("LogDpsHold(ai, bot, subject, row, lastTrigger, lastTriggerMs, key);", read)

    def test_the_state_is_pruned_on_death_and_staleness(self):
        prune = body(ACTIONS, "void PruneDpsHolds(")
        self.assertIn("nowMs - it->second.lastReadMs > kDpsHoldStaleMs", prune)
        self.assertIn("!subject->IsAlive()", prune)
        self.assertIn("g_dpsHolds.erase(it)", prune)
        read = body(ACTIONS, "bool ReadDpsHold(")
        self.assertIn("std::lock_guard<std::mutex> lock(g_dpsHoldMutex);", read)
        # The key is the map instance, the subject and the row's own key columns: one hold per band.
        self.assertIn("std::make_tuple(bot->GetMapId(), bot->GetInstanceId(), "
                      "subject->GetObjectGuid().GetRawValue(),", ACTIONS)
        self.assertIn("row.bossEntry, row.spellId, row.hpMin, row.hpMax)", ACTIONS)


class DispelPriorityIsRanked(unittest.TestCase):
    """The predicate asks the pure verdict for the member and orders by the
    row's rank first, the target tier second. Without this the row's
    priorities never reach the cure pick."""

    def test_predicate_calls_the_verdict_and_the_rank_order(self):
        fn = body(DISPEL_VALUE, "virtual bool Check(Unit* unit) override", "\n    }\n")
        self.assertIn("DispelMemberVerdict(rows, auras, dispelType", fn)
        self.assertIn("DispelCandidateBetter(rank, tier, bestRank, bestTier)", fn)
        # The tier-0 early return may only fire for a rank-0 member, or a rank-2 carrier in reach would end the search.
        self.assertIn("if (ranked && rank == 0 && tier == 0)", fn)

    def test_excluded_member_is_skipped_before_its_aura_is_offered(self):
        fn = body(DISPEL_VALUE, "virtual bool Check(Unit* unit) override", "\n    }\n")
        self.assertLess(fn.index("LogDispelExcluded"), fn.index("HasAuraToDispel"))
        self.assertIn("verdict.excluded", fn)

    def test_plain_predicate_when_no_row_is_active(self):
        fn = body(DISPEL_VALUE, "virtual bool Check(Unit* unit) override", "\n    }\n")
        self.assertIn("tier >= bestTier", fn)  # the old order is still there, unranked
        self.assertIn("ranked ? &overrideSet : nullptr", fn)


class DispelPriorityIsGated(unittest.TestCase):
    """Off, the rows are inert: the predicate reads no row and the
    hard never-dispel list keeps deciding exactly as before."""

    def test_predicate_needs_the_key_the_map_and_the_strategy(self):
        fn = body(DISPEL_VALUE, "Unit* PartyMemberToDispel::Calculate()")
        self.assertIn("sPlayerbotAIConfig.dungeonRunAuraTactics", fn)
        self.assertIn("GetBossTactics().HasDispelMap(bot->GetMapId())", fn)
        self.assertIn('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', fn)

    def test_multiplier_needs_the_key_the_map_and_the_strategy(self):
        fn = body(ACTIONS, "float BossTacticsDispelHoldMultiplier::GetValue")
        self.assertIn("sPlayerbotAIConfig.dungeonRunAuraTactics", fn)
        self.assertIn("GetBossTactics().HasDispelMap(bot->GetMapId())", fn)
        self.assertIn('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', fn)


class DispelHoldIsRegistered(unittest.TestCase):
    """The safety net only exists when the combat engine owns it."""

    def test_multiplier_is_on_the_combat_engine(self):
        fn = body(TACTICS_STRATEGY, "void BossTacticsStrategy::InitCombatMultipliers")
        self.assertIn("new BossTacticsDispelHoldMultiplier(ai)", fn)

    def test_multiplier_is_not_on_the_non_combat_or_reaction_engine(self):
        self.assertNotIn("BossTacticsDispelHoldMultiplier",
                         body(TACTICS_STRATEGY, "void BossTacticsStrategy::InitNonCombatMultipliers"))
        self.assertNotIn("BossTacticsDispelHoldMultiplier",
                         body(TACTICS_STRATEGY, "void BossTacticsStrategy::InitCombatTriggers"))


class DispelHoldCoversTheAreaCures(unittest.TestCase):
    """Abolish Poison's later ticks, the cleansing totems and Stoneform remove
    auras the row never chose, so the multiplier checks every nearby member."""

    def test_multiplier_checks_the_party_cure_target(self):
        fn = body(ACTIONS, "float BossTacticsDispelHoldMultiplier::GetValue")
        self.assertIn("dynamic_cast<CurePartyMemberAction*>(action)", fn)
        self.assertIn("verdict.excluded", fn)
        self.assertIn("return 0.0f;", fn)

    def test_the_hold_judges_every_type_the_cast_spell_removes(self):
        # A poison-triggered Cleanse also strips magic and disease, so the hold must not read only GetDispelType().
        fn = body(ACTIONS, "float BossTacticsDispelHoldMultiplier::GetValue")
        self.assertIn("ActionDispelTypes(ai, spell)", fn)
        self.assertIn("DispelCureVerdict(rows, auras, types, allyYd, defaultClearYd)", fn)
        self.assertNotIn("GetDispelType()", fn)
        # The type set comes from the spell's effects, not a name table.
        types = body(ACTIONS, "void CollectSpellDispelTypes(")
        self.assertIn("SPELL_EFFECT_DISPEL", types)
        self.assertIn("EffectMiscValue[i]", types)
        self.assertIn("SPELL_AURA_DISPEL_IMMUNITY", types)
        self.assertIn("SPELL_ATTR_EX_DISPEL_AURAS_ON_IMMUNITY", types)
        self.assertIn("SPELL_AURA_PERIODIC_TRIGGER_SPELL", types)
        self.assertNotIn("CureDispelTypes", ACTIONS)

    def test_area_cures_scan_the_nearby_group(self):
        fn = body(ACTIONS, "float BossTacticsDispelHoldMultiplier::GetValue")
        for name in ('"abolish disease"', '"disease cleansing totem"', '"poison cleansing totem"', '"stoneform"'):
            self.assertIn(name, ACTIONS)
        self.assertIn("kDispelSafetyYd", fn)
        self.assertIn("LiveGroupMembers(group)", fn)

    def test_self_cures_judge_the_bot(self):
        fn = body(ACTIONS, "float BossTacticsDispelHoldMultiplier::GetValue")
        self.assertLess(fn.index("if (!areaCure)"), fn.index("veto(bot)"))


class TheRunningCuresAreSwept(unittest.TestCase):
    """A protected aura applied after an area cure started would be removed by its next tick or pulse: the bot-cast
    Abolish aura is removed and the bot's own cleansing totem destroyed, and only those."""

    def test_sweep_removes_only_bot_cast_auras(self):
        fn = body(ACTIONS, "bool SweepDispelCures(")
        self.assertIn("GET_PLAYERBOT_AI(caster)", fn)
        self.assertIn("RemoveAurasByCasterSpell(entry.first, entry.second)", fn)
        # The aura that decided the verdict is the protected one itself: never remove that.
        self.assertIn("verdict.auraId == aura->GetId()", fn)

    def test_sweep_destroys_the_bots_own_totem_in_range(self):
        fn = body(ACTIONS, "bool SweepDispelCures(")
        self.assertIn("bot->GetTotem(TotemSlot(slot))", fn)
        self.assertIn("TotemDispelRadius(totemSpell)", fn)
        self.assertIn("totem->UnSummon()", fn)
        self.assertIn("sServerFacade.GetDistance2d(totem, member) > radius", fn)

    def test_probe_mode_reports_work_but_mutates_nothing(self):
        # apply=false is the read-only probe the trigger evaluates: it must still report work, but the aura removal
        # and the totem destroy both sit behind `if (apply)`.
        fn = body(ACTIONS, "bool SweepDispelCures(")
        self.assertRegex(
            fn,
            r"if \(apply\)\s*member->RemoveAurasByCasterSpell\(entry\.first, entry\.second\);\s*swept = true;",
        )
        self.assertRegex(fn, r"if \(apply\)\s*totem->UnSummon\(\);\s*swept = true;")
        self.assertEqual(fn.count("RemoveAurasByCasterSpell("), 1)
        self.assertEqual(fn.count("UnSummon("), 1)

    def test_sweep_is_gated_and_registered(self):
        fn = body(ACTIONS, "bool ai::SweepBossDispelCures(")
        self.assertIn("sPlayerbotAIConfig.dungeonRunAuraTactics", fn)
        self.assertIn("GetBossTactics().HasDispelMap(bot->GetMapId())", fn)
        self.assertIn('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', fn)
        self.assertIn("DispelRows(ScanBossTactics(ai, bot))", fn)
        self.assertIn("SweepBossDispelCures(ai, bot, false)", TRIGGERS)
        self.assertIn("SweepBossDispelCures(ai, bot, true)", ACTIONS)
        self.assertIn('"boss tactics dispel sweep"', TACTICS_STRATEGY)
        self.assertIn('creators["boss tactics dispel sweep"]', ACTION_CONTEXT)
        self.assertIn('creators["boss tactics dispel sweep"]', TRIGGER_CONTEXT)


class TheHardNeverListIsShared(unittest.TestCase):
    """28169 must stay excluded with no row (today's behaviour), and a clear
    row must be able to release it: both readers need the same list."""

    def test_list_lives_in_the_dispel_policy(self):
        self.assertIn("constexpr std::uint32_t kNeverDispelSpellIds[] = { 28169 };", DISPEL_POLICY)
        self.assertIn("inline bool IsNeverDispelSpellId(", DISPEL_POLICY)
        self.assertIn("struct DispelOverride", DISPEL_POLICY)

    def test_ai_side_has_no_copy_of_the_list(self):
        self.assertNotIn("static constexpr uint32 kNeverDispelSpellIds[]", PLAYERBOT_AI_SPELLS)
        self.assertIn("return IsNeverDispelSpellId(spellId);", PLAYERBOT_AI_SPELLS)

    def test_candispel_consults_the_override(self):
        fn = body(PLAYERBOT_AI_SPELLS, "bool PlayerbotAI::canDispel(")
        self.assertIn("override->deny", fn)
        self.assertIn("override->allow", fn)
        self.assertIn("DispelOverrideContains(", fn)

    def test_has_aura_to_dispel_has_the_override_overload(self):
        self.assertIn(
            "virtual bool HasAuraToDispel(Unit* player, uint32 dispelType, const DispelOverride* override = nullptr);",
            PLAYERBOT_AI_H)
        self.assertIn(
            "bool canDispel(const SpellEntry* entry, uint32 dispelType, const DispelOverride* override = nullptr);",
            PLAYERBOT_AI_H)

    def test_verdict_is_the_single_reader_of_the_list(self):
        fn = body(POLICY, "inline DispelVerdict DispelMemberVerdict(", "\n    }\n")
        self.assertIn("IsNeverDispelSpellId(aura.spellId)", fn)
        self.assertIn("TacticMode::Never", fn)
        self.assertIn("TacticMode::Clear", fn)


class TheDispelDecisionIsLogged(unittest.TestCase):
    def test_log_row_carries_the_reported_fields(self):
        self.assertIn('"BossTacticsDispel"', ACTIONS)
        for field in ("aura=", "carrier=", "mode=", "rank=", "ally_yd=", "verdict="):
            self.assertIn(field, ACTIONS)
        self.assertIn('"pick"', ACTIONS)
        self.assertIn('verdict.wait ? "wait" : "excluded"', ACTIONS)


class DispelHoldAtCastTime(unittest.TestCase):
    """The member can gain the protected aura after the action was chosen, and a paladin Cure resolves one of its
    dispel types at random, so the same hold is judged again at the last gate: the cast itself."""

    def test_the_cast_path_refuses_the_spell_before_the_core_cast(self):
        fn = body(PLAYERBOT_AI_SPELLS, "bool PlayerbotAI::CastSpell(uint32 spellId, Unit* target")
        self.assertIn("DispelCastBlocked(this, target, spellId)", fn)
        self.assertLess(fn.index("DispelCastBlocked"), fn.index("new Spell("))
        self.assertIn("reason=dispel-hold", fn)

    def test_the_cast_gate_judges_every_type_the_spell_removes(self):
        fn = body(ACTIONS, "bool ai::DispelCastBlocked")
        self.assertIn("CollectSpellDispelTypes(sSpellMgr.GetSpellEntry(spellId), types)", fn)
        self.assertIn("DispelCastVerdict(targetKind, rows, auras, types", fn)
        self.assertIn("GetBossTactics().HasDispelMap(bot->GetMapId())", fn)
        self.assertIn('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', fn)

    def test_a_group_pet_is_judged_by_its_owners_group(self):
        fn = body(ACTIONS, "bool ai::DispelCastBlocked")
        self.assertIn("dynamic_cast<Pet*>(target)", fn)
        self.assertIn("dynamic_cast<Player*>(pet->GetOwner())", fn)
        self.assertIn("owner->GetGroup() == bot->GetGroup()", fn)
        self.assertIn("DispelTargetKind::GroupPet", fn)
        # A target the gate cannot place stays blocked (fail closed).
        self.assertIn("DispelTargetKind::Unknown", fn)


class FreezeFleeWiring(unittest.TestCase):
    def test_freeze_and_flee_are_ordered_on_both_walk_engines(self):
        combat = body(STRATEGY, "void BossTacticsStrategy::InitCombatTriggers(")
        self.assertIn('new NextAction("boss tactics freeze", ACTION_EMERGENCY + 7)', combat)
        self.assertIn('new NextAction("boss tactics flee", ACTION_EMERGENCY + 6)', combat)
        # Freeze above flee above the listed-cast walk-out (BossTacticsPolicy.h's ordering).
        self.assertLess(combat.index('"boss tactics freeze"'), combat.index('"boss tactics flee"'))
        self.assertLess(combat.index('"boss tactics flee"'), combat.index('"boss tactics avoid cast"'))

        # The reaction engine stops a walk and a cast the tick the aura lands, so both rows live there too.
        reaction = body(STRATEGY, "void BossTacticsStrategy::InitReactionTriggers(")
        self.assertIn('new NextAction("boss tactics freeze", ACTION_EMERGENCY + 7)', reaction)
        self.assertIn('new NextAction("boss tactics flee", ACTION_EMERGENCY + 6)', reaction)
        self.assertLess(reaction.index('"boss tactics freeze"'), reaction.index('"boss tactics flee"'))

        # Out of combat a frozen carrier can still heal or walk: the non-combat engine carries the freeze.
        noncombat = body(STRATEGY, "void BossTacticsStrategy::InitNonCombatTriggers(")
        self.assertIn('new NextAction("boss tactics freeze", ACTION_EMERGENCY + 7)', noncombat)
        self.assertNotIn('"boss tactics flee"', noncombat)

    def test_the_freeze_multiplier_runs_on_all_three_engines(self):
        for function in ("InitCombatMultipliers", "InitNonCombatMultipliers", "InitReactionMultipliers"):
            section = body(STRATEGY, "void BossTacticsStrategy::" + function)
            self.assertIn("new BossTacticsFreezeMultiplier(ai)", section, function)
        self.assertIn("void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;", STRATEGY_H)
        self.assertIn("class BossTacticsFreezeMultiplier : public Multiplier", ACTIONS_H)
        multiplier = body(ACTIONS, "float BossTacticsFreezeMultiplier::GetValue")
        self.assertIn("FreezeHoldsAction(aura != 0, isFreeze)", multiplier)
        self.assertIn("dynamic_cast<BossTacticsFreezeAction*>(action)", multiplier)
        self.assertIn("!sPlayerbotAIConfig.dungeonRunAuraTactics", multiplier)
        self.assertIn("HasAuraTacticMap(bot->GetMapId())", multiplier)

    def test_the_freeze_action_stops_once_and_logs_the_drift(self):
        action = body(ACTIONS, "bool BossTacticsFreezeAction::Execute(")
        self.assertIn("FreezeEpisodeTick(ai, bot, aura)", action)
        self.assertIn("FreezeStopDue(ai, bot, aura)", action)
        for call in ("bot->AttackStop();", "bot->CastStop();", "bot->StopMoving();"):
            self.assertIn(call, action)
        self.assertIn('"BossTacticsFreeze"', ACTIONS)
        self.assertIn('"ms=%u,moved_yd=%.2f"', ACTIONS)

    def test_the_raid_bomb_trigger_defers_to_an_active_flee_row(self):
        bomb = body(DUNGEON_TRIGGERS, "bool RaidBombDebuffTrigger::IsActive()")
        self.assertIn("FleeRowListsAura(ai, bot, spellId)", bomb)
        after = bomb[bomb.index("FleeRowListsAura(ai, bot, spellId)"):]
        self.assertIn("continue;", after[:after.index("return true;")])
        helper = body(ACTIONS, "bool ai::FleeRowListsAura(")
        self.assertIn("!sPlayerbotAIConfig.dungeonRunAuraTactics", helper)
        self.assertIn("HasAuraTacticMap(bot->GetMapId())", helper)
        self.assertIn('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', helper)
        self.assertIn("BossTacticKind::FleeOnAura", helper)

    def test_the_new_kinds_are_read_from_the_key_gated_scan(self):
        # The rows come from the fight scan, which drops the six aura kinds with the key off (step 1), so neither
        # helper reads the table directly.
        freeze = body(ACTIONS, "std::uint32_t ai::FreezeAuraOnBot(")
        self.assertIn("for (const BossTacticRow* row : fight.rows)", freeze)
        self.assertIn("BossTacticKind::FreezeOnAura", freeze)
        self.assertIn("bot->HasAura(row->spellId)", freeze)
        flee = body(ACTIONS, "const BossTacticRow* ai::ActiveFleeRow(")
        self.assertIn("for (const BossTacticRow* row : fight.rows)", flee)
        self.assertIn("BossTacticKind::FleeOnAura", flee)
        self.assertIn("bot->HasAura(row->spellId)", flee)
        self.assertIn('"BossTacticsFlee"', ACTIONS)
        self.assertIn('walk=%s', ACTIONS)

    def test_the_runout_body_is_shared_and_the_hard_coded_path_is_unchanged(self):
        self.assertIn("bool RunOutFromGroup(MovementAction& mover, PlayerbotAI* ai, Player* bot, float radius, "
                      "bool requireHealer);", MOVEMENT)
        self.assertIn("friend bool RunOutFromGroup(", MOVEMENT)
        self.assertIn("bool ai::RunOutFromGroup(", DUNGEON_ACTIONS)
        hard = body(DUNGEON_ACTIONS, "bool RaidBombRunoutAction::Execute(")
        self.assertIn("RunOutFromGroup(*this, ai, bot, sPlayerbotAIConfig.bombRunoutDistance, false)", hard)
        flee = body(ACTIONS, "bool BossTacticsFleeAction::Execute(")
        self.assertIn("RunOutFromGroup(*this, ai, bot, radius, true)", flee)
        self.assertIn("GetRaidTankRoles(ai).MainTank()", flee)
        self.assertIn("MoveTo(tank, radius)", flee)
        self.assertIn("PlayerbotAI::IsTank(bot)", flee)
        # The healer list is the row path's own: the hard-coded runout passes false and keeps its old step.
        self.assertIn("if (requireHealer && PlayerbotAI::IsHeal(member))", DUNGEON_ACTIONS)
        self.assertIn("healers.data(), healers.size()", DUNGEON_ACTIONS)

    def test_the_contexts_register_the_new_action_and_trigger(self):
        for name, cls in (("boss tactics freeze", "BossTacticsFreezeAction"),
                          ("boss tactics flee", "BossTacticsFleeAction")):
            self.assertIn('creators["%s"]' % name, ACTION_CONTEXT)
            self.assertIn("%s(ai)" % cls, ACTION_CONTEXT)
            self.assertIn('creators["%s"]' % name, TRIGGER_CONTEXT)
        self.assertIn("BossTacticsFreezeTrigger(ai)", TRIGGER_CONTEXT)
        self.assertIn("BossTacticsFleeTrigger(ai)", TRIGGER_CONTEXT)

    def test_declarations_match_definitions(self):
        pairs = ((ACTIONS_H, "class BossTacticsFreezeAction", ACTIONS,
                  "bool BossTacticsFreezeAction::Execute(Event& event)"),
                 (ACTIONS_H, "class BossTacticsFleeAction", ACTIONS,
                  "bool BossTacticsFleeAction::Execute(Event& event)"),
                 (ACTIONS_H, "class BossTacticsFreezeMultiplier", ACTIONS,
                  "float BossTacticsFreezeMultiplier::GetValue(Action* action)"),
                 (TRIGGERS_H, "class BossTacticsFreezeTrigger", TRIGGERS,
                  "bool BossTacticsFreezeTrigger::IsActive()"),
                 (TRIGGERS_H, "class BossTacticsFleeTrigger", TRIGGERS,
                  "bool BossTacticsFleeTrigger::IsActive()"))
        for header, declaration, source, definition in pairs:
            self.assertIn(declaration, header)
            self.assertIn(definition, source)


if __name__ == "__main__":
    unittest.main()
