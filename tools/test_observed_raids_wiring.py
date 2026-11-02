"""Raids tab module wiring: the observed-raid sampler runs outside the binding
sweep, never touches a bound group's book, and the RAIDS datagram and the
roster's raid flag are wired where the spec puts them."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADAPTER = (ROOT / "host" / "BotHostAdapter.cpp").read_text()
COMMANDER = (ROOT / "commands" / "Commander.cpp").read_text()
OBSERVED = (ROOT / "commands" / "ObservedRaids.cpp").read_text()
EMITTER = (ROOT / "runtime" / "ObservabilityEmitter.cpp").read_text()
CONF = (ROOT / "conf" / "tortoise_bots.conf.dist").read_text()
AI_CONF = (ROOT / "ai" / "playerbot" / "aiplayerbot.conf.dist.in").read_text()
CONFIG_H = (ROOT / "ai" / "playerbot" / "PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai" / "playerbot" / "PlayerbotAIConfig.cpp").read_text()
LOGGING = (ROOT / "docs" / "server" / "settings" / "logging.md").read_text()
CMAKE = (ROOT / "TortoiseBots.cmake").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def definition(text, head):
    # The body of the first `head` that opens a brace before any ';' (skips forward declarations).
    i = text.index(head)
    while True:
        rest = text[i:]
        if rest.index("{") < rest.index(";"):
            return rest[:rest.index("\n}\n")]
        i = text.index(head, i + len(head))


class ObservedRaidsWiringTest(unittest.TestCase):
    def test_sampler_runs_outside_the_binding_sweep(self):
        # UpdateRemoteCommanders returns early while nothing is bound, so the
        # observed-raid sampler is called by the host adapter after it.
        code = strip_comments(ADAPTER)
        update = definition(code, "void BotHostAdapter::OnUpdate(")
        self.assertIn("UpdateRemoteCommanders(diff);", update)
        self.assertIn("UpdateObservedRaids(diff);", update)
        self.assertLess(update.index("UpdateRemoteCommanders(diff);"), update.index("UpdateObservedRaids(diff);"),
                        "the observed sampler runs after the binding sweep")
        self.assertNotIn("UpdateObservedRaids", strip_comments(COMMANDER),
                         "the sampler is never inside UpdateRemoteCommanders")

    def test_sampler_gates_on_the_raid_flag_then_the_monitor(self):
        code = strip_comments(OBSERVED)
        sampler = definition(code, "void UpdateObservedRaids(")
        self.assertIn("RaidsEnabled()", sampler)
        self.assertIn("SyncConfig(", sampler)
        enabled = "CombatMonitorService::Instance().Enabled()"
        self.assertIn(enabled, sampler)
        self.assertLess(sampler.index("RaidsEnabled()"), sampler.index("SyncConfig("),
                        "the raid flag is read before the monitor sync")
        self.assertLess(sampler.index("SyncConfig("), sampler.index(enabled),
                        "the sync runs before the enabled check")

    def test_sampler_never_feeds_a_bound_group(self):
        code = strip_comments(OBSERVED)
        sampler = definition(code, "void UpdateObservedRaids(")
        self.assertIn("GroupIsBound(", sampler)
        self.assertLess(sampler.index("GroupIsBound("), sampler.index("NoteBinding("),
                        "a bound group's book belongs to the bound path")
        self.assertLess(sampler.index("GroupIsBound("), sampler.index("Refresh("),
                        "no double Refresh of a bound book")
        # The binding table lives in Commander.cpp; the predicate is its FindByGroup lookup.
        export = definition(strip_comments(COMMANDER), "bool GroupIsBound(")
        self.assertIn("FindByGroup(", export)

    def test_sampler_feeds_only_the_groups_own_copy(self):
        # A member in another instance or on another map is not in the raid: the filter runs before the combat and death
        # read and before the boss anchor, and it reads the copy the collection selected for the group.
        code = strip_comments(OBSERVED)
        sampler = definition(code, "void UpdateObservedRaids(")
        self.assertIn("OnSelectedCopy(", sampler)
        self.assertIn("RaidSet()[groupId]", sampler, "the selected copy is the collected one")
        self.assertLess(sampler.index("OnSelectedCopy("), sampler.index("MemberFighting("),
                        "the filter runs before the combat and death read")
        self.assertLess(sampler.index("OnSelectedCopy("), sampler.index("NoteFightBoss("),
                        "the boss anchor is a member of the selected copy")

    def test_off_turn_closes_the_books_it_fed(self):
        # `.reload config` turning the flag off closes the open observed fights at that moment and drops the sampler's
        # groups, instead of leaving a window open on the tab.
        sampler = definition(strip_comments(OBSERVED), "void UpdateObservedRaids(")
        self.assertIn("TurnedOff(", sampler)
        self.assertIn("ReleaseBooks(", sampler)
        self.assertIn("CloseUnfinished(", sampler)
        self.assertIn("Forget(", sampler)
        self.assertLess(sampler.index("TurnedOff("), sampler.index("ReleaseBooks("))
        self.assertLess(sampler.index("ReleaseBooks("), sampler.index("RaidSet().clear()"))
        self.assertIn("RaidAway().clear()", sampler)
        self.assertLess(sampler.index("RaidSet().clear()"), sampler.index("RaidAway().clear()"))
        # The datagram side: one empty list after the last one that listed a group, then nothing.
        emit = definition(strip_comments(EMITTER), "void ObservabilityEmitter::EmitRaids(")
        off = emit[:emit.index("if (!m_raidProvider")]
        self.assertIn("if (!m_raidsEnabled)", off)
        self.assertIn("if (m_raidsSent)", off)
        self.assertIn("SendDatagram(BuildRaidsPayload(", off)
        self.assertIn("m_raidsSent = false;", off)

    def test_released_binding_keeps_an_observed_book(self):
        code = strip_comments(COMMANDER)
        forget = definition(code, "static void ForgetReleasedCombatGroups()")
        self.assertIn("IsObservedRaidGroup(", forget)

    def test_datagram_and_roster_field_wiring(self):
        code = strip_comments(EMITTER)
        update = definition(code, "void ObservabilityEmitter::Update(")
        self.assertIn("EmitCommanders();", update)
        self.assertIn("EmitRaids(", update)
        self.assertLess(update.index("EmitCommanders();"), update.index("EmitRaids("),
                        "RAIDS lands after the commanders of the same cycle")
        self.assertIn("SetRaidProvider(", ADAPTER)
        self.assertIn("ClearObservedRaids()", ADAPTER)
        self.assertIn("commands/ObservedRaids.cpp", CMAKE)

    def test_roster_writes_the_raid_flag_only_on_a_raid_map(self):
        code = strip_comments(EMITTER)
        fill = definition(code, "void FillLiveUnitSnapshot(")
        self.assertIn("IsRaid()", fill, "the flag is the core's map type")
        line = next(line for line in fill.splitlines() if "raidMap" in line)
        self.assertIn("GetMap()->IsRaid()", line)
        self.assertNotIn(".size()", line, "never a member-count rule")
        writer = definition(code, "void ObservabilityEmitter::EmitSnapshotCycle(")
        at = writer.index('\\"raid\\":true')
        before = writer[:at]
        self.assertIn("if (b.raidMap)", before[-80:], "the key is written only inside its guard")
        self.assertNotIn('"raid":false', writer, "an absent key means not a raid map")

    def test_config_flag_is_shipped_off(self):
        self.assertIn("AiPlayerbot.ObservabilityRaids = 0", CONF)
        self.assertIn("AiPlayerbot.ObservabilityRaids = 0", AI_CONF)
        self.assertIn("bool observabilityRaids = false;", CONFIG_H)
        self.assertIn('GetBoolDefault("AiPlayerbot.ObservabilityRaids", false)', CONFIG_CPP)
        self.assertIn("AiPlayerbot.ObservabilityRaids", LOGGING)

    def test_no_creature_entry_literals(self):
        # The boss pick reads ranks and flags from the core, never a hard-coded entry.
        numbers = re.findall(r"\b\d{4,5}\b", strip_comments(OBSERVED))
        self.assertEqual(numbers, [], numbers)


if __name__ == "__main__":
    unittest.main()
