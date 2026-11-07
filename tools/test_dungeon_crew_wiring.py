"""A dungeon crew bot keeps its group and its slot (bot LFG part 2).

The pure decisions (GroupHygienePolicy FarAwayLeaveAllowed/RandomGroupInviteAllowed,
RunReleaseHold::Decide with the on-instance-map flag) are tested in
tools/test_group_hygiene_policy.cpp and tools/test_run_release_hold_policy.cpp;
this checks the call sites use them, since those files only build against the core:
- LeaveFarAwayAction refuses for a crew bot (Dungeon activity lease) via
  ai::FarAwayLeaveAllowed, before anything can return true;
- "invite nearby" refuses for a crew bot via ai::RandomGroupInviteAllowed,
  before the group-size check (GrouperType grouperType);
- "invite guild" is defined in the .cpp (not the header) and refuses for a
  crew bot via ai::RandomGroupInviteAllowed while keeping the
  randomBotGuildNearby gate;
- AutoReleaseSpiritAction passes the bot's instanceable map into
  RunReleaseHold::Decide, and the verdict gate still sits before the stock rules.
- RepopAction keeps a dungeon crew bot in its group via ai::RepopLeavesGroup.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LEAVE = (ROOT / "ai/playerbot/strategy/actions/LeaveGroupAction.cpp").read_text()
INVITE_H = (ROOT / "ai/playerbot/strategy/actions/InviteToGroupAction.h").read_text()
INVITE = (ROOT / "ai/playerbot/strategy/actions/InviteToGroupAction.cpp").read_text()
ACTION = (ROOT / "ai/playerbot/strategy/actions/ReleaseSpiritAction.h").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class DungeonCrewWiringTest(unittest.TestCase):
    def test_leave_far_away_refuses_dungeon_crew(self):
        fn = body(LEAVE, "bool LeaveFarAwayAction::isUseful()", "\n    }\n}")
        self.assertIn("TortoiseBots::BotActivity::Dungeon", fn)
        guard = fn.index("ai::FarAwayLeaveAllowed(")
        self.assertLess(guard, fn.index("return true;"))

    def test_invite_nearby_refuses_dungeon_crew(self):
        fn = body(INVITE, "bool InviteNearbyToGroupAction::isUseful()", "\n    }\n}")
        guard = fn.index("ai::RandomGroupInviteAllowed(")
        self.assertLess(guard, fn.index("GrouperType grouperType"))

    def test_invite_guild_refuses_dungeon_crew(self):
        self.assertIn("virtual bool isUseful() override;", body(INVITE_H, "class InviteGuildToGroupAction", "};"))
        self.assertNotIn("randomBotGuildNearby; };", INVITE_H)
        fn = body(INVITE, "bool InviteGuildToGroupAction::isUseful()", "\n    }\n}")
        self.assertIn("ai::RandomGroupInviteAllowed(", fn)
        self.assertIn("randomBotGuildNearby", fn)

    def test_only_the_group_leader_invites_or_converts_to_raid(self):
        guild = body(INVITE, "bool InviteGuildToGroupAction::isUseful()", "\n    }\n}")
        self.assertIn("ai->HasRealPlayerMaster()", guild)
        self.assertIn("group->IsLeader(bot->GetObjectGuid())", guild)
        invite = body(INVITE, "bool InviteToGroupAction::Invite(", "bool JoinGroupAction::Execute(")
        self.assertLess(invite.index("group->IsLeader(inviter->GetObjectGuid())"), invite.index("ConvertToRaid()"))
    def test_release_passes_the_instance_map(self):
        auto = body(ACTION, "class AutoReleaseSpiritAction", "class CorpseRunAction")
        useful = body(auto, "virtual bool isUseful() override", "\n        }\n")
        gate = useful.index("RunReleaseHold::Decide(")
        self.assertLess(gate, useful.index("bot->GetMap()->Instanceable()"))
        self.assertLess(useful.index("bot->GetMap()->Instanceable()"), useful.index("if (verdict =="))

    def test_repop_keeps_dungeon_crew_group(self):
        rep = body(ACTION, "class RepopAction", "class SelfResurrectAction")
        ex = body(rep, "virtual bool Execute(Event& event) override", "virtual bool isUseful() override")
        guard = ex.index("ai::RepopLeavesGroup(")
        self.assertIn("TortoiseBots::BotActivity::Dungeon", ex[guard:ex.index("RemoveMember(", guard)])
        self.assertLess(guard, ex.index("group->RemoveMember("))
        self.assertIn('#include "playerbot/policy/rpg/GroupHygienePolicy.h"', ACTION)


if __name__ == "__main__":
    unittest.main()
