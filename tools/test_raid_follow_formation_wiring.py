"""The raid follow formation is wired into every formation and the follow action.

Section M of BOTS-PATHING-WAYPOINTS: inside a dungeon or raid group with
AiPlayerbot.RaidFollowWedge on, followers use the near wedge behind the leader,
a far follower walks the leader's trail, and every formation refuses a spot
over a drop. The pure numbers live in RaidFollowWedgePolicy.h,
FollowSpotSafetyPolicy.h and FollowTrailPolicy.h (tools/
test_raid_follow_formation_policy.cpp); this test pins the call sites so a new
formation cannot skip the guard.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
FORMATIONS = (ROOT / "ai/playerbot/strategy/values/Formations.cpp").read_text()
FORMATIONS_H = (ROOT / "ai/playerbot/strategy/values/Formations.h").read_text()
ARROW = (ROOT / "ai/playerbot/strategy/values/Arrow.cpp").read_text()
FOLLOW = (ROOT / "ai/playerbot/strategy/actions/FollowActions.cpp").read_text()
FOLLOW_H = (ROOT / "ai/playerbot/strategy/actions/FollowActions.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
PLAYERBOT_AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
PLAYERBOT_AI_H = (ROOT / "ai/playerbot/PlayerbotAI.h").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/party.md").read_text()


def body(text, start, end="\n    };"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RaidFollowFormationWiringTest(unittest.TestCase):
    def test_every_formation_refuses_a_drop(self):
        spots = {
            "plain follow": body(FORMATIONS, "WorldLocation FollowFormation::GetLocation()", "\n}\n"),
            "near": body(FORMATIONS, "class NearFormation", "\n    };"),
            "chaos": body(FORMATIONS, "class ChaosFormation", "\n    };"),
            "circle": body(FORMATIONS, "class CircleFormation", "\n    };"),
            "line": body(FORMATIONS, "WorldLocation MoveFormation::MoveSingleLine(", "\n}\n"),
            "custom": body(FORMATIONS, "class CustomFormation", "\n    };"),
            "arrow": body(ARROW, "WorldLocation ArrowFormation::GetLocationInternal()", "\n}\n"),
        }
        for name, text in spots.items():
            with self.subTest(formation=name):
                self.assertIn("FinalizeFollowSpot(", text)
        # Near, the circle, the line rows and the arrow had the ground guard before the wedge and keep it everywhere.
        for name in ("near", "circle", "line", "arrow"):
            self.assertNotIn("UpdateAllowedPositionZ", spots[name], name)
            self.assertNotIn("UsesRaidFollowWedge(ai) && !FinalizeFollowSpot(", spots[name], name)
        # The formations that had no guard get the shared one only inside the instance wedge: outside it the
        # open-world follow stays as it was (issue #217's guard is the formations' own).
        for name in ("plain follow", "chaos", "custom"):
            self.assertIn("UsesRaidFollowWedge(ai)", spots[name], name)
        self.assertIn("bot->UpdateAllowedPositionZ(x, y, z);", spots["chaos"])
        # Shield and line build their spots through MoveLine -> MoveSingleLine.
        self.assertIn("return MoveSingleLine(", body(FORMATIONS, "WorldLocation MoveFormation::MoveLine(", "\n}\n"))

    def test_one_ground_guard(self):
        self.assertEqual(FORMATIONS.count("bool ai::FinalizeFollowSpot("), 1)
        self.assertIn("float& z, bool refuseVoid = false);", FORMATIONS_H)
        guard = body(FORMATIONS, "bool ai::FinalizeFollowSpot(", "\n}\n")
        self.assertIn("refuseVoid && ground <= INVALID_HEIGHT", guard)
        self.assertIn("ai::FollowSpotGroundRejected(fromZ, ground)", guard)
        self.assertIn("map->GetLosHitPosition(", guard)
        self.assertIn("bot->UpdateAllowedPositionZ(x, y, z);", guard)
        self.assertIn("std::clamp(z, minZ, maxZ)", guard)

    def test_wedge_is_the_near_formation_inside_instances(self):
        self.assertIn("bool ai::UsesRaidFollowWedge(PlayerbotAI* ai)", FORMATIONS)
        gate = body(FORMATIONS, "bool ai::UsesRaidFollowWedge(PlayerbotAI* ai)", "\n}\n")
        self.assertIn("sPlayerbotAIConfig.raidFollowWedge", gate)
        self.assertIn("map->IsDungeon() || map->IsRaid()", gate)
        self.assertIn("bot->GetGroup()", gate)
        value = body(FORMATIONS, "Formation* FormationValue::Get()", "\n}\n")
        self.assertIn("UsesRaidFollowWedge(ai)", value)
        self.assertIn("new NearFormation(ai)", value)
        self.assertIn("if (!userChosen", value)
        self.assertIn("bool FormationValue::LoadUserChoice(", FORMATIONS)
        self.assertIn("bool LoadUserChoice(std::string value);", FORMATIONS_H)

    def test_wedge_angle_and_rings_are_used(self):
        self.assertIn("RaidFollowWedgeAngle(start, index - 1, total - 1)", FORMATIONS)
        self.assertIn("RaidFollowWedgeRingOffset(followers, ai->IsTank(bot), ai->IsHeal(bot))", FORMATIONS)
        self.assertIn("sPlayerbotAIConfig.raidFollowDistance", FORMATIONS)

    def test_far_follower_walks_the_trail(self):
        self.assertIn("bool FollowAction::WalkLeadTrail(", FOLLOW)
        self.assertIn("WalkLeadTrail(followTarget, moved)", body(FOLLOW, "bool FollowAction::Execute(Event& event)", "\n}\n"))
        walk = body(FOLLOW, "bool FollowAction::WalkLeadTrail(", "\n}\n")
        self.assertIn("ai->GetLeadTrail()", walk)
        self.assertIn("ai::FollowTrailWaypoint(", walk)
        self.assertIn("ai::kRaidFollowCatchUpDistanceYd", walk)
        self.assertIn("bot->IsInCombat() || followTarget->IsInCombat()", walk)
        self.assertIn("std::vector<FollowTrailPoint> const& GetLeadTrail() const { return leadTrail; }", PLAYERBOT_AI_H)

    def test_trail_is_sampled_every_ai_tick(self):
        update = body(PLAYERBOT_AI, "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)", "\n}\n")
        self.assertIn("if (sPlayerbotAIConfig.raidFollowWedge)", update)
        self.assertIn("UpdateLeadTrail();", update)
        sample = body(PLAYERBOT_AI, "void PlayerbotAI::UpdateLeadTrail()", "\n}\n")
        self.assertIn("FollowTrailRecord(leadTrail,", sample)
        self.assertIn("map->IsDungeon() || map->IsRaid()", sample)

    def test_config_key(self):
        self.assertIn('config.GetBoolDefault("AiPlayerbot.RaidFollowWedge", false)', CONFIG_CPP)
        self.assertIn("bool raidFollowWedge;", CONFIG_H)
        self.assertIn("#AiPlayerbot.RaidFollowWedge = 0", TEMPLATE)
        self.assertIn("AiPlayerbot.RaidFollowWedge", SETTINGS)
        self.assertIn('config.GetFloatDefault("AiPlayerbot.RaidFollowDistance", 4.0f)', CONFIG_CPP)
        self.assertIn("#AiPlayerbot.RaidFollowDistance = 4.0", TEMPLATE)


if __name__ == "__main__":
    unittest.main()
