#pragma once
#include "playerbot/PlayerbotAI.h"
#include "MovementActions.h"
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

    // Universal raid survival: bomb/plague runout. Flees AWAY from the raid
    // anchor (not toward a member like FleeAction) so the 30yd detonation
    // cannot bracket the clump. Reuses the flee-distance config knob.
    class RaidBombRunoutAction : public MovementAction
    {
    public:
        RaidBombRunoutAction(PlayerbotAI* ai, std::string name = "raid bomb runout") : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };

    // Universal raid survival: dragon flank. Sidesteps out of the frontal
    // breath cone and rear tail cone to the boss's flank; tanks hold the
    // head via the explicit .bot raid tankface command instead.
    class DragonFlankAction : public MovementAction
    {
    public:
        DragonFlankAction(PlayerbotAI* ai, std::string name = "dragon flank") : MovementAction(ai, name) {}
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };

    // Universal raid survival: ranged spread. Steps 10-12yd away from the
    // nearest stacked friendly so chain abilities cannot bracket casters.
    class RaidSpreadAction : public MovementAction
    {
    public:
        RaidSpreadAction(PlayerbotAI* ai, std::string name = "raid spread") : MovementAction(ai, name) {}
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
