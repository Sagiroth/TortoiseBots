#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/TrinketUsePolicy.h"
#include "UseItemAction.h"

#include <cstdint>
#include <map>
#include <utility>

namespace ai
{
    class UseTrinketAction : public UseAction
    {
    public:

        UseTrinketAction(PlayerbotAI* ai) : UseAction(ai, "use trinket") {}
        virtual bool Execute(Event& event) override;
        virtual bool isPossible() override;
        virtual bool isUseful() override { return UseAction::isUseful() && !bot->HasStealthAura(); }

    private:
        bool UseTrinket(Player* requester, Item* item);
        TrinketEffectClass ClassifyTrinketSpell(SpellEntry const* spellInfo);

        // Donor per-item + per-category cooldown memory (server ms).
        std::map<std::pair<uint32, uint32>, uint32> itemCooldownExpiries;
        std::map<uint32, uint32> categoryCooldownExpiries;
    };
}