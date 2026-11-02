#pragma once
#include "playerbot/PlayerbotAI.h"
#include "MovementActions.h"
#include "playerbot/policy/class/ResistAuraPolicy.h"
#include "playerbot/strategy/values/HazardsValue.h"

namespace ai
{
    class MoveAwayFromHazard : public MovementAction
    {
    public:
        MoveAwayFromHazard(PlayerbotAI* ai, std::string name = "move away from hazard") : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
        bool isPossible() override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "move away from hazard"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot move away from hazardous areas in dungeons.\n"
                   "It identifies dangerous positions and navigates to a safer location.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {"hazards"}; }
#endif

    private:
        bool IsHazardNearby(const WorldPosition& point, const std::list<HazardPosition>& hazards) const;
    };

    class MoveAwayFromCreature : public MovementAction
    {
    public:
        MoveAwayFromCreature(PlayerbotAI* ai, std::string name, uint32 creatureID, float range) : MovementAction(ai, name), creatureID(creatureID), range(range) {}
        bool Execute(Event& event) override;
        bool isPossible() override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "move away from creature"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot move away from a specific creature in dungeons.\n"
                   "It maintains a safe distance from the specified creature ID within a defined range.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {"hazards"}; }
#endif

    private:
        bool IsValidPoint(const WorldPosition& point, const std::list<Creature*>& creatures, const std::list<HazardPosition>& hazards);
        bool HasCreaturesNearby(const WorldPosition& point, const std::list<Creature*>& creatures) const;
        bool IsHazardNearby(const WorldPosition& point, const std::list<HazardPosition>& hazards) const;

    private:
        uint32 creatureID;
        float range;
    };

    // Generic move-away-from-debuffed-player primitive (donor
    // MoveAwayFromPlayerWithDebuffAction shape): raid scripts instantiate
    // it per boss (e.g. Geddon Living Bomb) via a spell-id/range subclass.
    // Everyone EXCEPT the carrier steps out of the blast radius; the
    // carrier's own escape stays RaidBombRunoutAction.
    class MoveAwayFromPlayerWithDebuff : public MovementAction
    {
    public:
        MoveAwayFromPlayerWithDebuff(PlayerbotAI* ai, std::string name, uint32 spellId, float range) : MovementAction(ai, name), spellId(spellId), range(range) {}
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }

    private:
        uint32 spellId;
        float range;
    };

    // Universal raid survival: bomb/plague runout. Runs once directly away
    // from the centre of the living same-map group until no member is
    // within BombRunoutDistance, then stays put (BombRunoutPolicy.h).
    class RaidBombRunoutAction : public MovementAction
    {
    public:
        RaidBombRunoutAction(PlayerbotAI* ai, std::string name = "raid bomb runout") : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };

    // Four Horsemen mark rotation (Naxxramas): at 3+ stacks of one mark
    // the bot moves to the next horseman's corner clockwise (fewest own
    // stacks first). Tanks and DPS switch to that horseman, healers stand
    // off it toward the room. Rules: FourHorsemenRotationPolicy.h.
    class FourHorsemenRotateAction : public MovementAction
    {
    public:
        FourHorsemenRotateAction(PlayerbotAI* ai, std::string name = "four horsemen rotate") : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
        // A stay order (.bot action stay, a pull hold) vetoes the move in
        // any engine state: the player parked this bot on purpose.
        bool isUseful() override
        {
            return MovementAction::isUseful() &&
                !ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) &&
                !ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT);
        }
    };

    // Universal raid survival: dragon flank. Sidesteps out of the frontal
    // breath cone and rear tail cone to the boss's flank; tanks hold the
    // head via the explicit .bot raid tankface command instead.
    class DragonFlankAction : public MovementAction
    {
    public:
        DragonFlankAction(PlayerbotAI* ai, std::string name = "dragon flank") : MovementAction(ai, name) {}
        // A bot a stand set seats (ai_boss_stand) is never picked as a reaction: picking it already stops the bot's
        // move and cast, which would cancel the walk to the seat every tick.
        bool isUseful() override;
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };

    // Resist-aura auto-swap (mod-playerbots parity): switch the
    // paladin to the matching resistance upkeep strategy (combat only,
    // like the donor) and cast it now. Inherit plain Action, not
    // ChangeAllStrategyAction: the manual "aura fire/shadow/..." strategies
    // only maintain auras out of combat, and persisting the swap into the
    // non-combat engine would overwrite the player's resting aura.
    class SwapFireResistanceAuraAction : public Action
    {
    public:
        SwapFireResistanceAuraAction(PlayerbotAI* ai) : Action(ai, "swap fire resistance aura") {}
        bool Execute(Event& event) override;
    };

    class SwapShadowResistanceAuraAction : public Action
    {
    public:
        SwapShadowResistanceAuraAction(PlayerbotAI* ai) : Action(ai, "swap shadow resistance aura") {}
        bool Execute(Event& event) override;
    };

    // Fight over: undo the swap above. Puts the "aura" placeholder back in
    // the combat engine (dropping rfire/rshadow) and removes the resist
    // aura so the spec aura returns through the normal "no paladin aura"
    // path. A manual aura order is left as is.
    class RestoreSpecAuraAction : public Action
    {
    public:
        RestoreSpecAuraAction(PlayerbotAI* ai) : Action(ai, "restore spec aura") {}
        bool Execute(Event& event) override;
    };

    // Vaelastrasz Burning Adrenaline refinement (mod-playerbots parity):
    // flees by weighted repulsion from non-BA bots (BA carriers cluster),
    // the boss's victim holds while Vael lives, tail-sweep recovery past
    // 30y walks back, ranged past 25y blends toward the boss. Queued by the
    // universal "raid bomb debuff" trigger only while +vael is active; the
    // generic anchor-flee stays the fallback for every other bomb.
    class VaelBurningAdrenalineFleeAction : public MovementAction
    {
    public:
        VaelBurningAdrenalineFleeAction(PlayerbotAI* ai, std::string name = "vael adrenaline flee") : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };

    // Universal raid survival: ranged spread. Steps 10-12yd away from the
    // nearest stacked friendly so chain abilities cannot bracket casters.
    class RaidSpreadAction : public MovementAction
    {
    public:
        RaidSpreadAction(PlayerbotAI* ai, std::string name = "raid spread") : MovementAction(ai, name) {}
        // A bot a stand set seats (ai_boss_stand) is never picked as a reaction: picking it already stops the bot's
        // move and cast, which would cancel the walk to the seat every tick.
        bool isUseful() override;
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };

    // Explicit tank command: face the current dragon boss away from the
    // raid anchor so breath/cleave point at a wall, not the raid.
    class DragonTankFaceAwayAction : public MovementAction
    {
    public:
        DragonTankFaceAwayAction(PlayerbotAI* ai, std::string name = "dragon tank face away") : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };
}
