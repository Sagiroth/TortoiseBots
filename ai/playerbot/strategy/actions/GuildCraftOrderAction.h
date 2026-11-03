#pragma once
#include "playerbot/PlayerbotAI.h"
#include "MovementActions.h"
#include "playerbot/strategy/values/GuildValues.h"

namespace ai
{
    class GuildCraftOrderAction : public MovementAction
    {
    public:
        GuildCraftOrderAction(PlayerbotAI* ai) : MovementAction(ai, "guild craft order") {}

        bool Execute(Event& event) override;
        bool isUseful() override;

    private:
        uint32 FindCraftSpell(uint32 itemId);
        uint32 FindCraftEffect(uint32 spellId, uint32 itemId);
        uint32 CountFreeBagSlots();
        GameObject* FindFocusObject(uint32 focusId, float range);
    };
}
