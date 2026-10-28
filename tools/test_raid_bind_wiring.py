"""The raid bind glue (commands/Commander.cpp): binding the commander's raid.

`.bot commander bind <LeadBot> raid` binds the commander's own raid of 6-39
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
STORE = (ROOT / "ai/playerbot/PlayerbotDbStore.cpp").read_text()


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
        self.assertIn('"raid"', bind.split("SendActionAck")[-1])

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
        self.assertIn("if (removed.raid)\n        ClearRaidLayout(removed.groupId);", COMMANDS)
        self.assertIn("RaidLayouts()[group->GetId()]", COMMANDS)

    def test_shutdown_clears_layouts_while_groups_exist(self):
        shutdown = body(SRC, "void ClearRemoteCommanders()", "\n}\n")
        clear = shutdown.index("ClearRaidLayout(binding->groupId)")
        bindings = shutdown.index("CommanderBindings().Clear()")
        layouts = shutdown.index("RaidLayouts().clear()")
        self.assertLess(clear, bindings)
        self.assertLess(bindings, layouts)

    def test_departed_follower_leaves_the_layout_record(self):
        departed = body(SRC, "void ReleaseDepartedFollowers(", "\n}\n")
        forget = departed.index("ForgetDepartedRaidMember(follower, binding.groupId);")
        drop = departed.index("CommanderBindings().RemoveFollower(commanderRaw, follower);")
        self.assertLess(forget, drop)

    def test_telemetry_names_only_the_bots_still_bound(self):
        collect = body(SRC, "void CollectCommanderTelemetry(", "\n}\n")
        fill = collect.index("FillCommandedBots(group, lead, ref.bots);")
        prune = collect.index("ref.layout = CommanderTelemetry::CurrentRaidLayout(ref.layout, members);")
        self.assertLess(fill, prune)


class RaidLayoutWiringTest(unittest.TestCase):
    def test_apply_lays_out_the_raid(self):
        apply = body_from(SRC, "static void ApplyRaidLayout(Group* group, Player* lead, std::vector<Player*> const& bots, ChatHandler* handler, uint64_t commanderRaw)\n{")
        self.assertIn("RaidLayout::Plan(", apply)
        self.assertIn("PlanSubgroupMoves(", apply)
        self.assertIn("SwapMembersGroup(", apply)
        self.assertIn("GetMemberGroup(", apply)
        self.assertIn("GetMemberSlots()", apply)
        self.assertIn("SetMainTank(", apply)
        self.assertIn("SetAssistant(", apply)
        self.assertIn("GetMainAssistantGuid()", apply)
        self.assertIn('"focus heal targets"', apply)
        self.assertIn("+focus heal targets", apply)
        self.assertIn('HasStrategy("focus heal targets"', apply)
        self.assertIn("applied.commanderRaw = commanderRaw;", apply)
        self.assertIn("TBM:RAID_LAYOUT|warning|", apply)

    def test_clear_undoes_the_layout_record(self):
        clear = body_from(SRC, "static void ClearRaidLayout(uint32 groupId)\n{")
        self.assertIn("SetAssistant(", clear)
        self.assertIn("PlanSubgroupMoves(", clear)
        self.assertIn("RestoreRaidFocusHealer(bot, record)", clear)
        restore = body_from(SRC, "static void RestoreRaidFocusHealer(Player* bot, RaidFocusHealer const& record)\n{")
        self.assertIn("-focus heal targets", restore)
        self.assertIn("record.previousTargets", restore)
        self.assertIn("record.hadStrategy", restore)
        self.assertIn("GetGroupById(groupId)", clear)
        self.assertIn("GetMemberGroup(", clear)
        self.assertIn("GetMemberSlots()", clear)
        self.assertIn("GetMainTankGuid() == applied.setMainTank", clear)
        self.assertIn("SetMainAssistant(", clear)
        self.assertIn("subgroups not restored: %s", clear)
        self.assertIn("layouts.erase(it)", clear)

    def test_assistant_slot_is_read_before_the_tank_is_restored(self):
        # Codex r3: the core's tank flag clears the assistant, so the slot is
        # read first and a later assistant who was the saved tank keeps it.
        clear = body_from(SRC, "static void ClearRaidLayout(uint32 groupId)\n{")
        read = clear.index("ObjectGuid const assistantNow = group->GetMainAssistantGuid();")
        self.assertLess(read, clear.index("group->SetMainTank(applied.previousMainTank);"))
        self.assertIn("assistantNow != applied.previousMainTank", clear)
        self.assertIn("&& assistantNow.IsEmpty())", clear)

    def test_departure_restores_the_healer_snapshot(self):
        forget = body_from(SRC, "static void ForgetDepartedRaidMember(uint64 followerRaw, uint32 groupId)\n{")
        self.assertIn("RestoreRaidFocusHealer(bot, *heals)", forget)
        self.assertIn("record.guid == guid", forget)

    def test_save_skips_layout_focus_heal(self):
        self.assertIn("IsRaidFocusHealer(ai->GetBot())", STORE)
        self.assertIn('strategies.remove(std::string_view("focus heal targets"))', STORE)


if __name__ == "__main__":
    unittest.main()
