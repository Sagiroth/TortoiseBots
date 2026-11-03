#pragma once
#include "AttackAction.h"
#include "DungeonActions.h"

namespace ai
{
    bool ViscidusEnabled(PlayerbotAI* ai);
    bool IsViscidusShatterTarget(Player* player, Unit* target);
    bool ViscidusBlocksSpell(PlayerbotAI* ai, uint32 spellId, Unit* target);
    Unit* GetViscidusCombatTarget(PlayerbotAI* ai);
    void UpdateViscidus(PlayerbotAI* ai);
    std::list<HazardPosition> GetViscidusToxinHazards(PlayerbotAI* ai);
    bool InsideViscidusToxin(PlayerbotAI* ai);
    bool NeedsViscidusDistance(PlayerbotAI* ai);

    class ViscidusCombatAction : public AttackAction
    {
    public:
        explicit ViscidusCombatAction(PlayerbotAI* ai) : AttackAction(ai, "viscidus combat") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
        void Update();
        void Refresh();
        Unit* Boss();
        Unit* Target();
        std::list<HazardPosition> Clouds();
        bool RingPoint(WorldPosition& point);
        bool MoveRing();
        bool GlobBurst(Unit* target);
        bool KeepDistance();
        bool Retreat();
        bool TankRecovery(Unit* boss);
        bool CheckedRoute(WorldPosition const& point);
        bool RingFallback(WorldPosition& point);
    private:
        ObjectGuid bossGuid, encounterGuid;
        std::vector<ObjectGuid> globs, clouds;
        uint32 scanned = 0, instance = 0, ringReported = 0;
        bool initialized = false, engaged = false, wasSplit = false, retakePending = false;
        uint32 reformAt = 0, distanceReported = 0;
        bool fallbackValid = false;
        WorldPosition fallbackPoint, fallbackBoss;
    };

    class ViscidusDistanceAction : public MovementAction
    {
    public:
        explicit ViscidusDistanceAction(PlayerbotAI* ai) : MovementAction(ai, "viscidus keep distance") {}
        bool isUseful() override { return NeedsViscidusDistance(ai); }
        bool Execute(Event& event) override;
    };

    class ViscidusToxinEscapeAction : public MoveAwayFromHazard
    {
    public:
        explicit ViscidusToxinEscapeAction(PlayerbotAI* ai) : MoveAwayFromHazard(ai, "viscidus toxin escape") {}
        bool Execute(Event& event) override;
    };
}
