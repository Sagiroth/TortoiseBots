#pragma once
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/Trigger.h"

// The escape of a taunt-immune boss's non-tank victim (AiPlayerbot.DungeonRunImmuneBossVictimEscape; the rules are in
// playerbot/VictimEscapePolicy.h). Every bot carries both triggers: the reaction engine's "avoid aoe" and the combat
// engine's "dungeon" strategy. No boss is named: the gate is a boss (a world boss or a plan's boss entry) the game
// makes immune to taunts, with a non-tank as its victim.
namespace ai
{
    // A boss the game makes immune to taunts whose victim is this member, or null.
    Unit* ImmuneBossOnVictim(Unit* member);

    // This bot is such a boss's non-tank victim.
    class ImmuneBossVictimTrigger : public Trigger
    {
    public:
        ImmuneBossVictimTrigger(PlayerbotAI* ai) : Trigger(ai, "immune boss victim", 1) {}
        bool IsActive() override;
    };

    // This paladin has a group member within 30 yd that is such a boss's non-tank victim.
    class ImmuneBossVictimProtectTrigger : public Trigger
    {
    public:
        ImmuneBossVictimProtectTrigger(PlayerbotAI* ai) : Trigger(ai, "immune boss victim protect", 1) {}
        bool IsActive() override;
    };

    // The victim's own escape: Feign Death, Vanish, Ice Block or Divine Shield, cast at once (the bot's own cast is
    // broken for it).
    class ImmuneBossVictimEscapeAction : public Action
    {
    public:
        ImmuneBossVictimEscapeAction(PlayerbotAI* ai) : Action(ai, "immune boss victim escape") {}
        bool ShouldReactionInterruptCast() const override { return true; }
        // Ice Block and Divine Shield go out while stunned (the plan reads Feign Death and Vanish as not ready then).
        bool isUsefulWhenStunned() override { return true; }
        bool Execute(Event& event) override;
    };

    // The paladin's Blessing (on this core: Hand) of Protection on a victim with no escape of its own ready.
    class ImmuneBossVictimProtectAction : public Action
    {
    public:
        ImmuneBossVictimProtectAction(PlayerbotAI* ai) : Action(ai, "bop immune boss victim") {}
        bool ShouldReactionInterruptCast() const override { return true; }
        bool Execute(Event& event) override;
    };
}
