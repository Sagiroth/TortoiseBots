#pragma once
#include "playerbot/PlayerbotAI.h"
#include "MovementActions.h"

namespace ai
{
    class SummonAction : public MovementAction
    {
    public:
        SummonAction(PlayerbotAI* ai, std::string name = "summon") : MovementAction(ai, name) {}
        bool isPossible() override { return true; }
        bool isUseful() override { return true; }
        bool isUsefulWhenStunned() override { return true; }
        virtual bool Execute(Event& event) override;
        // A spot beside the summoner for player, as the summon teleport picks it (ground on the
        // summoner's floor, in the summoner's sight). No side effects; the navmesh-island unstick
        // (MovementAction::TryPlaceUnstick) uses it with its own combat-keeping teleport.
        static bool FindSpotBeside(Player* summoner, Player* player, float followRange, float followAngle,
            float& outX, float& outY, float& outZ);

    protected:
        bool Teleport(Player* requester, Player *summoner, Player *player);
        bool SummonUsingGos(Player* requester, Player *summoner, Player *player);
        bool SummonUsingNpcs(Player* requester, Player *summoner, Player *player);
    };

    class UseMeetingStoneAction : public SummonAction
    {
    public:
        UseMeetingStoneAction(PlayerbotAI* ai) : SummonAction(ai, "use meeting stone") {}
        virtual bool Execute(Event& event) override;
    };

    class AcceptSummonAction : public Action
    {
    public:
        AcceptSummonAction(PlayerbotAI* ai) : Action(ai, "accept summon") {}
        virtual bool Execute(Event& event) override;
    };
}
