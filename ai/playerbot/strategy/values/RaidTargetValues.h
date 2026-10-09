#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    // Name-based boss lookup (mod-playerbots parity: "find target" /
    // "boss target" from TargetValue.h / ValueContext.h). The qualifier is
    // the full lowercase creature name ("ossirian the unscarred") —
    // case-insensitive full equality (ai::RaidNameMatches), never a
    // substring. Searches the group-wide "attackers" list only, like the
    // donor's threat-list search: non-null implies engagement, with no
    // grid fallback (unengaged units must not arm suppression).
    class FindTargetByNameValue : public UnitCalculatedValue, public Qualified
    {
    public:
        FindTargetByNameValue(PlayerbotAI* ai, std::string name = "find target")
            : UnitCalculatedValue(ai, name, 1), Qualified() {}

    protected:
        Unit* Calculate() override;
    };

    // Nearest engaged hostile world-boss unit (rank
    // CREATURE_ELITE_WORLDBOSS + IsInCombat), regardless of name. Used by
    // per-fight suppression multipliers to arm only while a real boss is
    // engaged — dormant worldbosses nearby do not qualify.
    class BossTargetValue : public UnitCalculatedValue, public Qualified
    {
    public:
        BossTargetValue(PlayerbotAI* ai, std::string name = "boss target")
            : UnitCalculatedValue(ai, name, 2), Qualified() {}

    protected:
        Unit* Calculate() override;
    };

    // Per-fight action suppression flag (mod-playerbots parity: "neglect
    // threat" from ThreatValues.h). Fight multipliers Set it on every
    // evaluation while their boss is engaged; ThreatMultiplier Gets it and
    // skips threat gating so the fight tactic owns target choice.
    // Read-once like the donor: Get returns the flag and resets to false,
    // so a stale set cannot leak past the fight that set it — which means
    // fight code must Set per evaluation, not once on strategy add.
    class NeglectThreatValue : public ManualSetValue<bool>
    {
    public:
        NeglectThreatValue(PlayerbotAI* ai, std::string name = "neglect threat")
            : ManualSetValue<bool>(ai, false, name) {}

        bool Get() override
        {
            bool set = value;
            Reset();
            return set;
        }
    };
}
