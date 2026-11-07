#pragma once
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/Trigger.h"

namespace ai
{
    // The "boss tactics" strategy is on for a bot while it is on a map with rows
    // and the gate allows it (BossTacticsWanted: AiPlayerbot.DungeonTactics, or
    // the bot is in a group a remote commander has bound). Same enter/leave shape
    // as the per-dungeon strategies, re-checked every 5 ticks.
    class BossTacticsEnterTrigger : public Trigger
    {
    public:
        BossTacticsEnterTrigger(PlayerbotAI* ai) : Trigger(ai, "enter boss tactics", 5) {}
        bool IsActive() override;
    };

    class BossTacticsLeaveTrigger : public Trigger
    {
    public:
        BossTacticsLeaveTrigger(PlayerbotAI* ai) : Trigger(ai, "leave boss tactics", 5) {}
        bool IsActive() override;
    };

    // A listed spell is being cast and this bot has an interrupt for it.
    class BossTacticsInterruptTrigger : public Trigger
    {
    public:
        BossTacticsInterruptTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics interrupt", 1) {}
        bool IsActive() override;
    };

    // A protected aura was applied after an area cure started: the bot-cast Abolish aura or the bot's own cleansing
    // totem of that type would remove it on the next tick or pulse.
    class BossTacticsDispelSweepTrigger : public Trigger
    {
    public:
        BossTacticsDispelSweepTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics dispel sweep", 1) {}
        bool IsActive() override;
    };

    // A kill_first creature is alive and this DPS bot is not on it. Checked every second under
    // AiPlayerbot.DungeonRunTotemFocus (a totem's whole life is 1-2 s), every two seconds otherwise.
    class BossTacticsKillFirstTrigger : public Trigger
    {
    public:
        BossTacticsKillFirstTrigger(PlayerbotAI* ai)
            : Trigger(ai, "boss tactics kill first", sPlayerbotAIConfig.dungeonRunTotemFocus ? 1 : 2) {}
        bool IsActive() override;
    };

    // A cursed healer or off-tank whose Shade is in reach and whose raid can spare it (AiPlayerbot.DungeonRunCarrierStrike;
    // the guard is CarrierStrikeAllowed). Checked every second while a carrier row is open.
    class BossTacticsCarrierStrikeTrigger : public Trigger
    {
    public:
        BossTacticsCarrierStrikeTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics carrier strike", 1) {}
        bool IsActive() override;
    };

    // This DPS bot's current target is a boss entry a hold_kill row holds (its blocker still lives).
    class BossTacticsHoldKillTrigger : public Trigger
    {
    public:
        BossTacticsHoldKillTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics hold kill", 1) {}
        bool IsActive() override;
    };

    // This free DPS bot's current target is the subject of an active dps_hold_after hold: stop and wait it out.
    class BossTacticsDpsHoldTrigger : public Trigger
    {
    public:
        BossTacticsDpsHoldTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics dps hold", 1) {}
        bool IsActive() override;
    };

    // A linked_death group is held and this free DPS bot is not on the equalise pick (the highest member, no member at
    // or below param while another is above it).
    class BossTacticsLinkedDeathTrigger : public Trigger
    {
    public:
        BossTacticsLinkedDeathTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics linked death", 1) {}
        bool IsActive() override;
    };

    // This bot carries a freeze_on_aura row's aura: it stands still and starts nothing until the aura ends.
    class BossTacticsFreezeTrigger : public Trigger
    {
    public:
        BossTacticsFreezeTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics freeze", 1) {}
        bool IsActive() override;
    };

    // This bot carries a flee_on_aura row's aura: `away` needs it clear of every living group member by the row's
    // radius, `to_tank` needs it near the main tank (a tank carrier has nothing to do).
    class BossTacticsFleeTrigger : public Trigger
    {
    public:
        BossTacticsFleeTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics flee", 1) {}
        bool IsActive() override;
    };

    // A creature in the fight casts a spell an avoid_cast row lists and this bot (not a tank) stands inside its radius.
    class BossTacticsAvoidCastTrigger : public Trigger
    {
    public:
        BossTacticsAvoidCastTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics avoid cast", 1) {}
        bool IsActive() override;
    };

