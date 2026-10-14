"""A tactical order that breaks a stay leaves the bot following again.

Compiles the production RelaxTacticalMovement from commands/BotCommands.cpp
and PlayerbotAI::EndStayForOrder from ai/playerbot/PlayerbotAI.cpp against a
stand-in PlayerbotAI (strategy sets per engine, the real movement-mode list
from MovementModePolicy.h). Also checks that the per-bot `.bot attack` and
the whispered `attack` end the stay before they attack, as the party-wide
`.bot action` forms do. Requires g++ (skipped when unavailable).
"""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "commands/BotCommands.cpp").read_text()
AI_SOURCE = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
ATTACK_SOURCE = (ROOT / "ai/playerbot/strategy/actions/AttackAction.cpp").read_text()


def extract(signature, next_signature, source=SOURCE):
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


def extract_definition(signature, next_signature, source=SOURCE):
    # A forward declaration shares the definition's prefix ("...;" vs the
    # definition's body); the definition is the occurrence not followed by
    # a semicolon on its declaration line.
    pos = 0
    while True:
        start = source.index(signature, pos)
        eol = source.index("\n", start)
        if not source[start:eol].rstrip().endswith(";"):
            break
        pos = start + len(signature)
    end = source.index(next_signature, start)
    return source[start:end]


FIXTURE = r"""
#include "ai/playerbot/MovementModePolicy.h"
#include <cassert>
#include <map>
#include <set>
#include <sstream>
#include <string>

enum class BotState { BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT, BOT_STATE_DEAD, BOT_STATE_REACTION };

struct PlayerbotAI
{
    std::map<BotState, std::set<std::string>> engines;
    bool HasStrategy(std::string const& name, BotState state) { return engines[state].count(name) != 0; }
    void ChangeStrategy(std::string const& names, BotState state)
    {
        std::stringstream in(names);
        std::string entry;
        while (std::getline(in, entry, ','))
        {
            if (entry[0] == '+') engines[state].insert(entry.substr(1));
            else if (entry[0] == '-') engines[state].erase(entry.substr(1));
        }
    }
    bool HasActiveMovementStrategy()
    {
        for (char const* mode : ai::kMovementModeStrategies)
            if (HasStrategy(mode, BotState::BOT_STATE_NON_COMBAT) || HasStrategy(mode, BotState::BOT_STATE_REACTION))
                return true;
        return false;
    }
    std::string GetDefaultMovementStrategy() { return "follow"; }
    void EndStayForOrder();
    int droppedQueuedOrders = 0;
    size_t DropQueuedMovementOrders() { ++droppedQueuedOrders; return 0; }
};

%(code)s

int main()
{
    using S = BotState;
    // Whispered stay (SetMovementStrategy("stay")): stay in all three engines.
    PlayerbotAI stayed;
    for (S s : { S::BOT_STATE_REACTION, S::BOT_STATE_NON_COMBAT, S::BOT_STATE_COMBAT })
        stayed.engines[s] = { "stay" };
    stayed.engines[S::BOT_STATE_NON_COMBAT].insert("follow jump");
    RelaxTacticalMovement(&stayed);
    // A tactical order also drops the movement orders still queued from chat.
    assert(stayed.droppedQueuedOrders == 1);
    assert(stayed.HasStrategy("follow", S::BOT_STATE_NON_COMBAT));
    assert(stayed.HasStrategy("follow", S::BOT_STATE_REACTION));
    assert(!stayed.HasStrategy("stay", S::BOT_STATE_NON_COMBAT));
    assert(!stayed.HasStrategy("stay", S::BOT_STATE_COMBAT));
    assert(!stayed.HasStrategy("follow", S::BOT_STATE_COMBAT));

    // Stay plus a passive hold: the passive is not a mode, so the order must
    // not count it and leave the bot without one.
    PlayerbotAI passive;
    passive.engines[S::BOT_STATE_NON_COMBAT] = { "stay", "passive" };
    passive.engines[S::BOT_STATE_COMBAT] = { "stay", "passive" };
    RelaxTacticalMovement(&passive);
    assert(passive.HasStrategy("follow", S::BOT_STATE_NON_COMBAT));
    assert(!passive.HasStrategy("passive", S::BOT_STATE_NON_COMBAT));
    assert(!passive.HasStrategy("passive", S::BOT_STATE_COMBAT));

    // A guard order is a mode of its own and is kept as it is.
    PlayerbotAI guarded;
    guarded.engines[S::BOT_STATE_NON_COMBAT] = { "guard" };
    RelaxTacticalMovement(&guarded);
    assert(guarded.HasStrategy("guard", S::BOT_STATE_NON_COMBAT));
    assert(!guarded.HasStrategy("follow", S::BOT_STATE_NON_COMBAT));

    // Whispered attack: AttackMyTargetAction calls EndStayForOrder directly.
    PlayerbotAI whispered;
    for (S s : { S::BOT_STATE_REACTION, S::BOT_STATE_NON_COMBAT, S::BOT_STATE_COMBAT })
        whispered.engines[s] = { "stay" };
    whispered.EndStayForOrder();
    assert(whispered.HasStrategy("follow", S::BOT_STATE_NON_COMBAT));
    assert(whispered.HasStrategy("follow", S::BOT_STATE_REACTION));
    assert(!whispered.HasStrategy("stay", S::BOT_STATE_NON_COMBAT));
    assert(!whispered.HasStrategy("stay", S::BOT_STATE_COMBAT));
    // A second call (a .bot order already ended the stay) changes nothing.
    whispered.EndStayForOrder();
    assert(whispered.HasStrategy("follow", S::BOT_STATE_NON_COMBAT));
    assert(!whispered.HasStrategy("follow", S::BOT_STATE_COMBAT));

    // Whispered attack after a flee plus stay: the passive hold ends with
    // the stay, like the .bot orders, so the rotation is not vetoed and
    // the bot rejoins afterwards.
    PlayerbotAI fled;
    fled.engines[S::BOT_STATE_NON_COMBAT] = { "stay", "passive" };
    fled.engines[S::BOT_STATE_COMBAT] = { "stay", "passive" };
    fled.EndStayForOrder();
    assert(fled.HasStrategy("follow", S::BOT_STATE_NON_COMBAT));
    assert(!fled.HasStrategy("passive", S::BOT_STATE_NON_COMBAT));
    assert(!fled.HasStrategy("passive", S::BOT_STATE_COMBAT));
    return 0;
}
"""


class TacticalStayReleaseTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_broken_stay_returns_to_follow(self):
        code = extract("void PlayerbotAI::EndStayForOrder()", "std::string PlayerbotAI::HandleRemoteCommand(", AI_SOURCE)
        # End at the function's own closing brace: helpers defined after it
        # (the pull checks and snapshots) need types this stand-in lacks.
        code += extract_definition("static void RelaxTacticalMovement(PlayerbotAI* ai)", "\n}\n") + "\n}\n"
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / "fixture.cpp"
            src.write_text(FIXTURE % {"code": code})
            binary = pathlib.Path(tmp) / "fixture"
            subprocess.run(["g++", "-std=c++17", "-I", str(ROOT), str(src), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_per_bot_attack_breaks_stay(self):
        # `.bot attack <Name>` must end a stay like `.bot action attack`.
        body = extract("static bool HandleAttack(", "static bool IsPublicFormation(")
        self.assertRegex(body, r'HandleNamedAction\([^;]*"attack my target"[^;]*,\s*true\)')
        named = extract("static bool HandleNamedAction(", "// `.bot action flee`")
        relax = named.index("RelaxTacticalMovement(ai)")
        self.assertLess(relax, named.index("DoSpecificAction(action"))

    def test_whispered_attack_breaks_stay(self):
        # A whispered "attack" (AttackMyTargetAction) ends a stay before it
        # attacks, like the .bot orders.
        body = extract("bool AttackMyTargetAction::Execute(", "\n}\n", ATTACK_SOURCE)
        end_stay = body.index("ai->EndStayForOrder()")
        self.assertLess(end_stay, body.index("Attack(requester, tgt, true)"))


if __name__ == "__main__":
    unittest.main()
