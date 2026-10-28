"""The raid bind glue (commands/Commander.cpp): binding the commander's raid.

`.bot commander bind <LeadBot> raid` binds the commander's own raid of 6-40
bots with the commander outside it (runtime/RemoteCommanderPolicy.h: the raid
refusals, TakeBindRaid, KeepCommanderRaid). The glue reads the raid facts,
keeps the raid group as it is, marks the binding a raid, and applies the raid
layout (subgroups, main tank, assistants, tank healers' focus heal targets;
runtime/RaidLayoutPolicy.h). The release undoes exactly that layout.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def body_from(text, start):
    i = text.index(start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j]
    return text[i:]


SRC = strip_comments(COMMANDS)


class RaidBindWiringTest(unittest.TestCase):
    def test_bind_reads_the_raid_word_and_marks_the_binding(self):
        bind = body(SRC, "static bool HandleCommanderBind(", "\n}\n")
        inparty = bind.index("RemoteCommander::TakeBindInParty(names)")
        raid = bind.index("RemoteCommander::TakeBindRaid(names)")
        raid_fact = bind.index("facts.raid = raidBind;")
        lead_tank = bind.index("facts.leadIsTank = ")
        check = bind.index("RemoteCommander::CheckBind(facts)")
        mark = bind.index("binding.raid = raidBind;")
        table = bind.index("CommanderBindings().Bind(binding)")
        layout = bind.index("ApplyRaidLayout(")
        self.assertLess(inparty, raid)
        self.assertLess(raid, raid_fact)
        self.assertLess(raid_fact, lead_tank)
        self.assertLess(lead_tank, check)
        self.assertLess(check, mark)
        self.assertLess(mark, table)
        self.assertLess(table, layout)
        self.assertIn("BindGroupPlan::KeepCommanderRaid", bind)
        self.assertIn("raidBind ? names.size() != 1", bind)

    def test_binding_state_reads_the_raid_gates(self):
        state = body(SRC, "ReadBindingState(", "\n}\n")
        self.assertIn("state.raidAllowed = ", state)
        self.assertIn("state.leadGroupIsBg = group->isBGGroup();", state)

    def test_release_clears_the_layout_before_the_bots_leave(self):
        release = body(SRC, "ReleaseBinding(", "\n}\n")
        clear = release.index("ClearRaidLayout(")
        remove = release.find("RemoveMember(")
        if remove >= 0:
            self.assertLess(clear, remove)


class RaidLayoutWiringTest(unittest.TestCase):
    def test_apply_lays_out_the_raid(self):
        apply = body_from(SRC, "static void ApplyRaidLayout(Group* group, Player* lead, std::vector<Player*> const& bots, ChatHandler* handler)\n{")
        self.assertIn("RaidLayout::Plan(", apply)
        self.assertIn("ChangeMembersGroup(", apply)
        self.assertIn("SetMainTank(", apply)
        self.assertIn("SetAssistant(", apply)
        self.assertIn('"focus heal targets"', apply)
        self.assertIn("+focus heal targets", apply)

    def test_clear_undoes_the_layout_record(self):
        clear = body_from(SRC, "static void ClearRaidLayout(uint32 groupId)\n{")
        self.assertIn("SetAssistant(", clear)
        self.assertIn("-focus heal targets", clear)
        self.assertIn("SetMainTank(ObjectGuid())", clear)
        self.assertIn("layouts.erase(it)", clear)


if __name__ == "__main__":
    unittest.main()