    // A stand set applies to this bot (ai_boss_stand: the boss is at an anchor casting the row's window spell)
    // and the bot is not at its seat.
    class BossTacticsStandTrigger : public Trigger
    {
    public:
        BossTacticsStandTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics stand", 1) {}
        bool IsActive() override;
    };

    // The bot stands in the front of a boss with a listed frontal attack.
    class BossTacticsFrontTrigger : public Trigger
    {
    public:
        BossTacticsFrontTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics front", 2) {}
        bool IsActive() override;
    };

    // A use_object row is due or nearly due and this bot holds the record's one clicker claim.
    class BossTacticsUseObjectTrigger : public Trigger
    {
    public:
        BossTacticsUseObjectTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics use object", 1) {}
        bool IsActive() override;
    };

    // A swap_on_aura row is at its stack count on the tank the boss is on and this bot is the next rotation tank:
    // it taunts the boss off (both DungeonRun keys and the row's own rules gate it).
    class BossTacticsTankSwapTrigger : public Trigger
    {
    public:
        BossTacticsTankSwapTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics tank swap", 1) {}
        bool IsActive() override;
    };

    // The tank a swap moved off still stands on the boss (its victim or its current target) and the stand-down runs:
    // the bot drops the boss and keeps every action aimed at it stood down until its stacks or the window end.
    class BossTacticsTankSwapHoldTrigger : public Trigger
    {
    public:
        BossTacticsTankSwapHoldTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics tank swap hold", 1) {}
        bool IsActive() override;
    };

    // This tank is the one the boss is on and the boss stands too far from the row's object: the walk over starts.
    class BossTacticsObjectAnchorTrigger : public Trigger
    {
    public:
        BossTacticsObjectAnchorTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics object anchor", 2) {}
        bool IsActive() override;
    };

    // This bot is a seeker (no poison, not a tank, not the boss's victim) and the seek_aura row has a phase for it:
    // a cloud of the row's to stand in, or the gather around the source before it dies.
    class BossTacticsSeekTrigger : public Trigger
    {
    public:
        BossTacticsSeekTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics seek", 1) {}
        bool IsActive() override;
    };

    // The second off-tank has a source to take, to pull, or to drag back to the row's spot.
    class BossTacticsSeekTankTrigger : public Trigger
    {
    public:
        BossTacticsSeekTankTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics seek tank", 1) {}
        bool IsActive() override;
    };

    // A free DPS bot may damage the row's source right now (the pre-damage, or the burn) and is not on it.
    class BossTacticsSeekKillTrigger : public Trigger
    {
    public:
        BossTacticsSeekKillTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics seek kill", 1) {}
        bool IsActive() override;
    };

    // This hunter or warlock still has a pet out and the siphon is close or landing.
    class BossTacticsSeekPetTrigger : public Trigger
    {
    public:
        BossTacticsSeekPetTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics seek pet", 1) {}
        bool IsActive() override;
    };

    // This shaman still has a totem down and the siphon is close or landing.
    class BossTacticsSeekTotemTrigger : public Trigger
    {
    public:
        BossTacticsSeekTotemTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics seek totem", 1) {}
        bool IsActive() override;
    };

    // A taunt_after_cast row (mode until_tank) names this tank and its subject's victim is no tank any more: the tank
    // takes the subject back inside the row's deadline.
    class BossTacticsTauntAfterTrigger : public Trigger
    {
    public:
        BossTacticsTauntAfterTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics taunt after", 1) {}
        bool IsActive() override;
    };

    // A burn_mana row's boss is in the fight at or above the row's start % of mana and this bot is one of the row's
    // burners (FindBossBurnMana): it casts its longest drain spell on the boss.
    class BossTacticsBurnManaTrigger : public Trigger
    {
    public:
        BossTacticsBurnManaTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics burn mana", 1) {}
        bool IsActive() override;
    };

    // This melee bot's current target is held by a ranged_only row (the subject stands the row's gap above it): it
    // drops the target for the adds.
    class BossTacticsRangedOnlyTrigger : public Trigger
    {
    public:
        BossTacticsRangedOnlyTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics ranged only", 1) {}
        bool IsActive() override;
    };
}
