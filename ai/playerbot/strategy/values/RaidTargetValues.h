#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    // Name-based boss lookup (mod-playerbots parity: "find target" /
    // "boss target" from TargetValue.h / ValueContext.h). The qualifier is
    // a lowercase creature-name substring ("loatheb", "anub'rekhan").
    // Searches the shared "attackers" list first (cheap, already cached),
    // then falls back to a 100yd grid sweep for bosses the raid is fighting
    // but that have not hit this bot yet (phase detection before aggro).
    class FindTargetByNameValue : public UnitCalculatedValue, public Qualified
    {
    public:
        FindTargetByNameValue(PlayerbotAI* ai, std::string name = "find target")
            : UnitCalculatedValue(ai, name, 1), Qualified() {}

    protected:
        Unit* Calculate() override;
    };

    // Nearest hostile world-boss unit (rank CREATURE_ELITE_WORLDBOSS),
    // regardless of name. Used by per-fight suppression multipliers to arm
    // only while a real boss is engaged.
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
