#pragma once
#include "playerbot/strategy/Action.h"
namespace ai
{
    Unit* GetKelThuzadInterruptBoss(PlayerbotAI* ai);
    bool IsKelThuzadReservedInterrupt(PlayerbotAI* ai, uint32 spell);
    class KelThuzadInterruptAction : public Action
    {
    public:
        KelThuzadInterruptAction(PlayerbotAI* ai) : Action(ai, "kt frostbolt interrupt") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };
}
