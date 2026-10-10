#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class OssirianStartFightTrigger : public StartBossFightTrigger
    {
    public:
        OssirianStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start ossirian fight", "ossirian", 15339) {}
    };

    class OssirianEndFightTrigger : public EndBossFightTrigger
    {
    public:
        OssirianEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end ossirian fight", "ossirian", 15339) {}
    };

    // Crystal-run timing (mod-playerbots parity: Aq20MoveToCrystalTrigger).
    // Fires while Ossirian is in combat and either his Strength buff
    // (25176) is up, or the weakness debuff is nearly out (<5s), or the
    // bot must leave now to cover the run time plus the 5s crystal-arm
    // lead (inside the 30s window). Debuff IDs: 25177/78/80/81/83.
    class OssirianCrystalRunTrigger : public Trigger
    {
    public:
        OssirianCrystalRunTrigger(PlayerbotAI* ai, std::string name = "ossirian crystal run", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        bool IsActive() override;
    };
}
