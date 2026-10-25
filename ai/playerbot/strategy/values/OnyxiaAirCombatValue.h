#pragma once
#include "playerbot/OnyxiaAirCombat.h"
#include "playerbot/strategy/Value.h"
namespace ai
{
class OnyxiaAirCombatStateValue : public ManualSetValue<OnyxiaAirCombatState&>
{
public:
    OnyxiaAirCombatStateValue(PlayerbotAI* ai) : ManualSetValue<OnyxiaAirCombatState&>(ai, state, "onyxia air combat state") {}
private:
    OnyxiaAirCombatState state;
};
}
