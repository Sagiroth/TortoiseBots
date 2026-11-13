"""Wiring pins for the summon commands' pet hand-over (issue #1606).

The rules are pure and tested in tools/test_summon_pet_follow_policy.cpp (a live pet of a bot, nothing already pending,
and a landing the core will resummon at; everything else keeps today's behaviour). These pins only prove the live
summon code feeds those facts and hands the pet to the core's own relocation before its TeleportTo, and gives it back
when TeleportTo refuses: the player's `.bot summon` / `.bot cmd summon` flow (PlayerConvenience::UpdateSummons) and
the bot's own whisper `summon` order (SummonAction::Teleport).
Run: python3 -m unittest tools/test_summon_pet_follow_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONVENIENCE = (ROOT / "behavior/PlayerConvenience.cpp").read_text()
SUMMON = (ROOT / "ai/playerbot/strategy/actions/UseMeetingStoneAction.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/SummonPetFollowPolicy.h").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def policy_facts(text):
    """The normalised argument list of the one SummonTeleportMovesPet call in text."""
    found = re.findall(r"ai::SummonTeleportMovesPet\((.*?)\);", text, re.S)
    assert len(found) == 1, found
    return re.sub(r"\s+", " ", found[0])


class SummonPetFollowWiring(unittest.TestCase):
    def test_the_bot_summon_hands_the_pet_to_the_core_before_its_teleport(self):
        # The reported case is the summon command: PlayerConvenience moves the bot.
        update = body(CONVENIENCE, "void PlayerConvenience::UpdateSummons(uint32 diff)")
        self.assertIn("bot->UnsummonPetTemporaryIfAny();", update,
            "the bot summon leaves a live pet behind; it walks through the instance to its owner and pulls mobs (#1606)")
        self.assertIn('#include "../ai/playerbot/SummonPetFollowPolicy.h"', CONVENIENCE)
        # The pet is read before it is handed over, and the decision is the pure policy.
        pet = update.index("Pet* const pet = bot->GetPet();")
        decide = update.index("ai::SummonTeleportMovesPet(")
        unsummon = update.index("bot->UnsummonPetTemporaryIfAny();")
        teleport = update.index("if (!bot->TeleportTo(state.destMap, state.destX")
        self.assertLess(pet, decide)
        self.assertLess(decide, unsummon)
        self.assertLess(unsummon, teleport)
        # Handed over only while the policy says so.
        self.assertLess(update.rindex("if (petFollows)", 0, unsummon), unsummon)
        # TeleportTo refusing gives the pet back before the summon is erased.
        self.assertIn("bot->ResummonPetTemporaryUnSummonedIfAny();", update,
            "a refused summon teleport must give the bot its pet back")
        resummon = update.index("bot->ResummonPetTemporaryUnSummonedIfAny();")
        self.assertLess(teleport, resummon)
        self.assertLess(resummon, update.index("it = m_summons.erase(it);", teleport))
        # The facts: the bot's pet, alive, nothing pending, and a landing the core resummons at.
        facts = policy_facts(update)
        self.assertIn("pet != nullptr", facts)
        self.assertIn("pet && pet->IsAlive()", facts)
        self.assertIn("bot->GetTemporaryUnsummonedPetNumber() == 0", facts)
        self.assertIn("!bot->IsPetNeedBeTemporaryUnsummoned()", facts)

    def test_the_chat_summon_hands_a_bot_pet_to_the_core_before_the_teleport(self):
        summon = body(SUMMON, "bool SummonAction::Teleport(Player* requester, Player *summoner, Player *player)")
        self.assertIn("player->UnsummonPetTemporaryIfAny();", summon,
            "the summon action leaves a live pet behind; it walks through the instance to its owner and pulls mobs (#1606)")
        self.assertIn('#include "playerbot/SummonPetFollowPolicy.h"', SUMMON)
        pet = summon.index("Pet* const pet = player->GetPet();")
        decide = summon.index("ai::SummonTeleportMovesPet(")
        unsummon = summon.index("player->UnsummonPetTemporaryIfAny();")
        teleport = summon.index("if (!player->TeleportTo(mapId, x, y, z, 0))")
        self.assertLess(pet, decide)
        self.assertLess(decide, unsummon)
        self.assertLess(unsummon, teleport)
        self.assertLess(summon.rindex("if (petFollows)", 0, unsummon), unsummon)
        # A real player keeps the core's own pet rule: the summon never hands over a player's pet.
        self.assertIn("!isRealPlayer_Helper(player) &&", summon[decide - 200:decide])
        # A refused teleport gives the bot its pet back in the same branch.
        self.assertIn("player->ResummonPetTemporaryUnSummonedIfAny();", summon)
        resummon = summon.index("player->ResummonPetTemporaryUnSummonedIfAny();")
        self.assertLess(teleport, resummon)
        self.assertLess(resummon, summon.index("return false;", teleport))
        facts = policy_facts(summon)
        self.assertIn("pet != nullptr", facts)
        self.assertIn("pet && sServerFacade.IsAlive(pet)", facts)
        self.assertIn("player->GetTemporaryUnsummonedPetNumber() == 0", facts)
        self.assertIn("!player->IsPetNeedBeTemporaryUnsummoned()", facts)

    def test_the_policy_keeps_every_refusal(self):
        # The gates the C++ test exercises are all in the header the call sites use.
        self.assertIn("if (!hasPet || !petAlive)", POLICY)
        self.assertIn("return noPetPending && coreWillResummon;", POLICY)


if __name__ == "__main__":
    unittest.main()
