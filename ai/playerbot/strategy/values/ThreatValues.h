#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class MyThreatValue : public LogCalculatedValue<float>, public Qualified
    {
    public:
        MyThreatValue(PlayerbotAI* ai, std::string name = "my threat") : LogCalculatedValue(ai, name), Qualified() { minChangeInterval = 1; lastTarget = ObjectGuid(); }
        virtual bool EqualToLast(float value) override { return value == lastValue; }
        virtual float Calculate() override;

    public:
        ObjectGuid lastTarget;
    };

    class TankThreatValue : public FloatCalculatedValue, public Qualified
    {
    public:
        TankThreatValue(PlayerbotAI* ai, std::string name = "tank threat") : FloatCalculatedValue(ai, name), Qualified() {}
        virtual float Calculate() override;
    };

    class ThreatValue : public Uint8CalculatedValue, public Qualified
	{
	public:
        ThreatValue(PlayerbotAI* ai, std::string name = "threat") : Uint8CalculatedValue(ai, name), Qualified() {}
	virtual uint8 Calculate() override;
        static float GetThreat(Player* player, Unit* target);
        static float GetTankThreat(PlayerbotAI* ai, Unit* target);

    protected:
	uint8 Calculate(Unit* target);
    };

    // Whether the core makes this target immune to taunts: Creature::HasImmunityFlag(CREATURE_IMMUNITY_TAUNT) reads
    // creature_template.immunity_flags 0x02, which Creature.cpp applies to SPELL_EFFECT_ATTACK_ME and
    // SPELL_AURA_MOD_TAUNT (Hakkar 14834: no taunt has ever landed on him).
    bool TargetTauntImmune(Unit* target);
}
