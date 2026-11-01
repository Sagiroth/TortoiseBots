#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"
#include "AttackAction.h"

namespace ai
{
    class OnyxiasLairEnableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiasLairEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable onyxia's lair strategy", "+onyxia's lair") {}
    };

    class OnyxiasLairDisableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiasLairDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable onyxia's lair strategy", "-onyxia's lair") {}
    };

    class OnyxiaEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiaEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable onyxia fight strategy", "+onyxia") {}
    };

    class OnyxiaDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiaDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable onyxia fight strategy", "-onyxia") {}
    };

    // Deep Breath safe-zone dodge: run to the nearest of the 2 safe spots
    // for the breath lane (mod-playerbots parity). Lane from boss facing
    // (triggered casts never hit the cast slot); holds when already inside.
    class OnyxiaBreathSafeZoneAction : public MovementAction
    {
    public:
        OnyxiaBreathSafeZoneAction(PlayerbotAI* ai) : MovementAction(ai, "onyxia breath safe zone") {}
        bool Execute(Event& event) override;
    };

    // Optional whelp and egg tactics (AiPlayerbot.OnyxiaWhelpsAndEggs, on
    // by default). Shared helpers: one pass over the cached attackers list.
    struct OnyxiaWhelpScan
    {
        bool bossAirborne = false;
        Unit* whelp = nullptr; // nearest pickable live whelp (not within an egg's trap circle)
    };
    OnyxiaWhelpScan ScanOnyxiaWhelps(PlayerbotAI* ai, Player* bot);
    // True when this bot is on whelp duty (AoE-capable ranged DPS).
    bool IsOnyxiaWhelpDuty(PlayerbotAI* ai, Player* bot);
    // Spawned Onyxia Eggs from the cached game object search.
    std::vector<GameObject*> CollectOnyxiaEggs(PlayerbotAI* ai);
    // Unbroken egg whose trap circle (plus margin) holds the bot.
    GameObject* FindOnyxiaEggInDanger(PlayerbotAI* ai, Player* bot);

    // Phase 2: take the nearest whelp as the target (mod-playerbots parity:
    // RaidOnyxiaKillWhelpsAction), only for bots on whelp duty.
    class OnyxiaKillWhelpsAction : public AttackAction
    {
    public:
        OnyxiaKillWhelpsAction(PlayerbotAI* ai) : AttackAction(ai, "onyxia kill whelps") {}
        bool Execute(Event& event) override;
    };

    // Whelp hold: once on a whelp, the generic assist/AoE re-pick would
    // swing the bot back to Onyxia the next tick.
    class OnyxiaWhelpHoldMultiplier : public Multiplier
    {
    public:
        OnyxiaWhelpHoldMultiplier(PlayerbotAI* ai) : Multiplier(ai, "onyxia whelp hold") {}
        float GetValue(Action* action) override;
    };

    // Step back out of an egg cave (mod-playerbots parity:
    // OnyxiaAvoidEggsAction) before the trap circle hatches whelps.
    class OnyxiaAvoidEggsAction : public MovementAction
    {
    public:
        OnyxiaAvoidEggsAction(PlayerbotAI* ai) : MovementAction(ai, "onyxia avoid eggs") {}
        bool Execute(Event& event) override;
    };
}
