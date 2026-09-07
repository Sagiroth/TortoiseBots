#pragma once
#include "DungeonTriggers.h"
#include "GenericTriggers.h"

namespace ai
{
    class NaxxramasEnterDungeonTrigger : public EnterDungeonTrigger
    {
    public:
        NaxxramasEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter naxxramas", "naxxramas", 533) {}
    };

    class NaxxramasLeaveDungeonTrigger : public LeaveDungeonTrigger
    {
    public:
        NaxxramasLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave naxxramas", "naxxramas", 533) {}
    };

    class FourHorsemanStartFightTrigger : public StartBossFightTrigger
    {
    public:
        FourHorsemanStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start four horseman fight", "four horseman", 16062) {}
    };

    class FourHorsemanEndFightTrigger : public EndBossFightTrigger
    {
    public:
        FourHorsemanEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end four horseman fight", "four horseman", 16062) {}
    };

    // 4H void zone: Lady Blaumeux summons creature 16697 (90s TEMPSUMMON,
    // boss_four_horsemen.cpp:567). Radius is a conservative estimate —
    // no DBC/script radius in evidence; tune from the trail (T: lines)
    // after the first 4H run. Matches the lava-bomb hazard pattern.
    class FourHorsemanVoidZoneTrigger : public CloseToCreatureHazardTrigger
    {
    public:
        FourHorsemanVoidZoneTrigger(PlayerbotAI* ai) : CloseToCreatureHazardTrigger(ai, "void zone too close", 16697, 10.0f, 90) {}
    };
}