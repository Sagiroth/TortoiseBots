"""The run leader's glue hands the policy what its retry rules match on.

runtime/DungeonRunPolicy.h retries a refused gather once and passes a refused
walk-back point by comparing the refused order's text with the order it sent
that tick (issue #999), so the glue in commands/Commander.cpp must report a
refusal with the order exactly as Tick() returned it. A dead lead bot released
by the regroup's `revive party` is a ghost off the plan's map; the glue must
not pause the run for that while a regroup (a resume or one the run started by
itself after a wipe) is on. `cmd move` must judge the point with
RemoteCommander::CheckMove from the lead bot's own 3D distance, which is what
lets the run leader's gather on the lead bot's own spot through (and only that
gather, not a player's order). During a walk back the step facts (hop pulls)
are read for the replayed move point.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RunLeaderWiringTest(unittest.TestCase):
    def test_refusal_reports_the_order_as_sent(self):
        self.assertIn(": TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);", COMMANDS)
        self.assertIn("bool const accepted = IssueRunOrder(commander, order, answer,\n"
                      "            TortoiseBots::DungeonRun::IsGatherOrder(it->second.run, order), &captured);", COMMANDS)
        self.assertIn("TortoiseBots::DungeonRun::OrderRefused(it->second.run, order, answer", COMMANDS)

    def test_ghost_lead_tolerated_during_a_regroup(self):
        # Any dead lead bot off the map (a ghost after a wipe too) lets the tick run; it starts the regroup.
        self.assertIn("bool const deadLeadAway = !lead->IsAlive();", COMMANDS)
        self.assertIn("if (lead->GetMapId() != slot.run.plan->map && !deadLeadAway)", COMMANDS)

    def test_exact_waypoint_facts_and_kind(self):
        # The exact waypoint's stand-still proof reads the lead bot's moving state (DungeonRunPolicy.h ExactWalkTo), and the
        # loader reads a move_hop_exact row as a MoveDirect with exactLaunch (DungeonPlanPolicy.h ParsePlanStepRowKind).
        self.assertIn("f.leadMoving = lead->IsMoving();", body(COMMANDS, "void ReadRunFacts(", "\n}\n"))
        loader = (ROOT / "ai/playerbot/DungeonPlanMgr.cpp").read_text()
        self.assertIn("ParsePlanStepRowKind(fields[3].GetCppString(), step.kind, step.exactLaunch, step.openProof);", loader)
        self.assertNotIn("step.kind = ParsePlanStepKind(", loader)

    def test_cmd_move_checks_the_lead_distance(self):
        self.assertIn("facts.distanceFromLead = std::sqrt(dx * dx + dy * dy + dz * dz);", COMMANDS)
        self.assertIn("RemoteCommander::MoveVerdict verdict = RemoteCommander::CheckMove(facts);", COMMANDS)

    def test_only_the_run_leaders_gather_gets_the_lead_spot_pass(self):
        # A player's `cmd move` never sets it: only IssueRunOrder, for the run's own gather.
        self.assertIn("facts.leadSpotGather = s_runIssuing && s_runGather;", COMMANDS)
        issue = body(COMMANDS, "bool IssueRunOrder(", "\n}\n")
        self.assertIn("s_runGather = gather;", issue)
        self.assertIn("s_runGather = false;", issue)
        self.assertEqual(COMMANDS.count("s_runGather = gather;"), 1)

    def test_gather_stragglers_are_reported_by_name(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("member.name = bot->GetName();", facts)
        self.assertIn("member.yd = yd;", facts)
        self.assertIn("member.dz = bot->GetMap() == lead->GetMap() ? std::fabs(bot->GetPositionZ() - lead->GetPositionZ()) : 0.0f;", facts)
        # During a walk back out of sight the hop pulls are read around the next approach hop.
        self.assertIn("cx = f.approachX;", facts)
        self.assertIn("f.members.push_back(member);", facts)

    def test_run_hold_is_let_go_on_every_path(self):
        # The hold (DungeonRun::Run::hold) is synced after every tick that runs, and let go on the
        # paths that return before Tick: a paused run, the lead bot off the plan's map, `run stop`.
        tick = body(COMMANDS, "void TickDungeonRun(", "\n}\n")
        self.assertIn("SyncRunHold(commander, lead, bots, it->second, !order.empty());", tick)
        early = body(tick, "if (slot.run.state != TortoiseBots::DungeonRun::State::Running || !commander)", "return;")
        self.assertIn("ReleaseRunHold(commander, binding->leadGuid, slot);", early)
        offmap = body(tick, "PauseLeadOffMap(slot.run, slot.run.plan->map);", "return;")
        self.assertIn("ReleaseRunHold(commander, binding->leadGuid, slot);", offmap)
        stop = body(COMMANDS, 'EndRunEarly(slot.run, RunStopNote(rawArg));', "EraseRunSlot(commanderRaw);")
        self.assertIn("ReleaseRunHold(commander, binding->leadGuid, slot);", stop)
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f.deadNames.push_back(bot->GetName());", facts)
        self.assertIn("member.x = bot->GetPositionX();", facts)

    def test_run_back_reads_cached_attackers_and_restores_the_ai(self):
        # A member coming back with attackers (DungeonRun::RunBack): its attackers are counted from the
        # core's attacker set (no scan); the glue makes a listed bot a passive follower of the lead bot,
        # never undoes a passive stance it did not set, and gives the AI back once the run lets it go.
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("for (Unit* attacker : bot->GetAttackers())", facts)
        self.assertIn("++member.attackers;", facts)
        start = body(COMMANDS, "void StartRunBackPassive(", "\n}\n")
        self.assertIn('ai->HasStrategy("passive", BotState::BOT_STATE_COMBAT)', start)
        self.assertIn('ai->ChangeStrategy("+passive", BotState::BOT_STATE_COMBAT);', start)
        self.assertIn("PointFollowerAtLead(commanderRaw, bot, lead);", start)
        tick = body(COMMANDS, "void TickDungeonRun(", "\n}\n")
        self.assertIn("for (std::string const& name : slot.run.runBack)", tick)
        sync = body(COMMANDS, "void SyncRunBackPassive()\n{", "\n}\n")
        self.assertIn("run->second.run.state == TortoiseBots::DungeonRun::State::Running", sync)
        self.assertIn("EndRunBackPassive(it->first);", sync)
        update = body(COMMANDS, "static void UpdateDungeonRuns(", "\n}\n")
        self.assertIn("SyncRunBackPassive();", update)
        self.assertIn("f.leadInCombat = member.inCombat;", facts)

    def test_player_order_restores_every_run_back_bot_first(self):
        # A player's order (a `cmd flee Bob` too) gives every bot the run made passive its own AI back
        # before the order runs; the order is noted before it executes at every call site, so a flee
        # then sets the stance on the bots it names only. No bot of the commander stays passive.
        pause = body(COMMANDS, "void PauseDungeonRunForPlayer(uint64 commanderRaw, std::string const& order)\n{", "\n}\n")
        self.assertIn("EndRunBackPassive(passive->first);", pause)
        self.assertNotIn('order.find("flee")', pause)
        # The order is noted before it runs: right after parsing in `.bot action`.
        action = body((ROOT / "commands/Action.cpp").read_text(),
                      "bool HandleAction(ChatHandler* handler, char const* args)", "\n}\n")
        self.assertIn("if (parsed)\n        PauseDungeonRunForPlayer(", action)


    def test_boss_down_needs_his_corpse(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("if (bossEntry && fromPoint <= step->radius + kSeeYd)\n"
                      "                            f.bossDeadNearSpot = true;", facts)
        self.assertIn("if (bossEntry && lead->GetDistance(creature) <= kSeeYd)\n"
                      "                            f.bossDeadNearLead = true;", facts)

    def test_boss_corpse_guid_is_read_for_the_loot_step(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f.bossCorpseGuid = creature->GetObjectGuid().GetRawValue();", facts)
        self.assertIn("(!f.bossCorpseGuid || fromPoint < bossCorpseFromSpot)", facts)

    def test_gather_anchor_path_is_read(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        # Read lazily: only after the policy asked for it.
        self.assertIn("if (slot.run.anchorPathWanted && slot.run.anchorKnown && lead->IsAlive() && lead->IsInWorld() &&", facts)
        self.assertIn("f.anchorPathRead = true;", facts)
        self.assertIn("f.anchorReachable = !(type & RemoteCommander::kPathNoPath) &&", facts)

    def test_direct_walk_back_checks_the_path_against_packs(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f.directPathCrossesPack = PathCrossesUnclearedPack(*slot.run.plan,\n"
                      "                            TortoiseBots::DungeonRun::LiveFrom(slot.run, true), whole, f.team,\n"
                      "                            TortoiseBots::DungeonRun::UnclearedForPath(slot.run));", facts)
        # UnclearedForPath is notCleared plus the steps before a placed run's start (2026-10-23).
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        self.assertIn("std::unordered_set<std::size_t> out = r.notCleared;", body(policy,
            "inline std::unordered_set<std::size_t> UnclearedForPath(const Run& r)", "\n}\n"))

    def test_guard_reads_every_run_cc_as_held(self):
        # The tank's guard leaves a shackled, banished or sapped mob alone like a sheep: both of its
        # reads (the lead bot's nearest attacker and the party's attackers), as code.
        guard = strip_comments(body(COMMANDS, "void ReadGuardFacts(", "\n}\n"))
        self.assertIn("fact.polymorphed = RunCcHeld(nearestHit);", guard)
        self.assertIn("fact.polymorphed = RunCcHeld(attacker);", guard)

    def test_focus_skull_and_camp_casters_are_wired(self):
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        self.assertIn("it->second.run.focusIconClear", tick)
        self.assertIn("EndRunIcon(clearGroup, TortoiseBots::DungeonRun::kFocusIcon, focus)", tick)
        self.assertIn("pending.icon = TortoiseBots::DungeonRun::kFocusIcon;", tick)
        self.assertLess(tick.index("StopLeadReach(commander, lead);"), tick.index("it->second.run.focusIconClear = 0;"))
        clear = body(COMMANDS, "void ClearRunCcIcons(uint64 leadRaw, uint32 groupId, DungeonRunSlot& slot)\n{", "\n}\n")
        self.assertIn("slot.run.focusIconGuid, slot.run.focusIconClear, slot.run.prevFocusIconGuid", clear)
        self.assertIn("pending.icon = TortoiseBots::DungeonRun::kFocusIcon;", clear)
        self.assertEqual(clear.count("PendingRunCcIcons().push_back(pending);"), 2)  # cc loop + focus loop
        self.assertEqual(COMMANDS.count("fact.castingAtRange = fact.hitting && !melee;"), 2)
        self.assertEqual(COMMANDS.count("IdleHostilesNear(lead, attacker, TortoiseBots::DungeonRun::kGuardCampYd)"), 2)
        caster = body(COMMANDS, "uint32 IdleHostilesNear(Player* lead, Unit* unit, float range)\n{", "\n}\n")
        self.assertIn("other != unit &&", caster)
        self.assertIn("GroupCanAttack(lead, other)", caster)
        reset = body(policy, "inline void ResetStep(Run& r)", "\n}\n")
        self.assertIn("DropFocus(r);", reset)
        self.assertNotIn("r.focusGuid = 0;", reset)
        self.assertEqual(policy.count("r.focusGuid = 0;"), 1)   # DropFocus's own line only

    def test_cc_clear_order_and_run_end_icons(self):
        # `cmd cc clear <bot> [<mark> <guid>]`: the bot's rti cc goes to none, the icon only while on that unit.
        clear = strip_comments(COMMANDS.split('if (!parts.empty() && parts[0] == "clear")', 1)[1].split("// cc <mark> <guid> [bot]", 1)[0])
        self.assertIn('namedAi->GetAiObjectContext()->GetValue<std::string>("rti cc")->Set("none");', clear)
        self.assertIn("order.group->GetTargetIcon(static_cast<uint8>(iconIndex)).GetRawValue() == iconGuid)", clear)
        self.assertIn("order.group->SetTargetIcon(static_cast<uint8>(iconIndex), ObjectGuid());", clear)
        # The run's end: the group without the lead bot too (its id from the binding), each icon by
        # DungeonRun::RunIconAtEnd, and a group not found keeps the icon for a bounded retry.
        group = strip_comments(body(COMMANDS, "Group* RunIconGroup(uint64 leadRaw, uint32 groupId)\n{", "\n}\n"))
        self.assertIn("return groupId ? sObjectMgr.GetGroupById(groupId) : nullptr;", group)
        end = strip_comments(body(COMMANDS, "bool EndRunIcon(Group* group, int icon, uint64 guid)\n{", "\n}\n"))
        self.assertIn("RunIconAtEnd(group != nullptr, live, guid)", end)
        self.assertIn("group->SetTargetIcon(static_cast<uint8>(icon), ObjectGuid());", end)
        icons = strip_comments(body(COMMANDS, "void ClearRunCcIcons(uint64 leadRaw, uint32 groupId, DungeonRunSlot& slot)\n{", "\n}\n"))
        self.assertIn("Group* const group = RunIconGroup(leadRaw, groupId);", icons)
        self.assertIn("if (!EndRunIcon(group, icon.first, icon.second))", icons)
        self.assertIn("PendingRunCcIcons().push_back(pending);", icons)
        retry = strip_comments(body(COMMANDS, "void RetryRunCcIcons(uint32 diff)\n{", "\n}\n"))
        self.assertIn("EndRunIcon(RunIconGroup(it->leadRaw, it->groupId), it->icon, it->guid) || it->ageMs >= kRunIconRetryMs", retry)
        update = strip_comments(body(COMMANDS, "void UpdateRemoteCommanders(uint32_t diff)\n{", "\n}\n"))
        self.assertLess(update.index("RetryRunCcIcons(diff);"), update.index("if (CommanderBindings().Empty())"))
        # The facts confirm the record only when the icons were read.
        facts = strip_comments(body(COMMANDS, "void ReadRunFacts(", "\n}\n"))
        self.assertIn("f.raidIconsRead = true;", facts)
        # Every run end clears them: the time limit, `.bot run stop`, the plan's end, an unbind, a switch-off.
        code = strip_comments(COMMANDS)
        self.assertEqual(code.count("ClearRunCcIcons(binding->leadGuid, binding->groupId, slot);"), 3)
        self.assertEqual(code.count("ClearRunCcIcons(standing->leadGuid, standing->groupId, run->second);"), 1)
        self.assertEqual(code.count("ClearRunCcIcons(standing->leadGuid, standing->groupId, entry.second);"), 1)

    def test_crowd_control_and_kill_first_facts(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        # The crowd control is picked from the members (issue #1159): a mage that knows Polymorph, and
        # the priest's, warlock's and rogue's run cc by KnownRunCc; each unit carries its creature type
        # and whether a run cc aura is on it, on both unit reads (the step's pack and a hold's units).
        self.assertIn("member.polymorph = bot->GetClass() == CLASS_MAGE && KnowsPolymorph(bot);", facts)
        code = "\n".join(line.split("//", 1)[0].rstrip() for line in facts.splitlines())
        self.assertIn("member.cc = KnownRunCc(bot);", code)
        self.assertEqual(code.count("unit.polymorphed = RunCcHeld(creature);"), 2)
        self.assertEqual(code.count("unit.creatureType = creature->GetCreatureType();"), 2)
        self.assertNotIn("Polymorphed(", COMMANDS)
        known = body(COMMANDS, "uint32 KnownRunCc(", "\n}\n")
        self.assertIn("case CLASS_PRIEST: return KnowsAny(bot, kShackleUndeadSpells) ? CcBit(CcFamily::Shackle) : 0;", known)
        self.assertIn("case CLASS_WARLOCK: return KnowsAny(bot, kBanishSpells) ? CcBit(CcFamily::Banish) : 0;", known)
        self.assertIn("case CLASS_ROGUE: return KnowsAny(bot, kSapSpells) ? CcBit(CcFamily::Sap) : 0;", known)
        held = body(COMMANDS, "bool RunCcHeld(", "\n}\n")
        for spells in ("kPolymorphSpells", "kShackleUndeadSpells", "kBanishSpells", "kSapSpells"):
            self.assertIn("HasAnyAura(unit, %s)" % spells, held)
        # Review of fe5d4109: each unit's immunity, the rogue's live master and the live raid icons are read.
        self.assertEqual(code.count("unit.ccImmune = RunCcImmune(creature);"), 2)
        self.assertIn("member.canSap = (member.cc & CcBit(CcFamily::Sap)) && memberAi && memberAi->HasRealPlayerMaster() &&", code)
        self.assertIn('!memberAi->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT);', code)
        self.assertIn("f.raidIcons[i] = iconGroup->GetTargetIcon(i).GetRawValue();", code)
        immune = strip_comments(body(COMMANDS, "uint32 RunCcImmune(", "\n}\n"))
        self.assertIn("(templateMask & (1u << (mechanic - 1))) || creature->IsImmuneToMechanic(Mechanics(mechanic))", immune)

        # Kill-first units come from the bots' cached attackers lists: no grid or world scan (AGENTS.md).
        self.assertIn("ReadKillFirst(*slot.run.plan, lead, bots, f);", facts)
        kill = body(COMMANDS, "void ReadKillFirst(", "\n}\n")
        self.assertIn('ReadCachedGuidList(PlayerbotAIStorage::Instance().GetAI(bot), "attackers", guids);', kill)
        for scan in ("GetCreatureListWithEntryInGrid", "Cell::Visit", "VisitAllObjects", "HostileCreaturesAround"):
            self.assertNotIn(scan, kill)
        # The creatures channelling an aura at an engaged attacker (Hailar's Ritualists, r19-19z37) are
        # read off that attacker's own aura holders, inside the engaged block, and flagged for the policy.
        # Checked on the code with its // comments stripped, so a commented-out line does not pass.
        code = "\n".join(line.split("//", 1)[0].rstrip() for line in kill.splitlines())
        self.assertIn("fact.auraOnEngaged = auraOnEngaged;", code)
        self.assertEqual(code.count("std::unordered_set<uint64> auraWalked;"), 1)
        engaged = code.split("if (EngagedWithParty(unit, party))\n", 1)
        self.assertEqual(len(engaged), 2)
        self.assertNotIn("GetSpellAuraHolderMap", engaged[0])
        block = engaged[1].split("\n            }\n", 1)[0]
        walk = block.split("if (auraWalked.insert(unit->GetObjectGuid().GetRawValue()).second)\n", 1)
        self.assertEqual(len(walk), 2)  # each attacker's holders once per read, however many bots list it
        walk = walk[1]
        self.assertIn("for (auto const& entry : unit->GetSpellAuraHolderMap())", walk)
        self.assertIn("ObjectGuid const casterGuid = holder->GetCasterGuid();", walk)
        # The caster is looked up on the attacker's own map and must be in the world, not deleted,
        # and channelling at this unit now (a long buff or HoT left by a creature elsewhere is not).
        lookup = "Unit* caster = unit->GetMap()->GetUnit(casterGuid);"
        channel = "caster->GetChannelObjectGuid() != unit->GetObjectGuid())\n                            continue;"
        self.assertIn(lookup, walk)
        self.assertNotIn("lead->GetMap()", walk)
        self.assertIn("if (!caster || !caster->IsInWorld() || caster->IsDeleted() ||", walk)
        self.assertIn(channel, walk)
        # A caster already read off an attackers list gets the flag on its fact (code, after the
        # channel check); a new one is added with it. Only the aura caster is flagged.
        upgrade = ("if (seen.count(casterRaw))\n                        {\n"
                   "                            for (TortoiseBots::DungeonRun::UnitFact& known : f.killFirst)\n"
                   "                                if (known.guid == casterRaw)\n"
                   "                                    known.auraOnEngaged = true;\n"
                   "                            continue;")
        self.assertIn(upgrade, "\n".join(l for l in walk.splitlines() if l.strip()))
        self.assertIn("add(caster, true);", walk)
        self.assertLess(walk.index(lookup), walk.index(channel))
        self.assertLess(walk.index(channel), walk.index("known.auraOnEngaged = true;"))
        self.assertLess(walk.index(channel), walk.index("add(caster, true);"))
        self.assertEqual(code.count(", true);"), 1)
        self.assertIn("fact.engaged = EngagedWithParty(unit, party);", kill)
        # A ward is read off its engaged owner at once (totems and guardians), not when a cached list takes it in.
        self.assertIn("CONTROLLED_TOTEMS | CONTROLLED_GUARDIANS", kill)
        self.assertIn("EngagedWithParty(summoner, party);", kill)
        loader = (ROOT / "ai/playerbot/DungeonPlanMgr.cpp").read_text()
        self.assertIn("SELECT `plan`, `map`, `cc_priority`, `cc_always`, `kill_first` FROM `ai_dungeon_plan`", loader)
        self.assertIn("table.SetUnits(plan, fields[2].GetCppString(), fields[3].GetCppString(), fields[4].GetCppString())", loader)

    def test_crowd_control_keeps_to_the_runs_mark(self):
        cc = (ROOT / "ai/playerbot/strategy/values/CcTargetValue.cpp").read_text()
        # Never a totem or an NPC's summon (the healing ward), and in a run only the ordered mark.
        self.assertIn("bool const totem = static_cast<Creature*>(creature)->IsTotem();", cc)
        self.assertIn("TortoiseBots::BotCommands::IsRunKillFirst(bot, creature->GetEntry());", cc)
        self.assertIn("if (TortoiseBots::CcLeavesOut(totem, npcSummoned, inRun, runKillFirst))", cc)
        self.assertIn("inRun = TortoiseBots::BotCommands::IsInRunningDungeonRun(ai->GetBot());", cc)
        self.assertIn("if (inRun && !assignedTarget)\n            return;", cc)
        self.assertIn("RunSlotOf(bot, activeOnly)", body(COMMANDS, "RunningRunOf(Player* bot, bool activeOnly = false)", "\n}\n"))
        query = body(COMMANDS, "static DungeonRunSlot* RunSlotOf(Player* bot, bool activeOnly)", "\n}\n")
        self.assertIn(": it->second.run.state != TortoiseBots::DungeonRun::State::Running", query)

    def test_spells_at_a_totem_must_hit_it(self):
        spells = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.cpp").read_text()
        useful = body(spells, "bool CastSpellAction::isUseful()", "\n}\n")
        self.assertIn("static_cast<Creature*>(spellTarget)->IsTotem()", useful)
        self.assertIn("TortoiseBots::SpellWorksOnTotem(info->Effect, info->EffectApplyAuraName,", useful)
        self.assertIn("info->EffectImplicitTargetA, MAX_EFFECT_INDEX, info->IsPositiveSpell()))", useful)

    def test_walk_back_reads_facts_for_the_replayed_point(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("ai::PlanStep const* step = FactStep(slot.run);", facts)
        self.assertIn("ai::OwnedByLaterStep(*slot.run.plan, OwnerFrom(slot.run), unit->GetEntry(),", facts)
        self.assertNotIn("Current(slot.run)", facts)

    def test_ritual_answer_reaches_the_run(self):
        # The run's `ritual` step waits for the ritual order's one answer (#984): the group's reply
        # notes it for the run whose carrier (the lead bot) is a member, and a run's `ritual` order
        # sets the run's carrier like loot/open/use (only the ritual's carrier is always the lead bot).
        reply = body(COMMANDS, "void RitualReply(RitualGroup const& group", "\n}\n")
        self.assertIn("run->second.taskReply = ok ? 1 : 2;", reply)
        self.assertIn('order.compare(0, 7, "ritual ") == 0', COMMANDS)
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("case ai::PlanStepKind::Ritual:", facts)
        self.assertIn("lead->GetMap()->GetInstanceData()", facts)


    def test_open_use_carrier_is_pinned_while_its_order_is_out(self):
        # An open / use_item order's bot stays the carrier until the step ends: the facts of the
        # next ticks (busy, reply, the made item) are read from that bot, not re-picked from the
        # bags (Uldaman: the use consumes 7740/7741 in a non-lead bot's bags, PR #1078 review).
        task = body(COMMANDS, "Player* RunTaskBot(", "\n}\n")
        bags = body(COMMANDS, "std::vector<TortoiseBots::DungeonRun::BagFact> RunTaskBags(", "\n}\n")
        self.assertIn("bag.guid = bot->GetObjectGuid().GetRawValue();", bags)
        self.assertIn("OutstandingCarrierVerdict(slot.run.orderSent, slot.carrierRaw, bags, &name);", task)
        self.assertLess(task.index("OutstandingCarrierVerdict("), task.index("PickItemCarrier("))
        # Only with no order out is the carrier picked from the bags.
        self.assertIn("if (verdict == TortoiseBots::DungeonRun::CarrierVerdict::NoOrder)\n"
                      "        name = TortoiseBots::DungeonRun::PickItemCarrier(", task)
        # An order whose bot is gone is retired (orderSent and carrierRaw cleared) before the facts are read.
        retire = body(COMMANDS, "void RetireLostCarrier(", "\n}\n")
        self.assertIn("!= TortoiseBots::DungeonRun::CarrierVerdict::Retire)\n        return;", retire)
        self.assertIn("TortoiseBots::DungeonRun::RetireOrder(slot.run);", retire)
        self.assertIn("slot.carrierRaw = 0;", retire)
        self.assertIn("slot.taskReply = 0;", retire)
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        self.assertIn("RetireLostCarrier(slot, lead, bots);\n    TortoiseBots::DungeonRun::Facts facts;\n"
                      "    ReadRunFacts(slot, lead, bots, facts);", tick)
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        retire_order = body(policy, "inline void RetireOrder(Run& r)", "\n}\n")
        self.assertIn("r.orderSent = false;", retire_order)
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("Player* const carrier = RunTaskBot(slot, lead, bots, step, &f.itemsSplit);", facts)
        # A new order picks afresh: the previous order's bot is cleared before the pick.
        self.assertIn("slot.carrierRaw = 0;\n            slot.carrierRaw = RunTaskBot(slot, lead, bots,", COMMANDS)

    def test_open_proof_is_read_only_for_its_step(self):
        # open_proof: the proof is read only for the actual current step (not FactStep's walk-back
        # replayed point), and only when that step is an open_proof (the way ReadHoldDone reads its
        # own step before the facts' own step). The glue asks the policy's ProofStep helper for step,
        # scoped to the task-facts case: the old string-only pin passed with the read moved out of
        # the guard, so only the guarded read inside this block passes.
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        proof = facts.index("f.proofUp = OpenProofUp(lead, *step);")
        block = facts[facts.index("case ai::PlanStepKind::Loot:"):proof]
        self.assertIn("ProofStep(slot.run) == step", block)

    def test_open_proof_object_must_be_spawned(self):
        # A placed but despawned light is no proof: the object search uses EntryGameObjectCheck (isSpawned).
        proof = body(COMMANDS, "bool OpenProofUp(", "\n}\n")
        self.assertIn("EntryGameObjectCheck check(step.entries.front(), point, step.radius);", proof)
        check = body(COMMANDS, "class EntryGameObjectCheck", "\n};\n")
        self.assertIn("sServerFacade.isSpawned(go)", check)

    def test_open_proof_creature_must_be_alive(self):
        proof = body(COMMANDS, "bool OpenProofUp(", "\n}\n")
        self.assertIn("EntryLivingCreatureCheck check(step.entries.front(), point, step.radius);", proof)
        self.assertIn("creature->IsAlive()", COMMANDS)

    def test_open_proof_search_is_bounded(self):
        proof = body(COMMANDS, "bool OpenProofUp(", "\n}\n")
        self.assertEqual(proof.count("Cell::VisitGridObjects(point.x, point.y, map, searcher, step.radius)"), 2)

    def test_loader_reads_the_open_proof_kind(self):
        loader = (ROOT / "ai/playerbot/DungeonPlanMgr.cpp").read_text()
        self.assertIn("ParsePlanStepRowKind(fields[3].GetCppString(), step.kind, step.exactLaunch, step.openProof);", loader)

    def test_reload_compares_open_proof(self):
        reload = (ROOT / "ai/playerbot/DungeonPlanReloadPolicy.h").read_text()
        self.assertIn("a.openProof == b.openProof", reload)

    def test_use_on_is_wired_like_an_open(self):
        # use_on (Zul'Farrak's gong with the Mallet): its carrier is the bot that carries the step's `item`, picked, pinned and
        # retired as for an open, and its facts (carrier, item, busy, answer) are read in the open / use_item case. The order
        # it sends starts with "use ", so the dispatch picks the carrier afresh for it. The policy dispatches the kind.
        bags = body(COMMANDS, "std::vector<TortoiseBots::DungeonRun::BagFact> RunTaskBags(", "\n}\n")
        self.assertIn("step.kind == ai::PlanStepKind::Open || step.kind == ai::PlanStepKind::UseOn ? step.item : step.entry", bags)
        task = body(COMMANDS, "Player* RunTaskBot(", "\n}\n")
        self.assertIn("step->kind != ai::PlanStepKind::UseOn)\n        return lead;", task)
        retire = body(COMMANDS, "void RetireLostCarrier(", "\n}\n")
        self.assertIn("step->kind != ai::PlanStepKind::UseOn))\n        return;", retire)
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("case ai::PlanStepKind::UseItem:\n        case ai::PlanStepKind::UseOn:", facts)
        self.assertIn("f.carrierHasItem = step->item && carrier->HasItemCount(step->item);", facts)
        self.assertIn('order.compare(0, 4, "use ") == 0', COMMANDS)
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        self.assertIn("case PlanStepKind::UseOn:\n            UseOnStep(r, f, *s, order);", policy)
        # The review of 2026-10-21: the answer carries whether the act had started and whether the object reacted (its state
        # before the cast is kept for a use on a game object), the slot clears both with each new order, and the facts read the
        # summoned boss (the boss row after the use_on) alive or dead around the lead bot.
        note = body(COMMANDS, "void NoteRunTaskReply(CommanderTask const& task, bool ok, std::string const& text)", "\n}\n")
        self.assertIn("it->second.taskActStarted = task.actStarted;", note)
        self.assertIn("ObjectEffectOf(task, ai) == RemoteCommander::ObjectEffect::Reacted", note)
        begin = body(COMMANDS, "ActStart BeginUseAct(", "\n}\n")
        self.assertLess(begin.index("task.baseline = PackGoState(go);"), begin.index('ExecuteQuietAction(ai, "commander use"'))
        self.assertIn("slot.taskActStarted = false;\n            slot.taskObjectReacted = false;", COMMANDS)
        self.assertIn("slot.taskActStarted = false;\n    slot.taskObjectReacted = false;", retire)
        self.assertIn("f.taskActStarted = slot.taskActStarted;", facts)
        self.assertIn("f.objectReacted = slot.taskObjectReacted;", facts)
        self.assertIn("f.summonSeen = !found.empty();", facts)
        use_on = body(policy, "inline void UseOnStep(", "\n}\n")
        self.assertIn("TaskResendable(f.taskText) && !f.taskActStarted && r.taskResends < kUseOnResends", use_on)
        reset = body(policy, "inline void ResetStep(Run& r)", "\n}\n")
        self.assertNotIn("useOnRang", reset)
        self.assertIn('order = "use " + std::to_string(s.item) + " " + f.carrier + " " + EntryArg(s.entry, s.x, s.y, s.z);', use_on)

    def test_live_fixes_2026_10_16_are_wired(self):
        # 1. `cmd move` takes the floor at or below the point before a surface above it (a low ceiling).
        move = body(COMMANDS, "static bool CommanderMove(", "\n}\n")
        self.assertIn("RemoteCommander::ChooseFloorZ(point.z, belowZ,", move)
        # 1b. Karazhan Crypt kc316 step 292: a water point is not lifted out of the water onto a surface the
        # lead bot cannot reach. The glue reads the point's liquid (the swim line's own test) only when the
        # search from above counts; the core is asked for the raised point's path and the water point's
        # swim depth only inside WaterSnapAsksPath's rare branch (Codex review of 3ad0950d: a pier keeps the
        # old snap when the lead bot has a path onto it); every fact is passed to the policy.
        self.assertIn("aboveZ, aboveZ > INVALID_HEIGHT, pointInWater, waterLevel, waterSwimmable, raisedReachable);",
                      move)
        above = move[move.index("if (!RemoteCommander::FloorBelowUsable(point.z, belowZ"):
                     move.index("float const snapped = RemoteCommander::ChooseFloorZ(")]
        self.assertIn("ai::kSwimLiquidRequest, &liquid));", above)
        self.assertIn("pointInWater = ai::SwimSampleInWater(status,", above)
        self.assertIn("waterLevel = liquid.level;", above)
        rare_at = above.index("if (RemoteCommander::WaterSnapAsksPath(point.z, belowZ")
        open_at = above.index("{", rare_at)
        depth, close_at = 0, None
        for i in range(open_at, len(above)):
            depth += {"{": 1, "}": -1}.get(above[i], 0)
            if depth == 0:
                close_at = i
                break
        rare = above[open_at:close_at + 1]  # the rare branch's own block, brace-matched
        self.assertIn("waterSwimmable = lead->GetMap()->GetTerrain()->IsSwimmable(point.x, point.y, point.z);", rare)
        self.assertIn("raisedPath.calculate(point.x, point.y, aboveZ, false);", rare)
        # ... and only for a swimmable water point (a shallow one keeps the old rule without a path query).
        self.assertIn("if (waterSwimmable)\n", rare)
        self.assertLess(rare.index("if (waterSwimmable)\n"), rare.index("PathFinder raisedPath(lead);"))
        self.assertIn("raisedReachable = RemoteCommander::RaisedFloorPathUsable(raisedPathType, raisedEndOffset, atLead);",
                      rare)
        # The raised point's path query happens nowhere else: once in the function, inside the rare branch.
        self.assertEqual(move.count("PathFinder raisedPath(lead);"), 1)
        self.assertEqual(move.count("WaterSnapAsksPath("), 1)
        self.assertLess(move.index("if (RemoteCommander::WaterSnapAsksPath("), move.index("PathFinder raisedPath(lead);"))
        self.assertLess(move.index("PathFinder raisedPath(lead);"), move.index("float const snapped = RemoteCommander::ChooseFloorZ("))
        self.assertIn('#include "../ai/playerbot/SwimPathPolicy.h"', COMMANDS)
        # 2. A follower off the lead bot's line walks its own; the refusal still names the bot for the summon.
        direct = body(COMMANDS, "static bool CommanderMoveDirect(ChatHandler* handler, CommanderOrder const& order,\n"
                      "    RemoteCommander::MovePoint const& point, std::string const& intent)\n{", "\n}\n")
        join = body(COMMANDS, "static RemoteCommander::DirectJoin DirectFollowerJoin(", "\n}\n")
        self.assertIn("pathComplete, pathLength, ownLine);", join)
        self.assertIn("DirectFollowerJoin(bot, map, leadX, leadY, leadZ, point.x, point.y, ground, horizontal, route);",
                      direct)
        self.assertIn("join == RemoteCommander::DirectJoin::Own", direct)
        self.assertIn('" is not with " + lead->GetName()', direct)

    def test_direct_gate_asks_the_steps_own_test(self):
        # r12-19z29 Uldaman leg 5: the gate before a direct step passed on the 20 yd ground distance while the step
        # refused the bot too-far. The facts read the step's own test (DirectFollowerJoin) per follower while a
        # direct step is under the gate, and the policy's gate asks it.
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("DirectJoinWanted(slot.run, joinX, joinY, joinZ);", facts)
        self.assertIn("f.directJoinRead = joinWanted;", facts)
        self.assertIn("f.directJoinSupported = true;", facts)
        # Cost: no path for a follower past kGatherYd (its distance fails the gate already); none during a walk
        # (DirectJoinWanted is false in Phase::Walk: test_dungeon_run_policy TestDirectOrderWaitsForTheRead).
        self.assertIn("if (joinWanted && bot != lead && yd <= kGatherYd && bot->IsInWorld()", facts)
        self.assertIn("member.withLead = DirectFollowerJoin(bot, joinMap, f.leadX, f.leadY, f.leadZ, joinX, joinY, "
                      "joinGround,", facts)
        self.assertLess(facts.index("member.withLead = DirectFollowerJoin("), facts.index("f.members.push_back(member);"))
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        move = body(policy, "inline void MoveStep(", "\n}\n")
        self.assertIn("s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && !DirectGathered(f)", move)
        self.assertIn('yd from the lead bot)", true);', move)
        walk_back = body(policy, "inline bool WalkBack(", "\n}\n")
        self.assertIn("if (direct && r.phase != Phase::Walk && HoldForDirectJoin(r, f))", walk_back)
        self.assertIn("if (s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && HoldForDirectJoin(r, f))", move)
        self.assertIn("direct && r.phase != Phase::Walk && !DirectGathered(f)", walk_back)
        self.assertIn('yd from the lead bot)", true);', walk_back)
        # 4. A door whose lock needs no key and no skill opens on a plain use.
        use = (ROOT / "ai/playerbot/strategy/actions/UseItemAction.cpp").read_text()
        self.assertIn("TortoiseBots::DoorLock::OpensOnPlainUse(lockTypes, lockIndex)", use)
    def test_uldaman_run3_hold_fixes_are_wired(self):
        # A hold's done condition is read on every tick, before the facts' own step (in a walk back too) ...
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertLess(facts.index("ReadHoldDone(slot.run.plan->steps[slot.run.step], lead, f);"),
                        facts.index("ai::PlanStep const* step = FactStep(slot.run);"))
        # ... and while the run is paused, before the paused early return.
        tick = body(COMMANDS, "void TickDungeonRun(", "\n}\n")
        self.assertLess(tick.index("TortoiseBots::DungeonRun::HoldDoneWhilePaused(slot.run, doneFacts)"),
                        tick.index("if (slot.run.state != TortoiseBots::DungeonRun::State::Running || !commander)"))
        # The party's places on a hold spot follow every tick and are let go with the run's hold.
        self.assertIn("SyncHoldStands(commander, lead, bots, it->second, order);", tick)
        release = body(COMMANDS, "void ReleaseRunHold(", "\n}\n")
        self.assertIn("slot.placedRaw.clear();", release)
        stands = body(COMMANDS, "void SyncHoldStands(", "\n}\n")
        self.assertIn("if (stand && (!placed || newPlaces || drifted))", stands)
        pause = body(COMMANDS, 'if (verb == "pause")', "else if")
        self.assertIn('PausePlayer(slot.run, "paused by .bot run pause");', pause)
        self.assertIn("member.ranged = PlayerbotAI::IsHeal(bot)", facts)
        self.assertIn("unit.x = creature->GetPositionX();", facts)


    def test_small_batch_2026_10_17_is_wired(self):
        # 1. `cmd move` hands the policy how far the path's end is from the lead bot (an incomplete path that goes nowhere).
        move = body(COMMANDS, "static bool CommanderMove(", "\n}\n")
        self.assertIn("facts.endFromLead = std::sqrt(lx * lx + ly * ly + lz * lz);", move)
        self.assertIn("facts.fromRunLeader = s_runIssuing;", move)
        # 2. The run's time limit ends the run with its own note; `run stop` takes a reason.
        tick = body(COMMANDS, "void TickDungeonRun(", "\n}\n")
        self.assertIn("RunTimeLimitReached(slot.elapsedMs, sPlayerbotAIConfig.dungeonRunMaxMinutes)", tick)
        self.assertIn("RunTimeLimitNote(sPlayerbotAIConfig.dungeonRunMaxMinutes)", tick)
        self.assertIn("EndRunEarly(slot.run, RunStopNote(rawArg));", COMMANDS) # the note, via EndRunEarly
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        self.assertIn('"AiPlayerbot.DungeonRunMaxMinutes", 180', config)
        self.assertIn("AiPlayerbot.DungeonRunMaxMinutes = 180", template)
        # 3. The reaction pass reads a queued chat command for a masterless bot (talents / spells answers).
        engine = (ROOT / "ai/playerbot/strategy/ReactionEngine.cpp").read_text()
        self.assertIn("ReactionPassForced(ai->HasRealPlayerMaster(), ai->QueuedChatCommandCount())", engine)
        # 4. `run start ... from ...` resolves the step and guards a start partway.
        start = body(COMMANDS, "bool HandleDungeonRun(", "\nvoid UpdateRemoteCommanders(")
        self.assertIn("ResolveStartStep(*plan, startArgs.from, startArgs.value, startStep, &movedBack)", start)
        self.assertIn("CheckPartialStart(OnInstanceMap(lead), members.size(), withLead)", start)
        self.assertIn("slot.run = Start(plan, startStep);", start)

    def test_rez_hold_is_wired_behind_its_key(self):
        # AiPlayerbot.DungeonRunRezHold (default 0): the glue reads the rez roster only with the key on and someone
        # dead; the policy asks RezHoldPolicy.h only when the facts carry it, else the old fixed wait runs.
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        header = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        self.assertIn('dungeonRunRezHold = config.GetBoolDefault("AiPlayerbot.DungeonRunRezHold", false);', config)
        self.assertIn("bool dungeonRunRezHold;", header)
        self.assertIn("AiPlayerbot.DungeonRunRezHold = 0", template)
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunRezHold && f.dead > 0)\n        ReadRezRoster(lead, bots, f);", COMMANDS)
        roster = body(COMMANDS, "void ReadRezRoster(", "\n}\n")
        self.assertIn("f.rezHold = true;", roster)
        # Rebirth's cooldown is a category cooldown and its seed a reagent; a cast under way holds the budget.
        self.assertIn("bot->HasSpellCategoryCooldown(spell->Category)", roster)
        self.assertIn("spell->Reagent[k] > 0 && !bot->HasItemCount(", roster)
        self.assertIn("f.rezInProgress = true;", roster)
        self.assertIn("bot->IsRessurectRequested()", roster)
        self.assertIn("m.blocksDruid =", roster)
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        self.assertIn("if (f.rezHold && !r.rezBodyUnreachable)", policy)
        self.assertIn("else if (f.rezzer && !r.reviveSent && !r.rezBodyUnreachable && r.regroupMs < kPartyRezWaitMs)", policy)
        self.assertIn("TortoiseBots::RezHold::Decide(hold, f.rezMembers)", policy)
        for doc in ("docs/server/settings/dungeons-raids.md",):
            self.assertIn("AiPlayerbot.DungeonRunRezHold", (ROOT / doc).read_text())

    def test_loot_ffa_is_wired_behind_its_key(self):
        # AiPlayerbot.DungeonRunLootFfa (default 0): the glue reads the group's loot method and the lead bot's
        # leadership only with the key on; the loot method goes back whenever the run is not running.
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        header = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        self.assertIn('dungeonRunLootFfa = config.GetBoolDefault("AiPlayerbot.DungeonRunLootFfa", false);', config)
        self.assertIn("bool dungeonRunLootFfa;", header)
        self.assertIn("AiPlayerbot.DungeonRunLootFfa = 0", template)
        facts = body(COMMANDS, "void ReadRunFacts(", "\n    f.leadAlive")
        self.assertIn("f.lootFfaOn = sPlayerbotAIConfig.dungeonRunLootFfa;\n    if (f.lootFfaOn || slot.run.lootFfaLive)", facts)
        self.assertIn("f.lootMethod = static_cast<int>(leadGroup->GetLootMethod());", facts)
        self.assertIn("f.leadLeadsGroup = leadGroup->IsLeader(lead->GetObjectGuid());", facts)
        restore = body(COMMANDS, "void RestoreRunLootMethod(", "\n}\n")
        self.assertIn("run.lootFfaLive = false;", restore)
        self.assertIn("group->IsLeader(lead->GetObjectGuid())", restore)
        self.assertIn("static_cast<int>(group->GetLootMethod()) != RemoteCommander::kLootFfa", restore)
        self.assertIn("HandleLootMethodOpcode(packet)", restore)
        # Every way a run stops being a running one restores: pause / stop / time limit (ReleaseRunHold),
        # an unbind (ReleaseBinding), the key switched off.
        hold = body(COMMANDS, "void ReleaseRunHold(", "\n    if (slot.heldRaw.empty()")
        self.assertIn("RestoreRunLootMethod(leadRaw, slot.run);", hold)
        release = body(COMMANDS, "uint32 ReleaseBinding(", "RemoteCommander::Binding removed =")
        self.assertIn("RestoreRunLootMethod(standing->leadGuid, run->second.run);", release)
        off = body(COMMANDS, "// Switched off by a config reload", "s_dungeonRunMs = 0;")
        # The key switched off lets each run go through ReleaseRunHold too (its first line restores, above).
        self.assertIn("ReleaseRunHold(FindCharacter(entry.first), standing->leadGuid, entry.second);", off)
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        self.assertIn("LootFfaWanted(r, f, s)", policy)
        self.assertIn('order = "lootmethod ffa";', policy)
        self.assertIn('std::string("lootmethod ") + RemoteCommander::LootMethodName(r.lootRestore)', policy)
        self.assertIn("AiPlayerbot.DungeonRunLootFfa", (ROOT / "docs/server/settings/dungeons-raids.md").read_text())

    def test_uldaman_run4_fixes_are_wired(self):
        # The tank's reach for a pull stops once a mob has aggro: the facts are read in fights, the policy asks, the
        # glue ends the lead bot's pull.
        guard = body(COMMANDS, "void ReadGuardFacts(", "\n}\n")
        self.assertIn("ReadReachFacts(lead, leadAi, bots, f);", guard)
        reach = body(COMMANDS, "void ReadReachFacts(", "\n}\n")
        self.assertIn("f.reachAggro", reach)
        self.assertIn("f.leadReaching", reach)
        self.assertIn("pull->IsCommandActive()", reach)
        stop = body(COMMANDS, "void StopLeadReach(", "\n}\n")
        self.assertIn('"pull end"', stop)
        tick = body(COMMANDS, "void TickDungeonRun(", "\n}\n")
        self.assertIn("if (it->second.run.stopReachDue)", tick)
        self.assertIn("StopLeadReach(commander, lead);", tick)
        # The healer: held on its stand in the run leader's pulls, behind the tank, kept to heals for the run.
        self.assertIn("context.runLeader = true;", COMMANDS)
        self.assertIn("HealerKeptToHeals(running, true, bot == lead, PlayerbotAI::IsHeal(bot))", COMMANDS)
        ctx = (ROOT / "commands/BotCommandContext.cpp").read_text()
        pause = body(ctx, "void PausePartyDpsForPull(", "\n}\n")
        self.assertIn("(healer && !context.runLeader)", pause)
        self.assertIn("context.noteHealOnly(bot);", pause)
        sync = body(COMMANDS, "void SyncRunHealOnly()\n{", "\n}\n")
        self.assertIn("CommandedBots(group, lead)", sync)
        self.assertIn("HealerKeptToHeals(running, true, bot == lead, PlayerbotAI::IsHeal(bot))", sync)
        self.assertNotIn("healerAi->SetRunHealOnly(true);", tick)
        self.assertIn("SyncRunBackPassive();\n    SyncRunHealOnly();", COMMANDS)
        self.assertIn("ai->SetRunHealOnly(true);", pause)
        self.assertIn("context.healerStand(bot, hx, hy, hz)", pause)
        trig = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()
        self.assertIn("gate.healOnly = ai->RunHealOnly();", trig)
        self.assertIn("if (ai->RunHealOnly())\n        return false;", trig)
        combat = (ROOT / "ai/playerbot/strategy/generic/CombatStrategy.cpp").read_text()
        self.assertIn("if (ai->RunHealOnly() && PlayerbotAI::IsHeal(ai->GetBot()))\n        return false;", combat)
        # A refused direct step names the dead bot, else the farthest, not the first one checked.
        direct = body(COMMANDS, "static bool CommanderMoveDirect(ChatHandler* handler, CommanderOrder const& order,\n"
                      "    RemoteCommander::MovePoint const& point, std::string const& intent)\n{", "\n}\n")
        self.assertIn("refusedDead", direct)
        self.assertIn("horizontal > refusedYd", direct)


    def test_pull_step_patrollers_reach_the_policy(self):
        # DungeonRun::WaitForPatroller (Uldaman r10 step 66): a pull step's living entries on a waypoint patrol outside
        # the radius, within kPatrolSeeYd beyond it, none a later step owns; the grid search of a pull step reaches that far.
        read = body(COMMANDS, "void ReadRunFacts(DungeonRunSlot const& slot,", "\n}\n")
        add = read[read.index("auto addPatroller"):read.index("f.patrollers.push_back(unit);")]
        self.assertIn("bool const pullStep = step->kind == ai::PlanStepKind::Pull;", read)
        self.assertIn("creature->GetDefaultMovementType() != WAYPOINT_MOTION_TYPE", add)
        self.assertIn("fromPoint <= step->radius || fromPoint > step->radius + kPatrolSeeYd", add)
        self.assertIn("ai::OwnedByLaterStep(*slot.run.plan, slot.run.step, creature->GetEntry(),", add)
        self.assertIn("pullStep ? std::min(toPoint + step->radius + kPatrolSeeYd, 2.0f * kSeeYd) : range", read)
        self.assertIn("unit.z = creature->GetPositionZ();", add)
        self.assertIn("unit.distSpot = fromPoint;", add)
        # Only living ones: the corpse branch lists none (the policy never sees a dead patroller).
        self.assertEqual(read.count("addPatroller(creature, fromPoint)"), 1)
        alive = read.index("if (!creature->IsAlive())")
        self.assertLess(alive, read.index("addPatroller(creature, fromPoint)"))
        self.assertNotIn("addPatroller", read[alive:read.index("if (fromPoint > step->radius)", alive)])
        # The wait and the pull are the policy's: the glue never orders a patroller.
        self.assertNotIn("pullback", add)

    def test_aggro_estimate_is_a_read_side_fact_behind_its_key(self):
        # AiPlayerbot.DungeonRunAggroEstimate (default 0): the glue reads the estimate, the policy decides.
        read = body(COMMANDS, "void ReadRunFacts(DungeonRunSlot const& slot,", "\n}\n")
        self.assertIn("f.joinerCeiling = sPlayerbotAIConfig.dungeonRunAggroEstimate ? "
                      "sPlayerbotAIConfig.dungeonRunAggroCeiling : 0;", read)
        self.assertIn("if (step->kind == ai::PlanStepKind::Pull)\n"
                      "                ReadPullJoiners(lead, bots, *step, toPoint, f.joinerCeiling, slot.run.joinerHeld, f);", read)
        joiners = body(COMMANDS, "void ReadPullJoiners(", "\n}\n")
        self.assertIn("if (!ceiling || f.units.empty())\n        return;", joiners)
        self.assertIn("ai::EstimateAggro(mobs, it->second, params)", joiners)
        self.assertIn("ai::JoinersOverCeiling(estimate, ceiling);", joiners)
        self.assertIn("sCreatureLinkingMgr.GetLinkedTriggerInformation(", joiners)
        self.assertIn("group->HasGroupFlag(OPTION_AGGRO_TOGETHER)", joiners)
        self.assertIn("creatures[i]->IsWithinLOS(params.engageX, params.engageY, params.engageZ + 1.0f, true, 0.0f)", joiners)
        self.assertIn("held.count(unit.guid)", joiners)
        self.assertIn("&& !toMaster", joiners)
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        self.assertIn('"AiPlayerbot.DungeonRunAggroEstimate", false', config)
        self.assertIn('"AiPlayerbot.DungeonRunAggroCeiling", 6', config)
        self.assertIn("AiPlayerbot.DungeonRunAggroEstimate = 0", template)
        self.assertIn("AiPlayerbot.DungeonRunAggroCeiling = 6", template)
        header = (ROOT / "ai/playerbot/PullAggroEstimatePolicy.h").read_text()
        self.assertIn("jrad7/mod-dungeon-clear", header)
        self.assertIn("@ da0bdfbc (AGPL-3.0-or-later;", header)
        self.assertIn("Copyright (C) 2026+ Jared Wright and", header)

    def test_pull_stand_is_wired_behind_its_key(self):
        # AiPlayerbot.DungeonRunPullStand (default 0): the glue reads sight, the shot range and the stand only with
        # the key on; the policy holds the pullback for them only when the facts say the key is on.
        read = body(COMMANDS, "void ReadRunFacts(DungeonRunSlot const& slot,", "\n}\n")
        self.assertIn("f.pullStand = sPlayerbotAIConfig.dungeonRunPullStand;", read)
        # The step units (pull/boss) and the hop units carry losLead, from the lead bot's own sight test.
        self.assertIn("if (f.pullStand || StandThisBoss(slot))\n                        unit.losLead = lead->IsWithinLOSInMap(creature);\n"
                      "                    f.units.push_back(unit);", read)
        self.assertIn("if (f.pullStand)\n                    fact.losLead = lead->IsWithinLOSInMap(unit);\n"
                      "                f.hopUnits.push_back(fact);", read)
        self.assertTrue(read.rstrip().endswith("ReadRunNeedFacts(slot, lead, f);"))
        stand = body(COMMANDS, "void ReadPullStand(DungeonRunSlot const& slot,", "\n}\n")
        self.assertIn("if ((!f.pullStand && !standStep) || !lead->IsAlive() || !lead->IsInWorld())\n        return;", stand)
        # leadShotYd is the module's PullStrategy::GetRange(), for a ranged weapon shot only (0 for spell and body pulls).
        self.assertIn('if (pull && pull->GetPullActionName() == "shoot")\n        f.leadShotYd = pull->GetRange();',
                      stand)
        self.assertNotIn("f.leadShotYd = ", stand.replace("f.leadShotYd = pull->GetRange();", ""))
        # The stand: only for Run::shotStandFor, and the path search comes after that check.
        guard = stand.index("uint64 const guid = slot.run.shotStandFor;\n    if (!guid || f.leadShotYd <= kShotMarginYd)\n"
                            "        return;")
        self.assertGreater(stand.index("PathFinder path(lead);"), guard)
        self.assertIn("map->GetCreature(ObjectGuid(guid))", stand)
        self.assertIn("map->isInLineOfSight(point.x, point.y, point.z + leadHeight, goal.x, goal.y, goal.z + unitHeight, true)",
                      stand)
        self.assertIn("ShotStandOnPath(points, goal, f.leadShotYd - kShotMarginYd, sees);", stand)
        self.assertIn("f.shotStandGuid = guid;", stand)
        # The policy: PullUnit's pullback passes through the gate first; HoldForShot is off without the key or a range.
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        pull = body(policy, "inline void PullUnit(", "\n}\n")
        gate = pull.index("HoldForShot(r, f, u, dt, order))\n            return;")
        self.assertLess(pull.index("if (f.pullStand && r.standPullShotGuid != u.guid)\n"), gate)
        self.assertLess(gate, pull.index('order = "pullback " + std::to_string(u.guid);'))
        self.assertLess(gate, pull.index("++tries;"))
        on = body(policy, "inline bool PullStandOn(", "\n}\n")
        self.assertIn("return f.pullStand && f.leadShotYd > kShotMarginYd && !InEvent(r);", on)
        # The hold ends with any tick the gate did not run on (no stale stand request for the glue).
        tick = body(policy, "inline std::string Tick(", "\n}\n")
        self.assertIn("r.shotGateRan = false;", tick)
        self.assertIn("if (!r.shotGateRan)\n        EndShotHold(r);", tick)
        hold = body(policy, "inline bool HoldForShot(", "\n}\n")
        self.assertIn("if (!PullStandOn(r, f))", hold)
        self.assertIn("r.shotStandFor = u.guid;", hold)
        # Every pull caller hands its tick length to the stand walk.
        self.assertIn("CcPick const ccPick = PickCcOrder(r, f, pick->guid);\n        PullUnit(r, f, *pick, order, ccPick.unit, false, dt);", policy)
        self.assertIn("PullUnit(r, f, *hopUnit, order, nullptr, false, dt, true);", policy)
        self.assertIn("PullUnit(r, f, *hopUnit, order, nullptr, true, dt);", policy)
        # PLAN-ZF-NOFIGHT: a move's and the walk back's pulls are hop pulls; a hop unit the tank cannot shoot is
        # left out with no order (after the stand gate, before any pullback). The run-wide set holds hop units
        # only, is read only by the two hop picks, is cleared by every regroup, and loses a unit that attacks.
        self.assertIn("hop = hop || walkBack;", pull)
        nohop = pull.index("if (hop && PullStandOn(r, f) && !ShootableFromLead(f, u))")
        self.assertLess(gate, nohop)
        self.assertLess(nohop, pull.index('order = "pullback " + std::to_string(u.guid);'))
        self.assertIn("r.pullHop = hop;", pull)
        reset = body(policy, "inline void ResetStep(", "\n}\n")
        self.assertIn("r.passed.clear();", reset)
        self.assertNotIn("neverCame", reset)
        await_pull = body(policy, "inline void AwaitPull(", "\n}\n")
        self.assertIn("if (r.pullHop)\n        r.neverCame.insert(r.pullGuid);", await_pull)
        self.assertEqual(policy.count("PickPull(r, f.hopUnits, 0, true)"), 2)
        self.assertEqual(policy.count("r.neverCame.count("), 1)
        regroup = body(policy, "inline void BeginRegroup(", "\n}\n")
        self.assertIn("r.neverCame.clear();", regroup)
        tick_body = body(policy, "inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)\n{",
                         "\n}\n")
        self.assertIn("ForgetUnitsThatCame(r, f);", tick_body)
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        header = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        self.assertIn('dungeonRunPullStand = config.GetBoolDefault("AiPlayerbot.DungeonRunPullStand", false);', config)
        self.assertIn("bool dungeonRunPullStand;", header)
        self.assertIn("AiPlayerbot.DungeonRunPullStand = 0", template)
        for doc in ("docs/server/settings/dungeons-raids.md", "docs/play/dungeon-run-leader.md"):
            self.assertIn("AiPlayerbot.DungeonRunPullStand", (ROOT / doc).read_text())


    def test_hold_follows_the_tank_and_dead_gossip_npc_is_named(self):
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        # 1. A hold's fight lets the places go when the tank is off the spot; the glue's existing release
        #    (SyncHoldStands -> FormUpOnLead) is what makes the placed bots follow it.
        fight = body(policy, "        ReleaseHoldStands(r, f);\n", "        if (CheckGuard(r, f, dt, order))")
        self.assertIn("CheckReach(r, f);", fight)
        release = body(policy, "inline bool ReleaseHoldStands(", "\n}\n")
        self.assertIn("++r.holdStandsGen;", release)
        self.assertIn("r.holdPlaced = false;", release)
        self.assertIn("farthest > kHoldFollowRangedYd", release)
        self.assertIn("fromSpot > kHoldReturnYd", release)
        self.assertIn("++r.holdFarTicks < kHoldFollowTicks", release)
        self.assertIn("constexpr float kHoldFollowRangedYd = 30.0f;", policy)
        stands = body(COMMANDS, "void SyncHoldStands(", "\n}\n")
        self.assertIn("else if (!stand && placed)", stands)
        self.assertIn("FormUpOnLead(commander, lead, released, false);", stands)
        self.assertIn("if (bot->IsAlive() && run.state == TortoiseBots::DungeonRun::State::Running)", stands)
        # 2. The module names a dead gossip NPC in the no-npc text, and the policy reads that wording.
        gossip = body(COMMANDS, "static bool CommanderGossip(", "\n}\n")
        no_npc = body(gossip, "if (!target.creature)", "return true;")
        self.assertIn("DeadGossipNpcText(bot, request.entry)", no_npc)
        self.assertIn("DeadGossipNpcText(bot, task.gossip.entry)", COMMANDS)
        text = body(COMMANDS, "std::string DeadGossipNpcText(", "\n}\n")
        self.assertIn("!creature->IsAlive()", body(COMMANDS, "class EntryDeadCreatureCheck", "};"))
        self.assertIn(") lies dead ", text)
        self.assertIn("GetRespawnDelay() == 0", text)
        self.assertIn('constexpr const char* kGossipNpcDeadMark = "lies dead";', policy)
        refused = body(policy, "inline void OrderRefused(", "\n}\n")
        self.assertIn("GossipNpcDeadNote(", refused)
        gossip_step = body(policy, "inline void GossipStep(", "\n}\n")
        self.assertIn("GossipNpcDeadNote(what, f.taskText)", gossip_step)


    def test_walk_back_after_won_fight_key_is_read_with_the_config(self):
        # AiPlayerbot.DungeonRunWalkBackAfterWonFight (default 0): one of the run leader's keys, read by
        # LoadDungeonRunKeys (Initialize and a `.reload config`) and handed to the policy on every tick by
        # ReadRunFacts, so no copy of it lives in a run.
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        header = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        keys = body(config, "void PlayerbotAIConfig::LoadDungeonRunKeys()\n{", "\n}\n")
        self.assertIn('dungeonRunWalkBackAfterWonFight = config.GetBoolDefault("AiPlayerbot.DungeonRunWalkBackAfterWonFight", false);', keys)
        self.assertIn("bool dungeonRunWalkBackAfterWonFight;", header)
        self.assertIn("AiPlayerbot.DungeonRunWalkBackAfterWonFight = 0", template)
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f = Facts();", facts)
        self.assertIn("f.walkBackAfterWonFight = sPlayerbotAIConfig.dungeonRunWalkBackAfterWonFight;", facts)
        self.assertIn("bool walkBackAfterWonFight = false;", policy)
        self.assertIn("else if (!GuardWalkBackWanted(r, f, FactStep(r) ? *FactStep(r) : *s))", policy)
        # The suppression follows the pull and boss steps' own end rule (Codex review of 7537eab8): one predicate.
        wanted = body(policy, "inline bool GuardWalkBackWanted(", "\n}\n")
        self.assertIn("return FightStepEnd(r, f, s) == FightEnd::NotYet;", wanted)
        self.assertNotIn("OpenUnits", wanted)
        fight = body(policy, "inline void FightStep(", "\n}\n")
        self.assertIn("FightEnd const end = FightStepEnd(r, f, s);", fight)
        self.assertNotIn("bossDeadNearSpot ||", fight)
        for doc in ("docs/server/settings/dungeons-raids.md", "docs/play/dungeon-run-leader.md"):
            self.assertIn("AiPlayerbot.DungeonRunWalkBackAfterWonFight", (ROOT / doc).read_text())

    def test_run_leader_keys_follow_a_config_reload(self):
        # `.reload config` (ChatHandler::HandleReloadConfigCommand -> World::LoadConfigSettings(true) ->
        # WORLDHOOK_ON_AFTER_CONFIG_LOAD) reaches BotHostAdapter::OnAfterConfigLoad; it only marks the run leader's
        # keys due, and the next world tick (OnUpdate) reads them again, through the same function Initialize uses.
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        header = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        host = (ROOT / "host/BotHostAdapter.cpp").read_text()
        keys = body(config, "void PlayerbotAIConfig::LoadDungeonRunKeys()\n{", "\n}\n")
        init = body(config, "bool PlayerbotAIConfig::Initialize()\n{", "\n}\n")
        reload = body(config, "bool PlayerbotAIConfig::ReloadDungeonRunKeys()\n{", "\n}\n")
        names = re.findall(r'"(AiPlayerbot\.Dungeon(?:SelfClear|Run\w+))"', config)
        self.assertGreaterEqual(len(names), 8)
        for name in names:
            # Every run leader key is read in LoadDungeonRunKeys and nowhere else (no second parse).
            self.assertIn('"' + name + '"', keys)
            self.assertEqual(config.count('"' + name + '"'), 1, name)
            self.assertNotIn('"' + name + '"', init)
        self.assertIn("LoadDungeonRunKeys();", init)
        self.assertLess(init.index('enabled = config.GetBoolDefault("AiPlayerbot.Enabled", false);'),
                        init.index("LoadDungeonRunKeys();"))
        self.assertIn("if (!enabled)\n        return false;", reload)
        self.assertIn("if (!config.Reload())", reload)
        self.assertLess(reload.index("if (!config.Reload())"), reload.index("LoadDungeonRunKeys();"))
        self.assertIn("void LoadDungeonRunKeys();", header)
        self.assertIn("bool ReloadDungeonRunKeys();", header)
        on_load = body(host, "void BotHostAdapter::OnAfterConfigLoad(bool reload)\n{", "\n}\n")
        self.assertIn("if (reload)\n        s_dungeonRunKeysDue.store(true);", on_load)
        self.assertNotIn("ReloadDungeonRunKeys", on_load)  # world thread only
        on_update = body(host, "void BotHostAdapter::OnUpdate(uint32 diff)\n{", "\n}\n")
        self.assertIn("if (s_dungeonRunKeysDue.exchange(false))\n        sPlayerbotAIConfig.ReloadDungeonRunKeys();", on_update)
        self.assertIn("WORLDHOOK_ON_AFTER_CONFIG_LOAD", host)
        self.assertEqual(host.count("ReloadDungeonRunKeys"), 1)
        # Switched off by a reload, every run ends as `.bot run stop` ends one: its held bots are let go
        # (ReleaseRunHold, which also restores a loot detour's method), not left on stay at hold stands.
        off = body(COMMANDS, "    // Switched off by a config reload: every run ends, with a last status line.",
                   "for (uint64 raw : switchedOff)")
        self.assertIn("ReleaseRunHold(FindCharacter(entry.first), standing->leadGuid, entry.second);", off)
        self.assertLess(off.index("SendRunLine(FindCharacter(entry.first), entry.second, false);"),
                        off.index("ReleaseRunHold("))

    def test_unreachable_move_falls_back_or_skips(self):
        # Razorfen Kraul r22-19z40 step 85: the step's move was refused unreachable after the walk around
        # and the run paused 3 times. A refused own move (or its detour's) goes to MoveRefusedUnreachable
        # with the stand-in NearestReachablePoint found within MoveFallbackYd (25 yd for a plain move),
        # searched only while MoveFallbackWanted; anything it does not handle pauses as before.
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        f = body(COMMANDS, "        if (!accepted && !stallDirectRefused && !legTaken)\n        {", "\n        }\n")
        code = strip_comments(f)
        own = code.index("TortoiseBots::DungeonRun::OwnMoveOrder(it->second.run, order) &&")
        verdict = code.index("TortoiseBots::DungeonRun::UnreachableAnswer(answer);")
        wanted = code.index("bool const searched = unreachable && TortoiseBots::DungeonRun::MoveFallbackWanted(it->second.run);")
        search = code.index("NearestReachablePoint(lead, step->x, step->y, step->z, TortoiseBots::DungeonRun::MoveFallbackYd(*step),")
        handled = code.index("TortoiseBots::DungeonRun::MoveRefusedUnreachable(it->second.run, order, answer, found, dx, dy, dz)))")
        refused = code.index("TortoiseBots::DungeonRun::OrderRefused(it->second.run, order, answer +")
        self.assertTrue(own < verdict < wanted < search < handled < refused)
        self.assertIn("(unreachable ? TortoiseBots::DungeonRun::NoStandInNote(*step, searched) : std::string()));", code)
        self.assertIn("bool const searched = unreachable && TortoiseBots::DungeonRun::MoveFallbackWanted(it->second.run);", code)
        self.assertIn("bool const found = searched &&\n                NearestReachablePoint(", code)
        # Opus review: the stand-in's path from the lead bot is capped (DetourPointReachable's pathYd).
        self.assertIn("DistanceTo(lead, end.x, end.y, end.z), DistanceTo(lead, px, py, ground), path.Length()))",
                      body(COMMANDS, "bool NearestReachablePoint(", "\n}\n"))
        self.assertIn("pathYd <= pointFromLeadYd + kDetourMaxPathYd &&", body(policy, "inline bool DetourPointReachable(", "\n}\n"))
        # Every rewind of the step forgets the step's refusals.
        for fn in ("inline void EventFailed(", "inline bool RearmEventOnWipe("):
            self.assertIn("ForgetMoveRefusals(r);", body(policy, fn, "\n}\n"))
        self.assertIn("if (NeverSkipMove(r))\n        ForgetMoveRefusals(r);", body(policy, "inline bool Resume(", "\n}\n"))
        self.assertNotIn("UseDetour(", code)
        self.assertNotIn("15 yd of it either", code)
        # The ring search reaches the radius it is given and no farther.
        near = strip_comments(body(COMMANDS, "bool NearestReachablePoint(", "\n}\n"))
        self.assertIn("float maxYd, float& ox, float& oy, float& oz,\n    bool dryOnly = false)", near)
        self.assertIn("float const rings[] = {3.0f, 6.0f, 10.0f, 15.0f, 20.0f, 25.0f};", near)
        self.assertIn("for (int i = 0; ring <= maxYd && i < 12; ++i)", near)
        self.assertIn("constexpr float kMoveFallbackYd = 25.0f;", policy)
        self.assertIn("if (ai::PlanDistance(x, y, z, s->x, s->y, s->z) > MoveFallbackYd(*s))",
                      body(policy, "inline bool UseDetour(", "\n}\n"))


class RunHoldsItsDeadWiringTest(unittest.TestCase):
    """SM-REVIVE-PATH-2026-10-23 fix 3: the run leader answers the auto release's
    question (runtime/RunReleaseHold.h) for a bot of a bound party whose run is
    running or paused (DungeonRun::HoldsItsDead, tested in
    tools/test_dungeon_run_policy.cpp); a stopped, timed-out or unbound run is
    erased, so no run is found and the bot releases as before."""

    def test_the_query_is_registered(self):
        code = strip_comments(COMMANDS)
        self.assertIn('#include "../runtime/RunReleaseHold.h"', COMMANDS)
        self.assertIn("TortoiseBots::RunReleaseHold::Register(&IsBoundToActiveDungeonRun)", code)

    def test_the_query_counts_running_and_paused_runs_of_the_bound_party(self):
        query = body(COMMANDS, "static bool IsBoundToActiveDungeonRun(Player* bot)", "}")
        self.assertIn("return RunningRunOf(bot, true) != nullptr;", query)
        finder = strip_comments(body(COMMANDS, "static DungeonRunSlot* RunSlotOf(Player* bot, bool activeOnly)", "\n}\n"))
        self.assertIn("activeOnly ? !TortoiseBots::DungeonRun::HoldsItsDead(it->second.run)", finder)
        # The bound party: the bot's own group is the binding's, and the lead bot is still in it.
        self.assertIn("if (!leadGroup || leadGroup->GetId() != binding->groupId || binding->groupId != groupId)", finder)
        # The kill-first and cc questions still ask about a running run only.
        self.assertIn("return RunningRunOf(bot) != nullptr;", body(COMMANDS, "bool IsInRunningDungeonRun(Player* bot)", "}"))

    def test_stop_and_time_limit_erase_the_run(self):
        stop = body(COMMANDS, 'if (verb == "stop")', 'if (verb == "status")')
        self.assertIn("EndRunEarly(slot.run, RunStopNote(rawArg));", stop) # Done, with the deferred packs left alive
        self.assertIn("EraseRunSlot(commanderRaw);", stop)
        limit = body(COMMANDS, "RunTimeLimitReached(slot.elapsedMs", "return;")
        self.assertIn("TortoiseBots::DungeonRun::EndRunEarly(slot.run,\n"
                      "            TortoiseBots::DungeonRun::RunTimeLimitNote(", limit)
        self.assertIn("EraseRunSlot(", limit)

    def test_walk_after_rest_stands_the_lead_up_before_the_order(self):
        # Blackfathom Deeps r22 step 85, r23 slot 6b step 8: the lead bot stood idle after eating. The walk sent again
        # (DungeonRun::Run::leadWalkReset) first stands a seated lead bot up. Codex and Opus review round 1: no cast is
        # cut, and follow, stay and the stay point are left to the walk's cmd move (a refused move must not leave the
        # lead bot without them); nothing in combat.
        code = strip_comments(COMMANDS)
        stand = body(code, "void StandLeadForWalk(Player* lead)", "\n}\n")
        self.assertIn("if (!lead->IsAlive() || !lead->IsInWorld() || lead->IsInCombat())\n        return;", stand)
        self.assertIn("if (lead->GetStandState() != UNIT_STAND_STATE_STAND)\n"
                      "        lead->SetStandState(UNIT_STAND_STATE_STAND);", stand)
        for gone in ("InterruptNonMeleeSpells", "ChangeStrategy", "DropQueuedMovementOrders", "erase(", "Reset("):
            self.assertNotIn(gone, stand)
        tick = body(code, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        self.assertIn("if (slot.run.leadWalkReset)\n    {\n        slot.run.leadWalkReset = false;\n"
                      "        StandLeadForWalk(lead);\n    }", tick)
        # After the policy's tick that set it, before the order goes out.
        self.assertLess(tick.index("TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);"), tick.index("StandLeadForWalk(lead);"))
        self.assertLess(tick.index("StandLeadForWalk(lead);"), tick.index("IssueRunOrder(commander, order, answer,"))
        # The facts that gate it (DungeonRun::RestWalkResendDue): a rest state confirms the stall, a cast defers it.
        facts = body(code, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f.leadResting = HasMealAura(lead) || lead->GetStandState() != UNIT_STAND_STATE_STAND;", facts)
        self.assertIn("f.leadCasting = lead->IsNonMeleeSpellCasted(true, false, true);", facts)
        self.assertIn("f.leadInCombat = member.inCombat;", facts)
    def test_a_deferred_pull_is_logged_once_after_the_stand_answer(self):
        # Windhorn Canyon r26 slot 5b step 68: a pull not reachable from this floor is deferred, not paused
        # (DungeonRun::DeferUnreachablePack, tested in tools/test_dungeon_run_policy.cpp). The run keeps running, so
        # the end-of-tick log of a stopped run never shows it: the glue logs the policy's deferLog once, after both
        # places a deferral comes from (the tick, and the stand search's answer), and clears it.
        tick = body(strip_comments(COMMANDS), "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        log = ("if (!slot.run.deferLog.empty())\n    {\n"
               "        TB_LOG_DETAIL(\"TortoiseBots: run leader of %s: %s\", commander->GetName(), slot.run.deferLog.c_str());\n"
               "        slot.run.deferLog.clear();\n    }")
        self.assertIn(log, tick)
        self.assertLess(tick.index("TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);"), tick.index(log))
        self.assertLess(tick.index("TortoiseBots::DungeonRun::ApproachStandFound(slot.run, facts, found, sx, sy, sz);"),
                        tick.index(log))

    def test_an_approach_with_no_way_goes_through_the_stand_search(self):
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        hop = body(policy, "inline void ApproachHop(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order,", "\n}\n")
        self.assertIn("ApproachNoWay(r, f, s, why);", hop)
        self.assertNotIn("if (DeferRetryOn(r) && DeferUnreachablePack(r, s, why))", hop)
        walk = body(policy, "inline bool WalkTo(Run& r, const Facts& f, float x, float y, float z, bool direct, std::uint32_t dt,", "\n}\n")
        self.assertLess(walk.index("WalkBackStuckRescue(r, f, dist, x, y, z)"), walk.index("ApproachWalkStuck(r, f, dist, x, y, z)"))
        self.assertLess(walk.index("ApproachWalkStuck(r, f, dist, x, y, z)"), walk.index('Pause(r, "the lead bot is stuck "'))
        noway = body(policy, "inline void ApproachNoWay(", "\n}\n")
        self.assertLess(noway.index("DeferRetryOn(r)"), noway.index("ApproachStandWanted(r, s)"))
        self.assertLess(noway.index("ApproachStandWanted(r, s)"), noway.index("DeferUnreachablePack(r, s, why))\n            return;"))
        # the glue still answers the stand search after the tick (unchanged): the existing pin covers it
    def test_a_refused_firing_position_goes_through_the_stand_search(self):
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        refused = body(policy, "inline void OrderRefused(Run& r, const std::string& order, const std::string& answer)", "\n}\n")
        self.assertLess(refused.index('answer.compare(0, 14, "invalid-target") == 0'),
                        refused.index("if (PullbackRefusedUnreachable(r, order, answer))"))
        self.assertLess(refused.index("if (PullbackRefusedUnreachable(r, order, answer))"),
                        refused.index('Pause(r, "cmd " + order + " refused: " + answer + MissingHint(r));'))
        fn = body(policy, "inline bool PullbackRefusedUnreachable(Run& r, const std::string& order, const std::string& answer)\n{", "\n}\n")
        self.assertIn("r.pullHop", fn)
        self.assertIn("r.pullGuid != guid", fn)
        self.assertIn("ApproachNoWay(r, here, *s, why);", fn)
        fight = body(policy, "inline void FightStep(Run& r, const Facts& f, const PlanStep& s, const std::vector<std::uint32_t>& killOrder,", "\n}\n")
        self.assertLess(fight.index("r.refusedPullStandStep == r.step"), fight.index("if (!f.seen)"))
        clear = body(policy, "inline void ClearApproachStand(Run& r)", "\n}\n")
        self.assertIn("r.refusedPullStandStep = static_cast<std::size_t>(-1);", clear)
        # The FightStep hook exists because the glue answers the stand search before it issues the order and hands a refusal
        # to OrderRefused (a stand asked by a refusal is answered on the next sync): pin that order.
        tick = body(strip_comments(COMMANDS), "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        self.assertLess(tick.index("TortoiseBots::DungeonRun::ApproachStandFound(slot.run, facts, found, sx, sy, sz);"),
                        tick.index("bool const accepted = IssueRunOrder(commander, order, answer,"))
        self.assertLess(tick.index("bool const accepted = IssueRunOrder(commander, order, answer,"),
                        tick.index("TortoiseBots::DungeonRun::OrderRefused(it->second.run, order, answer + legNote +"))

    def test_runs_ended_early_list_their_deferred_packs(self):
        # The Opus/Codex review of 6566f84d: a run stopped early (.bot run stop, the time limit, the key switched
        # off) lists the deferred packs it left alive (DungeonRun::EndRunEarly), never a bare state = Done.
        code = strip_comments(COMMANDS)
        self.assertIn("EndRunEarly(slot.run, RunStopNote(rawArg));", body(code, 'if (verb == "stop")', 'if (verb == "status")'))
        self.assertIn("TortoiseBots::DungeonRun::EndRunEarly(slot.run,", body(code, "RunTimeLimitReached(slot.elapsedMs", "return;"))
        off = body(code, "if (RemoteCommander::RunAllowed(kind, gates))", "SendRunLine(")
        self.assertIn("TortoiseBots::DungeonRun::EndRunEarly(entry.second.run,", off)
        self.assertNotIn("run.state = TortoiseBots::DungeonRun::State::Done;", code)
        self.assertNotIn("slot.run.state = State::Done;", code)

    def test_direct_walk_back_counts_cleared_pulls_from_the_live_step(self):
        # The Opus review of 6566f84d: during a deferred pull's retry the run's step is the deferred one; the pulls
        # cleared after it must not count as live (DungeonRun::LiveFrom: the step the run goes on with).
        self.assertIn("PathCrossesUnclearedPack(*slot.run.plan,\n"
                      "                            TortoiseBots::DungeonRun::LiveFrom(slot.run, true), whole, f.team,", COMMANDS)
    def test_a_bot_outside_the_instance_is_no_part_of_the_fight(self):
        facts = body(strip_comments(COMMANDS),
                     "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead, std::vector<Player*> const& bots,", "\n}\n")
        line = "member.offPlanMap = bot->GetMapId() != slot.run.plan->map;"
        self.assertIn(line, facts)
        self.assertLess(facts.index("member.yd = yd;"), facts.index(line))
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        tick = body(policy, "inline std::string Tick(Run& r, const Facts& raw,", "\n}\n")
        self.assertLess(tick.index("bool const offMap = PlanMapFightView(raw, view);"),
                        tick.index("UnattackableGraceTick(r, in, dt)"))
        self.assertLess(tick.index("bool const offMap = PlanMapFightView(raw, view);"),
                        tick.index("CombatDropTick(r, in, dt)"))
        fight_on = body(policy, "inline bool FightOn(Run& r, const Facts& f, std::uint32_t dt)", "\n}\n")
        self.assertIn("OffMapUnitsOnly(f)", fight_on)
    def test_a_bot_outside_the_instance_is_returned(self):
        issue = body(COMMANDS, "bool IssueRunOrder(Player* commander, std::string const& text, std::string& answer,", "\n}\n")
        self.assertIn('if (text.compare(0, 7, "return ") == 0)\n        return RunOffMapReturn(commander, text, answer);', issue)
        fn = body(strip_comments(COMMANDS),
                  "bool RunOffMapReturn(Player* commander, std::string const& order, std::string& answer)\n{", "\n}\n")
        # the player recheck and the map checks before anything is touched; CombatStop before the teleport
        self.assertLess(fn.index('answer = "player: a real player is in the party";'), fn.index("bot->CombatStop();"))
        self.assertLess(fn.index("lead->GetMapId() != planMap"), fn.index("bot->CombatStop();"))
        self.assertLess(fn.index("bot->GetMapId() == planMap"), fn.index("bot->CombatStop();"))
        # round 1: the open-world recheck (battleground / instance map / transport / summon-busy) before CombatStop
        self.assertLess(fn.index("bot->InBattleGround()"), fn.index("bot->CombatStop();"))
        self.assertLess(fn.index("bot->GetMap()->Instanceable()"), fn.index("bot->CombatStop();"))
        self.assertLess(fn.index("bot->GetTransport()"), fn.index("bot->CombatStop();"))
        self.assertLess(fn.index("PlayerConvenience::Instance().IsBusy(bot->GetObjectGuid())"), fn.index("bot->CombatStop();"))
        self.assertLess(fn.index("pet->CombatStop();"), fn.index("bot->CombatStop();"))
        self.assertLess(fn.index("sServerFacade.GetHostileRefManager(bot).deleteReferences();"), fn.index("bot->CombatStop();"))
        self.assertLess(fn.index("bot->CombatStop();"), fn.index("bot->TeleportTo(lead->GetMapId(),"))
        # round 1: the glue reads the returnable fact beside the off-map flag
        facts = body(strip_comments(COMMANDS),
                     "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead, std::vector<Player*> const& bots,", "\n}\n")
        self.assertIn("member.offPlanMap = bot->GetMapId() != slot.run.plan->map;", facts)
        self.assertIn("member.offMapReturnable = !bot->InBattleGround()", facts)
        self.assertIn("!bot->GetMap()->Instanceable()", facts)
        self.assertIn("!bot->GetTransport()", facts)
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        tick = body(policy, "inline std::string Tick(Run& r, const Facts& raw,", "\n}\n")
        self.assertLess(tick.index("order = TickBody(r, f, dt, lines);"), tick.index("OffMapReturnTick(r, f, order);"))
        # round 1: the policy skips a not-returnable bot (note, no tries, no pause)
        ret = body(policy, "inline bool OffMapReturnTick(Run& r, const Facts& f, std::string& order)\n{", "\n}\n")
        self.assertIn("!m.offMapReturnable", ret)
        self.assertIn("cannot be returned (not in the open world)", ret)
        # round 2: once per (bot, step), replacing an earlier copy; the glue passes the busy reason
        self.assertIn("offMapNoted", ret)
        self.assertIn("is outside the instance and busy (summon or teleport pending)", ret)
        self.assertIn("member.offMapBlock", facts)
        refused = body(policy, "inline void OrderRefused(Run& r, const std::string& order, const std::string& answer)", "\n}\n")
        self.assertLess(refused.index('order.compare(0, 7, "return ") == 0'),
                        refused.index('Pause(r, "cmd " + order + " refused: " + answer + MissingHint(r));'))



class WaitObjectGoneWiringTest(unittest.TestCase):
    """A wait_object row with ai::kPlanFlagGoneIsDone (Shadowfang Keep's Courtyard Door 18895, removed by the escort) is
    done by the object being gone after a wait step read it found. The glue reads the object's state for the wait step
    (-1 when not found), the step reads it into Run::objectsSeen before it judges, and the loader refuses the flag on any
    kind but loot, open and wait_object."""

    POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
    PLAN = (ROOT / "ai/playerbot/DungeonPlanPolicy.h").read_text()

    def test_glue_reads_the_wait_objects_state(self):
        facts = body(strip_comments(COMMANDS), "case ai::PlanStepKind::WaitObject:", "case ai::PlanStepKind::Hold:")
        self.assertIn("f.objectState = 0;", facts)
        self.assertIn("GameObject* go = guid && objectGuid.IsGameObject()", facts)

    def test_glue_reads_the_object_as_authoritative_only_when_it_could_find_it(self):
        facts = body(strip_comments(COMMANDS), "case ai::PlanStepKind::WaitObject:", "case ai::PlanStepKind::Hold:")
        read = body(facts, "f.objectReadable =", ";")
        for needed in ("lead->IsAlive()", "!lead->IsBeingTeleported()", "lead->GetMapId() == slot.run.plan->map",
                       "EntryPointReachable(lead, spec)", "lead->GetMap()->IsLoaded(step->x, step->y)"):
            self.assertIn(needed, read)

    def test_sighting_and_miss_count_are_updated_before_any_fight_handling(self):
        tick = body(strip_comments(self.POLICY), "NoteLeadPath(r, f);", "bool const objectDone")
        self.assertIn("UpdateObjectSighting(r, *s, f, dt);", tick)
        update = body(strip_comments(self.POLICY), "inline void UpdateObjectSighting(", "\n}\n")
        self.assertIn("r.objectsSeen.push_back({s.entry, s.x, s.y, s.z});", update)
        self.assertIn("f.objectReadable && f.leadAlive", update)

    def test_wait_step_judges_gone_before_the_timeout_pause(self):
        step = body(strip_comments(self.POLICY), "case PlanStepKind::WaitObject:", "case PlanStepKind::WaitClear:")
        self.assertIn("WaitObjectGoneIsDone(r, *s, f)", step)
        # The pause on the timeout stays after the gone check: a never-seen object still pauses.
        self.assertLess(step.index("WaitObjectGoneIsDone(r, *s, f)"), step.index("Pause(r,"))

    def test_stale_combat_shortcut_accepts_a_confirmed_gone(self):
        short = body(strip_comments(self.POLICY), "bool const objectDone =", ";")
        self.assertIn("WaitObjectGoneIsDone(r, *s, f)", short)

    def test_gone_check_needs_a_sighting_and_a_confirmed_readable_miss(self):
        check = body(strip_comments(self.POLICY), "inline bool WaitObjectGoneIsDone(", "\n}\n")
        self.assertIn("f.objectState < 0", check)
        self.assertIn("f.objectReadable", check)
        self.assertIn("r.goneMissMs >= kGoneConfirmMs", check)
        self.assertIn("WaitSightSeen(r, s)", check)
        self.assertIn("ai::kPlanFlagGoneIsDone", check)

    def test_loader_accepts_the_flag_on_wait_object_only_beside_loot_and_open(self):
        norm = body(strip_comments(self.PLAN), "inline PlanStepRefusal NormalizePlanStep(", "if ((step.flags & kPlanFlagDeadNpcIsDone)")
        for kind in ("Loot", "Open", "WaitObject"):
            self.assertIn("step.kind != PlanStepKind::" + kind, norm)

class WaitClearFloorWiringTest(unittest.TestCase):
    """Shadowfang Keep r38 slotA step 308: the wait_clear spot at z 129 counted hostiles on other floors (z ~100-157)
    and paused four times on "waited 120 s; 3-4 hostiles still near the spot". The glue lists every living hostile
    within the 2D radius of the step's point with its z (unit->GetPositionZ()) and whether it is engaged with the
    party (EngagedWithParty, the same 'a party bot is its victim or attacks it' rule the engaged reads use); the
    normal wait counts every one, and only the timeout boundary drops all-off-floor bystanders
    (WaitClearOffFloorOnly: every remaining hostile non-engaged and |dz| > kWaitClearMaxDz)."""

    POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()

    def test_policy_counts_every_hostile_until_the_timeout(self):
        self.assertIn("constexpr float kWaitClearMaxDz = 10.0f;", self.POLICY)
        count = body(strip_comments(self.POLICY), "inline std::uint32_t WaitClearCount(", "\n}\n")
        self.assertNotIn("kWaitClearMaxDz", count)
        self.assertNotIn("engaged", count)
        off = body(strip_comments(self.POLICY), "inline bool WaitClearOffFloorOnly(", "\n}\n")
        self.assertIn("h.engaged ||", off)
        self.assertIn("std::fabs(h.z - s.z) <= kWaitClearMaxDz", off)
        step = body(strip_comments(self.POLICY), "case PlanStepKind::WaitClear:", "case PlanStepKind::Rest:")
        self.assertIn("WaitClearCount(*s, f)", step)
        self.assertIn("WaitClearOffFloorOnly(*s, f)", step)
        self.assertIn("the spot is clear on its floor:", step)
        self.assertNotIn("f.hostilesNear", self.POLICY)

    def test_glue_lists_2d_near_hostiles_with_z(self):
        facts = body(strip_comments(COMMANDS), "case ai::PlanStepKind::WaitClear:", "case ai::PlanStepKind::WaitObject:")
        self.assertIn("float const dx = unit->GetPositionX() - step->x, dy = unit->GetPositionY() - step->y;", facts)
        self.assertIn("if (std::sqrt(dx * dx + dy * dy) <= step->radius)", facts)
        self.assertIn("unit->GetPositionZ()", facts)
        self.assertIn("EngagedWithParty(unit, party)", facts)
        self.assertIn("spot.engaged =", facts)
        self.assertIn("f.clearUnits.push_back(", facts)



class BossTacticsJoinWiringTest(unittest.TestCase):
    """The Codex review of d7aca6b5: the run leader's deferral gate (DungeonRun::PullMayBeDeferred) reads a later boss
    step's ai_boss_tactic subjects from the sealed plan (PlanStep::tacticEntries). The loader fills them from the tactic
    rows (DungeonPlanTable::JoinBossTactics, tested in tools/test_dungeon_plan_policy.cpp) before the seal, at startup
    and on `.bot run reloadplans`, with the tactics read first."""

    MGR = strip_comments((ROOT / "ai/playerbot/DungeonPlanMgr.cpp").read_text())

    def test_the_rows_reader_joins_the_tactics_before_the_seal(self):
        read = body(self.MGR, "static void ReadDungeonPlanRows(DungeonPlanTable& table, PlanLoadCounts& counts, "
                              "const BossTacticTable& tactics)", "\n}\n")
        self.assertIn("table.JoinBossTactics(tactics);", read)
        self.assertLess(read.index("table.JoinBossTactics(tactics);"), read.index("table.Seal();"))
        self.assertLess(read.index("FROM `ai_dungeon_step`"), read.index("table.JoinBossTactics(tactics);"))

    def test_startup_reads_the_tactics_first(self):
        load = body(self.MGR, "void ai::LoadDungeonPlans()", "\n}\n")
        self.assertIn("ReadDungeonPlanRows(table, counts, GetBossTactics());", load)
        self.assertLess(load.index("LoadBossTactics();"), load.index("ReadDungeonPlanRows(table, counts, GetBossTactics());"))

    def test_reload_joins_the_new_tactics(self):
        reload = body(self.MGR, "PlanReloadReport ai::ReloadDungeonPlans(", "    return report;\n}")
        self.assertIn("ReadDungeonPlanRows(*next, counts, *nextTactics);", reload)
        self.assertLess(reload.index("ReadBossTacticRows(*nextTactics, &tacticRowsRead);"),
                        reload.index("ReadDungeonPlanRows(*next, counts, *nextTactics);"))

    def test_the_glue_hands_the_run_the_live_tactics_before_its_tick(self):
        # The Codex review of 7cfcbff6: a reload swaps the tactic table under a run that keeps its old plan; the gate
        # also reads the table the server reads now (DungeonRun::Run::liveTactics), set before the tick each second.
        tick = body(strip_comments(COMMANDS), "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        self.assertIn("slot.run.liveTactics = &ai::GetBossTactics();", tick)
        self.assertLess(tick.index("slot.run.liveTactics = &ai::GetBossTactics();"),
                        tick.index("TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);"))
        self.assertIn('#include "../ai/playerbot/BossTacticsMgr.h"', COMMANDS)

    def test_the_gate_reads_the_joined_subjects(self):
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        gate = body(policy, "inline bool PullMayBeDeferred(const Run& r)", "\n}\n")
        self.assertIn("for (std::uint32_t e : n.tacticEntries)", gate)


class RunTestSpeedWiringTest(unittest.TestCase):
    """Owner request 2026-10-25: `.bot run start ... speed=<1.0-3.0>` (DungeonRun::TakeSpeed, SpeedRateNow, tested in
    tools/test_dungeon_run_policy.cpp). The glue's parts: the start's guards, the apply before the tick's order, and the
    sync that sends every listed bot back."""

    def test_the_start_takes_and_guards_the_speed(self):
        code = strip_comments(COMMANDS)
        run = body(code, "bool HandleDungeonRun(ChatHandler* handler, char const* args)", "\n}\n")
        start = run[run.index('if (verb == "start")\n'):]
        self.assertIn("CheckTestSpeed(speedArgs.rate, playerLedStart.playerLed, realPlayer, IsBotAdministrator(commander))", start)
        self.assertIn("if (!playerLedStart.playerLed)", start)
        self.assertIn("arg = LowerCopy(speedArgs.rest);", start)
        self.assertLess(start.index("CheckTestSpeed(speedArgs.rate,"), start.index("arg = LowerCopy(speedArgs.rest);"))
        self.assertLess(start.index("arg = LowerCopy(speedArgs.rest);"), start.index("StartArgs const startArgs = ParseStartArgs(arg);"))
        self.assertIn("!BotManager::Instance().IsBot(member.guid)", start[:start.index("CheckTestSpeed(speedArgs.rate,")])
        self.assertLess(start.index("slot.run.testSpeed = speedArgs.rate;"),
                        start.index("DungeonRunSlot& stored = DungeonRuns()[commanderRaw] = slot;"))

    def test_the_tick_applies_before_its_order(self):
        code = strip_comments(COMMANDS)
        tick = body(code, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        self.assertLess(tick.index("TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);"),
                        tick.index("TortoiseBots::DungeonRun::SpeedRateNow(slot.run, facts, dt);"))
        self.assertLess(tick.index("ApplyRunSpeed(commanderRaw, lead, bots, speedRate);"),
                        tick.index("bool const accepted = IssueRunOrder(commander, order, answer,"))
        # Round 1: the tick that advances onto a pull or boss step never starts fast (f.seen was read on the
        # step before). The cap sits between the rate read and the apply, before the order goes out.
        self.assertLess(tick.index("stepBeforeTick"), tick.index("TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);"))
        self.assertLess(tick.index("SpeedCapAfterTick(slot.run, stepBeforeTick)"),
                        tick.index("ApplyRunSpeed(commanderRaw, lead, bots, speedRate);"))
        self.assertLess(tick.index("ApplyRunSpeed(commanderRaw, lead, bots, speedRate);"),
                        tick.index("DropUnattackableCombat(lead, bots, slot.run.combatDrops);"))

    def test_the_rate_is_forced_and_goes_back(self):
        code = strip_comments(COMMANDS)
        apply = body(code, "bool ApplyBotSpeed(Player* bot, float rate, RunSpeedEntry& entry)", "\n}\n")
        self.assertIn("bot->UpdateSpeed(MOVE_RUN, true, rate);", apply)
        self.assertIn("bot->UpdateSpeed(MOVE_WALK, true, rate);", apply)
        self.assertNotIn("MOVE_SWIM", apply)
        restore = body(code, "bool RestoreBotSpeed(Player* bot, RunSpeedEntry& entry,", "\n}\n")
        self.assertIn("bot->UpdateSpeed(MOVE_RUN, true, 1.0f);", restore)
        self.assertIn("pet->UpdateSpeed(MOVE_RUN, true, 1.0f);", restore)
        each = body(code, "void ApplyRunSpeed(uint64 commanderRaw, Player* lead, std::vector<Player*> const& bots, float rate)\n{", "\n}\n")
        self.assertIn("!MemberInCombat(bot)", each)
        self.assertIn("bot->GetSpeedRate(MOVE_RUN) != entry->second.appliedRun", each)
        self.assertIn("CatchUpPet(bot, rate);", each)
        # Round 1: a slowed bot (Frostbolt, Crippling Poison) is never sped up; a bot in another instance of the
        # map stays 1.0; a bot with the movespeed cheat is left alone.
        self.assertIn("HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)", COMMANDS)
        self.assertIn("!SpeedSlowed(bot)", each)
        self.assertIn("SpeedSameInstance(bot, lead)", each)
        self.assertNotIn("bot->GetMapId() == lead->GetMapId()", each)
        self.assertIn("!SpeedCheatOn(bot)", each)
        self.assertIn("BotCheatMask::movespeed", COMMANDS)
        catchup = body(code, "void CatchUpPet(Player* bot, float rate)", "\n}\n")
        self.assertIn("IsFollowing()", catchup)
        self.assertIn("SpeedSlowed(pet)", catchup)
        sync = body(code, "void SyncRunSpeed()\n{", "\n}\n")
        self.assertIn("run->second.run.speedNow > 1.0f", sync)
        self.assertIn("MemberInCombat(bot)", sync)
        self.assertIn("TortoiseBots::DungeonRun::SpeedFightSeen(run->second.run);", sync)
        self.assertIn("SpeedSameInstance(bot, lead)", sync)
        self.assertIn("!SpeedSlowed(bot)", sync)
        # Round 1: a real player joining mid-run latches the run blocked and sends the party back in the same update.
        self.assertIn("speedPlayerBlocked = true;", sync)
        self.assertIn("playerBlocked.insert(it->second.commanderRaw)", sync)
        # Round 2 (Codex finding): a bot that turns the movespeed cheat on AFTER it was raised goes through the shared
        # cleanup, and the cheat owns the rate (10x, every AI update) — so both places erase its entry WITHOUT
        # RestoreBotSpeed (its pet is only restored when it does not follow the cheated bot). Mutation: calling
        # RestoreBotSpeed on the cheat branch fails a pin.
        self.assertIn("RestoreNonFollowingPet(bot);", each)
        self.assertIn("RestoreNonFollowingPet(bot);", sync)
        cheat_each = each[each.index("if (SpeedCheatOn(bot))"):each.index("RestoreNonFollowingPet(bot);\n                    listed.erase(entry);")]
        self.assertNotIn("RestoreBotSpeed(bot);", cheat_each)
        self.assertLess(each.index("listed.erase(entry);"), each.index("if (!RestoreBotSpeed(bot, entry->second, WorldTimer::getMSTime()))"))
        cheat_at = sync.index("if (bot && SpeedCheatOn(bot))")
        cheat_sync = sync[cheat_at:sync.index("continue;", cheat_at)]
        self.assertNotIn("RestoreBotSpeed(bot);", cheat_sync)
        self.assertIn("RestoreNonFollowingPet(bot);", cheat_sync)
        self.assertNotIn("SetSpeedRatePersistance", code)
        self.assertNotIn("UpdateSpeed(MOVE_RUN, false", apply + restore + each)

    def test_speed_change_waits_for_a_swimming_spline(self):
        code = strip_comments(COMMANDS)
        apply = body(code, "bool ApplyBotSpeed(Player* bot, float rate, RunSpeedEntry& entry)", "\n}\n")
        self.assertIn("SpeedChangeWaitsFor(bot)", apply)
        self.assertLess(apply.index("SpeedChangeWaitsFor(bot)"), apply.index("bot->UpdateSpeed(MOVE_RUN, true, rate);"))
        self.assertIn("return false;", apply)
        self.assertIn("waitSinceMs = 0;", apply)
        restore = body(code, "bool RestoreBotSpeed(Player* bot, RunSpeedEntry& entry,", "\n}\n")
        self.assertIn("SpeedChangeWaitsFor(bot)", restore)
        self.assertLess(restore.index("SpeedChangeWaitsFor(bot)"), restore.index("bot->UpdateSpeed(MOVE_RUN, true, 1.0f);"))
        self.assertLess(restore.index("SpeedChangeWaitsFor(bot)"), restore.index("pet->UpdateSpeed"))
        self.assertIn("return false;", restore)
        gate = body(code, "static bool SpeedChangeWaitsFor(Player* bot)", "\n}\n")
        self.assertIn("bot->IsInWater() || bot->IsUnderwater() ||", gate)
        self.assertIn("bot->HasMovementFlag(MOVEFLAG_SWIMMING)", gate)
        self.assertIn("!bot->movespline->Finalized()", gate)
        self.assertIn("TortoiseBots::DungeonRun::SpeedChangeWaits(", gate)
        each = body(code, "void ApplyRunSpeed(uint64 commanderRaw, Player* lead, std::vector<Player*> const& bots, float rate)\n{", "\n}\n")
        self.assertIn("if (!RestoreBotSpeed(bot, entry->second, WorldTimer::getMSTime()))", each)
        sync = body(code, "void SyncRunSpeed()\n{", "\n}\n")
        self.assertEqual(sync.count("if (bot && !RestoreBotSpeed(bot, it->second, WorldTimer::getMSTime()))"), 2)
        self.assertEqual(each.count("if (!RestoreBotSpeed(bot, entry->second, WorldTimer::getMSTime()))"), 1)

    def test_speed_restore_wait_is_bounded(self):
        code = strip_comments(COMMANDS)
        restore = body(code, "bool RestoreBotSpeed(Player* bot, RunSpeedEntry& entry,", "\n}\n")
        # The restore wait is bounded (lead decision 2026-10-25): a run a real
        # player joined restores at once, never waits; otherwise at most 15 s
        # from the first refused restore per bot, stamped in the speed entry.
        self.assertIn("speedPlayerBlocked", restore)
        self.assertIn("waitSinceMs", restore)
        self.assertIn("TortoiseBots::DungeonRun::SpeedRestoreWaits(", restore)
        self.assertIn("WorldTimer::getMSTime()", code)
        self.assertLess(restore.index("SpeedChangeWaitsFor(bot)"), restore.index("bot->UpdateSpeed(MOVE_RUN, true, 1.0f);"))
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        self.assertIn("kSpeedRestoreWaitMs = 15000", policy)
        self.assertIn("inline bool SpeedRestoreWaits(bool playerBlocked", policy)
        entry = body(code, "struct RunSpeedEntry", "\n};")
        self.assertIn("waitSinceMs", entry)
        apply = body(code, "bool ApplyBotSpeed(Player* bot, float rate, RunSpeedEntry& entry)", "\n}\n")
        self.assertIn("waitSinceMs = 0;", apply)

    def test_player_scan_runs_before_keep_is_decided(self):
        code = strip_comments(COMMANDS)
        sync = body(code, "void SyncRunSpeed()\n{", "\n}\n")
        # Round 2 (Codex): the bound group's real-player scan runs for EVERY listed entry with a
        # live run/binding, BEFORE keep is decided and BEFORE any restore. A real player joining
        # after keep already went false (fight, pause, stop, speedNow back at 1.0) still latches
        # the run blocked on that same call, so a bot already waiting out a swim-spline restore
        # (waitSinceMs set) goes back at once instead of up to 15 s later.
        loop = sync[sync.index("for (auto it = listed.begin(); it != listed.end();)"):]
        self.assertLess(loop.index("GetMemberSlots()"), loop.index("speedNow > 1.0f"))
        self.assertLess(loop.index("GetMemberSlots()"), loop.index("RestoreBotSpeed(bot, it->second"))
        # The scan is not gated on keep: the first keep gate comes after the scan.
        self.assertLess(loop.index("GetMemberSlots()"), loop.index("if (keep)"))
        self.assertIn("playerBlocked.insert(it->second.commanderRaw)", loop)
        # Latched at once: the flag is set before any same-call restore, so RestoreBotSpeed
        # (which restores at once when speedPlayerBlocked is set) does not wait out the swim.
        self.assertLess(loop.index("speedPlayerBlocked = true;"), loop.index("RestoreBotSpeed(bot, it->second"))

    def test_commander_join_blocks_like_any_real_player(self):
        code = strip_comments(COMMANDS)
        sync = body(code, "void SyncRunSpeed()\n{", "\n}\n")
        # A GM commander who starts a bot-only test-speed run and then joins the bound group blocks it on that
        # same call: only bots are skipped, never the commander GUID.
        self.assertIn("SpeedMemberBlocksRun(member.guid.GetRawValue(),", sync)
        self.assertIn("BotManager::Instance().IsBot(member.guid)", sync)
        self.assertNotIn("member.guid.GetRawValue() == binding->commanderGuid", sync)
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        decision = body(policy, "inline bool SpeedMemberBlocksRun(", "\n}\n")
        self.assertIn("return !memberIsBot;", decision)

    def test_every_clear_of_the_runs_syncs_the_speed(self):
        code = strip_comments(COMMANDS)
        update = body(code, "static void UpdateDungeonRuns(uint32 diff)", "\n}\n")
        self.assertLess(update.index("SyncRunSpeed();"), update.index("if (DungeonRuns().empty())\n"))
        remote = body(code, "void UpdateRemoteCommanders(uint32_t diff)", "\n}\n")
        empty = remote[remote.index("if (CommanderBindings().Empty())"):remote.index("s_commanderSweepMs += diff;")]
        # The runs are cleared through EraseAllRunSlots (it takes the given items back first, item-leg skip).
        self.assertLess(empty.index("EraseAllRunSlots();"), empty.index("SyncRunSpeed();"))
        clear = body(code, "void ClearRemoteCommanders()", "\n}\n")
        self.assertLess(clear.index("EraseAllRunSlots();"), clear.index("SyncRunSpeed();"))


if __name__ == "__main__":
    unittest.main()
