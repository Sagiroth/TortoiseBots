#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class AttackerWithoutAuraTargetValue : public UnitCalculatedValue, public Qualified
	{
	public:
        AttackerWithoutAuraTargetValue(PlayerbotAI* ai, std::string name = "attacker without aura", bool checkIsOwner = false) : UnitCalculatedValue(ai, name), Qualified(), checkIsOwner(checkIsOwner) {}

    protected:
        virtual Unit* Calculate() override;

    private:
        bool checkIsOwner;
	};

    // Same scan, but an aura cast by someone else does not count: the bot
    // wants an attacker that does not carry ITS OWN aura (per-caster DoTs).
    class AttackerWithoutMyAuraTargetValue : public AttackerWithoutAuraTargetValue
    {
    public:
        AttackerWithoutMyAuraTargetValue(PlayerbotAI* ai) : AttackerWithoutAuraTargetValue(ai, "attacker without my aura", true) {}
    };

    // Warlock DoT/curse spread: the same scan filtered by the raid rotation
    // policy (AiPlayerbot.WarlockRaidRotation), so a low-health or capped-out
    // attacker does not hide a later legal one. The trigger and its action
    // both resolve this value, so they always pick the same target.
    class WarlockSpreadAttackerTargetValue : public UnitCalculatedValue, public Qualified
    {
    public:
        WarlockSpreadAttackerTargetValue(PlayerbotAI* ai, std::string name, bool auraCheckIsOwner,
                                         bool capCheckIsOwner, bool raidUtility, bool ignoreHealth)
            : UnitCalculatedValue(ai, name), Qualified(), auraCheckIsOwner(auraCheckIsOwner),
              capCheckIsOwner(capCheckIsOwner), raidUtility(raidUtility), ignoreHealth(ignoreHealth) {}

    protected:
        Unit* Calculate() override;

    private:
        bool auraCheckIsOwner;
        bool capCheckIsOwner;
        bool raidUtility;
        bool ignoreHealth;
    };
}
