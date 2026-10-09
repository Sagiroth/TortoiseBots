#pragma once

#include <cstdint>

// Warlock curse default + conflict rule (WAR-3 + WAR-8, mod-playerbots
// parity): destruction opens Curse of the Elements while affliction and
// demonology stay on Curse of Agony (donor GenericWarlockStrategy: affli
// Agony, destro Elements; Curse of Shadow stays a manual pick — making it
// the affliction default would eat a raid debuff slot on every pull).
// Vanilla holds one curse per target, so the "no curse" gate also refuses
// while ANY warlock curse sits on the target, not just the bot's own:
// a second warlock never overwrites the first's curse with its default.
// A manual `curse X` order still fires through its own per-curse trigger,
// so the player always wins.
//
// The rule is a pure function of spec + the target's curse state and is
// unit-tested on its own (tools/test_warlock_curse_policy.cpp).

namespace TortoiseBots
{

enum class WarlockSpec
{
    Affliction,
    Demonology,
    Destruction,
};

enum class WarlockCurseDecision
{
    LeaveAlone,      // target already cursed (mine or another warlock's)
    CurseOfAgony,    // affliction / demonology default
    CurseOfTheElements, // destruction default
};

struct WarlockCurseInputs
{
    WarlockSpec spec = WarlockSpec::Affliction;
    bool knowsDefaultCurse = false; // HasSpell(CoA) / HasSpell(CoE)
    bool targetHasAnyCurse = false; // any curse-family aura, any caster
};

inline WarlockCurseDecision DecideWarlockCurse(WarlockCurseInputs const& inputs)
{
    if (!inputs.knowsDefaultCurse)
        return WarlockCurseDecision::LeaveAlone;
    if (inputs.targetHasAnyCurse)
        return WarlockCurseDecision::LeaveAlone;
    if (inputs.spec == WarlockSpec::Destruction)
        return WarlockCurseDecision::CurseOfTheElements;
    return WarlockCurseDecision::CurseOfAgony;
}

} // namespace TortoiseBots
