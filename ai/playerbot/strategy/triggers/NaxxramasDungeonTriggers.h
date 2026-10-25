#pragma once
#include "DungeonTriggers.h"
#include "GenericTriggers.h"

namespace ai
{

    class ThaddiusPolarityTrigger : public Trigger
    {
    public:
        ThaddiusPolarityTrigger(PlayerbotAI* ai) : Trigger(ai, "thaddius polarity split", 1) {}
        bool IsActive() override;
    };

    class PatchwerkMeleeTrigger : public Trigger
    {
    public:
        PatchwerkMeleeTrigger(PlayerbotAI* ai) : Trigger(ai, "patchwerk melee unsafe", 1) {}
        bool IsActive() override;
    };

    class KelThuzadInterruptTrigger : public Trigger
    {
    public:
        KelThuzadInterruptTrigger(PlayerbotAI* ai) : Trigger(ai, "kt frostbolt ready", 1) {}
        bool IsActive() override;
    };

    class GrobbulusCloudTrigger : public Trigger
    {
    public:
        GrobbulusCloudTrigger(PlayerbotAI* ai) : Trigger(ai,"grobbulus cloud nearby",1) {}
        bool IsActive() override;
    };

    class LocustSwarmTrigger : public Trigger
    {
    public:
        LocustSwarmTrigger(PlayerbotAI* ai) : Trigger(ai, "locust swarm active", 1) {}
        bool IsActive() override;
    };

    class MaexxnaRescueTrigger : public Trigger
    {
    public:
        MaexxnaRescueTrigger(PlayerbotAI* ai) : Trigger(ai, "maexxna rescue needed", 1) {}
        bool IsActive() override;
    };

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