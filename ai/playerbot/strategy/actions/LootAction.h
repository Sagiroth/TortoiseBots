#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/LootObjectStack.h"
#include "MovementActions.h"

namespace ai
{
    class LootAction : public MovementAction
    {
    public:
        LootAction(PlayerbotAI* ai) : MovementAction(ai, "loot") {}
        virtual bool Execute(Event& event) override;
    };

    class OpenLootAction : public MovementAction
    {
    public:
        OpenLootAction(PlayerbotAI* ai) : MovementAction(ai, "open loot") {}
        virtual bool Execute(Event& event) override;

    private:
        bool DoLoot(LootObject& lootObject);
        uint32 GetOpeningSpell(LootObject& lootObject);
        uint32 GetOpeningSpell(LootObject& lootObject, GameObject* go);
        bool CanOpenLock(LootObject& lootObject, const SpellEntry* pSpellInfo, GameObject* go);
        bool CanOpenLock(uint32 skillId, uint32 reqSkillValue);
    };

    class StoreLootAction : public Action
    {
    public:
        StoreLootAction(PlayerbotAI* ai) : Action(ai, "store loot") {}
        virtual bool Execute(Event& event) override;
        static bool IsLootAllowed(ItemQualifier& itemQualifier, PlayerbotAI *ai);
    };

    class ReleaseLootAction : public MovementAction
    {
    public:
        ReleaseLootAction(PlayerbotAI* ai) : MovementAction(ai, "release loot") {}
        virtual bool Execute(Event& event) override;
    };

    // Bot-only green/blue drop boost (AiPlayerbot.BotLootRateUncommon/Rare).
    // Called once per kill from the xpgain handler, before the corpse can be opened: rolls
    // the creature's own loot template for quality-2/3 entries again at (multiplier - 1) x
    // the entry's chance and adds the hits, so the bot finds them in the loot window.
    void ApplyBotLootBonus(PlayerbotAI* ai, Player* bot, ObjectGuid victimGuid);
}
