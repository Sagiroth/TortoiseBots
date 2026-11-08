"""The run leader's glue for the Scarlet Cathedral Mograine / Whitemane fixes (SM-CATHEDRAL-MOGRAINE-2026-10-23).

r23 slot 5: Whitemane's Dominate Mind made the judge bot Hele (the lead and two more were dead), so the run read
Whitemane as "cannot be attacked" and listed Hele as an engaged hostile; the boss step for Mograine paused "not
found" three times while his corpse lay at the door 290 yd away; and Whitemane, held at 1 hp by her script, was
fought by the same resumed fight four times. runtime/DungeonRunPolicy.h decides on the facts (EngagedPlanBossOrHold,
the boss search in FightStep, HpHeldTick, ResumeChecked); this test pins that commands/BotCommands.cpp feeds them.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


CODE = strip_comments(COMMANDS)


class BossPairWiringTest(unittest.TestCase):
    def test_a_charmed_bot_never_judges(self):
        fit = body(CODE, "bool RunJudgeFit(Player* lead, Player* bot)")
        self.assertIn("bot->IsCharmed()", fit)
        self.assertIn("return bot == lead || lead->IsCharmed() || sServerFacade.IsFriendlyTo(bot, lead);", fit)
        judge = body(CODE, "Player* RunJudge(Player* lead, std::vector<Player*> const& bots)")
        self.assertNotIn("lead->IsAlive()", judge)

    def test_party_members_and_pets_are_never_engaged_units(self):
        # Merged with the member read (integ/19z42): ReadEngagedParty fills the party and victim sets, ReadEngagedWith
        # applies them (the party read of ReadEngagedUnits passes them, the member read passes none).
        party = body(CODE, "EngagedParty ReadEngagedParty(std::vector<Player*> const& bots,")
        self.assertIn("ep.party.insert(bot->GetObjectGuid().GetRawValue());", party)
        self.assertIn("ep.party.insert(pet->GetObjectGuid().GetRawValue());", party)
        read = body(CODE, "void ReadEngagedWith(Player* lead, Player* judge, Player* factLead, Unit* unit,")
        self.assertIn("ep->party.count(other->GetObjectGuid().GetRawValue())", read)
        # A charmed party bot fighting the party is named for the note, never engaged.
        self.assertIn("ep->charmedFighting->push_back(other->GetName());", read)
        self.assertLess(read.index("charmedFighting->push_back"), read.index("out.push_back(fact);"))
        units = body(CODE, "void ReadEngagedUnits(Player* lead, std::vector<Player*> const& bots,")
        self.assertIn("ReadEngagedParty(bots, &f.charmedFighting);", units)
        self.assertLess(units.index("ReadEngagedParty("), units.index("ReadEngagedWith("))

    def test_engaged_units_carry_hp_and_party_hits(self):
        party = body(CODE, "EngagedParty ReadEngagedParty(std::vector<Player*> const& bots,")
        self.assertIn("ep.victims.insert(victim->GetObjectGuid().GetRawValue());", party)
        self.assertIn("if (Unit* victim = pet->GetVictim())", party)
        read = body(CODE, "void ReadEngagedWith(Player* lead, Player* judge, Player* factLead, Unit* unit,")
        self.assertIn("fact.health = other->GetHealth();", read)
        self.assertIn("fact.maxHealth = other->GetMaxHealth();", read)
        self.assertIn("fact.partyHits = ep && ep->victims.count(fact.guid) != 0;", read)
        self.assertIn("fact.evading = other->IsCreature() && static_cast<Creature*>(other)->IsInEvadeMode();", read)

    def test_boss_search_reads_the_instance_when_asked(self):
        facts = body(CODE, "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead,")
        self.assertIn("if (step->kind == ai::PlanStepKind::Boss && slot.run.bossSearchWanted && "
                      "slot.run.step < slot.run.plan->steps.size() &&\n                step == &slot.run.plan->steps[slot.run.step])\n"
                      "                ReadBossInInstance(*step, lead, f);", facts)
        search = body(CODE, "void ReadBossInInstance(ai::PlanStep const& step, Player* lead,")
        self.assertIn("f.bossSearchRead = true;", search)
        self.assertIn("std::vector<uint32> const& spawns = BossSpawnLowGuids(lead->GetMapId(), step.entry);", search)
        self.assertIn("map->GetCreature(data->GetObjectGuid(low))", search)
        self.assertIn("f.bossSearchAlive = true;", search)
        self.assertIn("f.bossSearchDead = true;", search)
        self.assertNotIn("GetCreatureListWithEntryInGrid", search)
        # A spawn not in the map whose grid is not loaded is noted, never loaded for the search.
        self.assertIn("if (!creature && !map->IsLoaded(data->position.x, data->position.y))\n        {\n"
                      "            f.bossSearchUnloaded = true;", search)
        self.assertNotIn("LoadGrid", search)
        # The step's own spawn (the row nearest to the step's point) lying dead is boss down whatever another row does.
        self.assertIn("f.bossSearchNoSpawn = spawns.empty();", search)
        self.assertIn("if (low == own)\n            f.bossSearchOwnDead = true;", search)
        spawns = body(CODE, "std::vector<uint32> const& BossSpawnLowGuids(uint32 mapId, uint32 entry)")
        self.assertIn("std::lock_guard<std::mutex> guard(lock);", spawns)
        self.assertIn("if (it != cache.end())\n        return it->second;", spawns)
        self.assertIn("sObjectMgr.DoCreatureData(worker);", spawns)

    def test_boss_search_reads_inactive_game_events(self):
        # Scarlet Graveyard r23 slot 7: Scorn's only spawn belongs to event 81 (Scourge Invasion), never started. The
        # search marks a boss whose every spawn row is in a game event that is not active, only when it saw him neither
        # alive nor dead; one row outside such an event (none, a removal event, a running one) clears the mark.
        self.assertIn('#include "GameEventMgr.h"', COMMANDS)
        # Codex review round 1: every association of a guid (it can sit in several game_event_creature rows), not
        # only the first that GetGameEventId<Creature> returns.
        lists = body(CODE, "struct GameEventSpawnLists : GameEventMgr", "\n};\n")
        self.assertIn("return mgr.*(&GameEventSpawnLists::mGameEventCreatureGuids);", lists)
        event = body(CODE, "std::vector<std::vector<int>> CreatureSpawnGameEvents(std::vector<uint32> const& lows)")
        self.assertIn("sPoolMgr.IsPartOfTopPool<Creature>(lows[i])", event)
        self.assertIn("sGameEventMgr.GetGameEventId<Pool>(topPool)", event)
        self.assertNotIn("GetGameEventId<Creature>", event)
        self.assertIn("int const size = static_cast<int>(sGameEventMgr.GetEventMap().size());", event)
        self.assertIn("for (uint32 guid : lists[list])", event)
        self.assertIn("int const event = static_cast<int>(list) + 1 - size;", event)
        self.assertIn("if (it != index.end() && event != 0)\n                events[it->second].push_back(event);", event)
        search = body(CODE, "void ReadBossInInstance(ai::PlanStep const& step, Player* lead,")
        self.assertIn("if (!spawns.empty() && !f.bossSearchAlive && !f.bossSearchDead)", search)
        self.assertIn("TortoiseBots::DungeonRun::SpawnEventsOff(\n            CreatureSpawnGameEvents(spawns), ownIndex,\n"
                      "            [](std::uint32_t id) { return sGameEventMgr.IsActiveEvent(static_cast<uint16>(id)); });",
                      search)
        self.assertIn("if (gate.off)\n        {\n            f.bossSearchEventOff = true;", search)
        self.assertIn("f.bossSearchEventId = gate.eventId;", search)
        self.assertIn("f.bossSearchEventName = events[gate.eventId].description;", search)
        # Read after the alive/dead loop, so the mark sees what the loop found.
        self.assertLess(search.index("f.bossSearchDead = true;"), search.index("CreatureSpawnGameEvents(spawns)"))

    def test_resume_of_a_held_unit_is_not_refused(self):
        # Codex review round 1: the pause for a unit held at 1 hp is resumed like any other.
        self.assertNotIn("unit-cannot-die", CODE)
        self.assertNotIn("hpHeldPaused", CODE)

if __name__ == "__main__":
    unittest.main()
