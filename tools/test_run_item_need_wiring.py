"""The item-leg-skip glue (commands/Commander.cpp) calls the pure decisions of runtime/RunItemNeedPolicy.h and
runtime/DungeonRunPolicy.h that tools/test_run_item_need_policy.cpp and tools/test_dungeon_run_policy.cpp test, so
those tests drive the code the server runs. Review round 2 (Codex + Opus, 2026-10-26).
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands" / "Commander.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class StartWiringTest(unittest.TestCase):
    def start_block(self):
        return body(COMMANDS, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n")

    def test_refusal_sends_the_refusal_line(self):
        # Codex fix 3: the bot-only refusal sends the verdict's text (PlanStart's refusal), never the empty warning.
        block = self.start_block()
        self.assertIn("RunItemNeed::PlanStart(", block)
        self.assertIn('SendActionError(handler, intent, "requirement", startPlan.refusal.c_str());', block)
        self.assertNotIn("needVerdict.warn.c_str()", block)

    def test_party_is_read_for_every_needed_item(self):
        # The bags are read for NeedsFrom's items (derived blocking ones too), not only the need rows.
        block = self.start_block()
        self.assertIn("RunItemNeed::PlanNeedItems(*plan, startStep)", block)


ACTIONS = ROOT / "ai" / "playerbot" / "strategy" / "actions"
CHAR_MIGRATION = (ROOT / "data" / "sql" / "character" / "20261026120000_char.sql").read_text()


class GivenItemProvenanceWiringTest(unittest.TestCase):
    """Codex fix 1: the ledger holds the exact created item instance, written before the bot holds it; the take-back
    destroys that instance only; a failed ledger write refuses the give; the given item cannot leave the bot."""

    def give(self):
        return body(COMMANDS, "void GiveRunItems(DungeonRunSlot& slot,")

    def take(self):
        return body(COMMANDS, "void TakeRunItems(DungeonRunSlot& slot,")

    def cleanup(self):
        return body(COMMANDS, "static void CleanupRunGivenItemsImpl(Player* player)")

    def test_ledger_row_is_written_before_the_bot_holds_the_item(self):
        give = self.give()
        created = give.index("Item::CreateItem(order.item, 1, bot)")
        bound = give.index("given->SetBinding(true)")
        written = give.index("RunGivenRowInsert(")
        stored = give.index("bot->StoreItem(dest, given, true)")
        self.assertLess(created, bound)
        self.assertLess(bound, written)
        self.assertLess(written, stored)
        self.assertNotIn("bot->StoreNewItem(", give)

    def test_failed_ledger_write_refuses_the_give(self):
        give = self.give()
        refuse = body(give, "if (!RunGivenRowInsert(", "continue;")
        self.assertIn("delete given;", refuse)
        insert = body(COMMANDS, "static bool RunGivenRowInsert(")
        self.assertIn("CharacterDatabase.DirectPExecute(", insert)
        self.assertIn("return ", insert)

    def test_give_records_the_instance(self):
        give = self.give()
        self.assertIn("RunGivenItems::Add(itemGuidLow)", give)
        self.assertIn("slot.given[raw].push_back({order.item, itemGuidLow})", give)

    def test_take_back_destroys_the_given_instance_only(self):
        take = self.take()
        self.assertIn("bot->GetItemByGuid(ObjectGuid(HIGHGUID_ITEM, row.itemGuid))", take)
        self.assertIn("bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true)", take)
        self.assertNotIn("DestroyItemCount", take)
        self.assertNotIn("GetItemCount", take)
        self.assertIn("RunGivenRowDelete(row.itemGuid)", take)

    def test_login_cleanup_destroys_the_ledger_instances_in_world_only(self):
        cleanup = self.cleanup()
        self.assertNotIn("DestroyItemCount", cleanup)
        self.assertIn("GetItemByGuid(ObjectGuid(HIGHGUID_ITEM,", cleanup)
        # Not in the world yet: the rows stay (queued for the tick), nothing deleted.
        guard = cleanup.index("if (!player->IsInWorld())")
        self.assertLess(guard, cleanup.index("PQuery("))
        self.assertIn("PendingRunGivenCleanup().insert(", body(cleanup, "if (!player->IsInWorld())", "}"))

    def test_full_bags_log_once_per_run_per_bot(self):
        give = self.give()
        self.assertIn("slot.bagsFullLogged.insert(", give)
        self.assertIn("bags are full", give)
        full = body(give, "CanStoreNewItem", "continue;\n        }")
        self.assertIn("TB_LOG_DETAIL(", full)
        self.assertLess(full.index("slot.bagsFullLogged.insert("), full.index("TB_LOG_DETAIL("))
        self.assertIn("std::unordered_set<uint64> bagsFullLogged;", body(COMMANDS, "struct DungeonRunSlot"))

    def test_take_back_names_the_core_lookup_covering_bank_and_keyring(self):
        take = body(COMMANDS, "// Takes back exactly the instances the run gave")
        self.assertIn("src/game/Objects/Player.cpp:10600", take)
        for place in ("keyring", "bank"):
            self.assertIn(place, take)

    def test_ledger_table_keys_the_item_instance(self):
        self.assertIn("`item_guid` int unsigned NOT NULL", CHAR_MIGRATION)
        self.assertIn("PRIMARY KEY (`item_guid`)", CHAR_MIGRATION)
        self.assertNotIn("`count`", CHAR_MIGRATION)

    def test_bot_item_paths_refuse_a_given_item(self):
        give = (ACTIONS / "GiveItemAction.cpp").read_text()
        share = (ACTIONS / "GuildShareItemAction.cpp").read_text()
        sell = (ACTIONS / "SellAction.cpp").read_text()
        for name, text in (("GiveItemAction", give), ("GuildShareItemAction", share)):
            self.assertIn("RunGivenItemRegistry.h", text, name)
            moved = text.index("bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);")
            guard = text.index("TortoiseBots::RunGivenItems::Has(item->GetGUIDLow())")
            self.assertLess(guard, moved, name)
        sell_one = body(sell + "\n", "bool SellAction::Sell(Player* requester, Item* item)")
        self.assertIn("TortoiseBots::RunGivenItems::Has(item->GetGUIDLow())", sell_one)



class DoorAndLegFactsWiringTest(unittest.TestCase):
    """Opus 1/2/3 + Codex 2: the glue hands the party's bags to FillLegNeedFacts (door holders, best player lockpick,
    the door's bot carrier), reads the door near the lead bot (not found = not read), and runs DoorOpenWhilePaused on
    a paused door hold; the resume goes through ResumeChecked (which refuses while the door is closed)."""

    def read(self):
        return body(COMMANDS, "void ReadRunNeedFacts(DungeonRunSlot const& slot, Player* lead, TortoiseBots::DungeonRun::Facts& f)\n{")

    def test_bags_go_through_the_pure_fill(self):
        read = self.read()
        self.assertIn("RunItemNeed::LegNeedItems(slot.run)", read)
        self.assertIn("RunItemNeed::FillLegNeedFacts(slot.run, RunItemNeedMembers(RunNeedParty(lead, items)), f)", read)
        self.assertNotIn("legNeedHolders", read)  # holders are decided in the pure fill only
        self.assertIn("ReadRunNeedFacts(slot, lead, f);", body(COMMANDS, "void ReadRunFacts(DungeonRunSlot const& slot,"))

    def test_door_is_looked_for_near_the_lead_and_not_found_is_not_closed(self):
        read = self.read()
        self.assertIn("spec.hasPoint = false;", read)
        self.assertNotIn("spec.point", read)
        found = read.index("if (!go)\n        return; // not found")
        self.assertLess(found, read.index("f.doorNeedRead = true;"))
        self.assertIn("f.doorNeedX = go->GetPositionX();", read)

    def test_party_reads_lockpicking_for_real_players(self):
        party = body(COMMANDS, "std::vector<RunNeedMemberPlain> RunNeedParty(Player* lead,")
        self.assertIn("entry.lockpick = member->GetSkillValue(SKILL_LOCKPICKING);", party)
        self.assertIn("slots.insert(slots.begin(), slot);", party)  # the lead bot first

    def test_paused_door_hold_reads_the_door_and_resumes_only_when_open(self):
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)")
        block = body(tick, "slot.run.state == TortoiseBots::DungeonRun::State::Paused && slot.run.doorPaused && commander",
                     "\n    }\n")
        self.assertIn("ReadRunNeedFacts(slot, doorLead, doorFacts);", block)
        self.assertIn("TortoiseBots::DungeonRun::DoorOpenWhilePaused(slot.run, doorFacts)", block)

    def test_resume_goes_through_resume_checked(self):
        run = body(COMMANDS, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n")
        resume = body(run, 'else if (verb == "resume")', "\n    }\n")
        self.assertIn("done = ResumeChecked(slot.run, spot, refusal, door);", resume)
        self.assertNotIn("Resume(slot.run)", resume.replace("ResumeChecked(slot.run", ""))

    def test_resume_tells_door_not_found_from_closed(self):
        run = body(COMMANDS, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n")
        resume = body(run, 'else if (verb == "resume")', "\n    }\n")
        self.assertIn("ReadRunNeedFacts(slot, doorLead, doorFacts);", resume)
        self.assertIn("ResumeChecked(slot.run, spot, refusal, door)", resume)
        self.assertIn("DoorRead::Open", resume)
        self.assertIn("doorNeedState", resume)
        self.assertIn("the door is out of sight of the lead bot",
            (ROOT / "runtime" / "DungeonRunPolicy.h").read_text())



class GiveTickWiringTest(unittest.TestCase):
    """Opus 4/5 + the note: TickDungeonRun calls RunItemNeed::DecideGiveTick with the slot's given bots and the lead
    bot's group, and acts on every field of the plan (take from bots that left, take all, the join pause only when
    the plan says so, give again). DungeonRuns() loses slots only through the helpers that take back first."""

    def tick(self):
        return body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)")

    def test_solo_lead_counts_as_its_own_group(self):
        tick = self.tick()
        group = tick[tick.index("if (Group* tickGroup = tickLead->GetGroup())"):]
        self.assertIn("giveIn.groupBots.push_back(binding->leadGuid);", group)
        self.assertLess(group.index("giveIn.groupBots.push_back(binding->leadGuid);"),
                        group.index("TortoiseBots::RunItemNeed::DecideGiveTick(giveIn)"))

    def test_tick_calls_the_pure_decision(self):
        tick = self.tick()
        self.assertIn("TortoiseBots::RunItemNeed::DecideGiveTick(giveIn)", tick)
        self.assertIn("giveIn.givenBots.push_back(entry.first);", tick)
        self.assertIn("giveIn.groupBots.push_back(member.guid.GetRawValue());", tick)
        self.assertIn("giveIn.system = binding->system;", tick)
    def test_tick_acts_on_every_field(self):
        tick = self.tick()
        self.assertIn("for (uint64 leftBot : givePlan.takeBots)\n            TakeRunItems(slot, leftBot);", tick)
        self.assertIn("if (givePlan.takeAll)\n            TakeRunItems(slot, 0);", tick)
        self.assertIn("if (givePlan.giveAgain && slot.run.plan)", tick)
        # The join pause only inside the plan's pauseForPlayer (once, the tick the items were taken).
        self.assertEqual(tick.count('"a real player joined: "'), 1)
        pause = body(tick, "if (givePlan.pauseForPlayer && slot.run.plan)", "\n        }\n")
        self.assertIn('PausePlayer(slot.run, "a real player joined: " + needVerdict.warn);', pause)
        self.assertNotIn("TakeRunItems(slot, 0); // done: nothing given", tick)

    def test_slots_go_only_through_the_take_back_helpers(self):
        erase = body(COMMANDS, "void EraseRunSlot(uint64_t commanderRaw)")
        clear = body(COMMANDS, "void EraseAllRunSlots()")
        self.assertIn("TakeRunItems(it->second, 0);", erase)
        self.assertIn("TakeRunItems(entry.second, 0);", clear)
        rest = COMMANDS.replace(erase, "").replace(clear, "")
        self.assertNotIn("DungeonRuns().erase(", rest)
        self.assertNotIn("DungeonRuns().clear()", rest)



class IncludesWiringTest(unittest.TestCase):
    def test_glue_includes_the_headers_it_calls(self):
        # The VM build of round 2 failed on these missing includes; the standalone tests compile the headers alone.
        for header in ("RunItemNeedPolicy.h", "RunItemNeedFacts.h", "RunGivenItemRegistry.h"):
            self.assertIn('#include "../runtime/%s"' % header, COMMANDS)


if __name__ == "__main__":
    unittest.main()
