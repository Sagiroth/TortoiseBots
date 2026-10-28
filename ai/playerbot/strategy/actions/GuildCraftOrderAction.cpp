#include "playerbot/playerbot.h"
#include "GuildCraftOrderAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/CraftValues.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "runtime/GuildOrderStore.h"

using namespace ai;

uint32 GuildCraftOrderAction::FindCraftSpell(uint32 itemId)
{
    for (auto& [spellId, spellState] : bot->GetSpellMap())
    {
        if (spellState.state == PLAYERSPELL_REMOVED || spellState.disabled || IsPassiveSpell(spellId))
            continue;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (!pSpellInfo)
            continue;

        for (int i = 0; i < 3; ++i)
        {
            if (pSpellInfo->Effect[i] == SPELL_EFFECT_CREATE_ITEM && pSpellInfo->EffectItemType[i] == itemId)
                return spellId;
        }
    }

    return 0;
}

bool GuildCraftOrderAction::Execute(Event& event)
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    uint32 itemId = order.itemId;
    if (!itemId || !OrderStillNeedsWork(bot, order))
        return false;

    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId);
    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    uint32 delivered = (stored && stored->itemId == itemId) ? stored->delivered : 0;
    uint32 craftSpellId = FindCraftSpell(itemId);
    if (!craftSpellId)
    {
        ai->TellDebug(ai->GetMaster(), "Guild craft order: I don't know how to craft: " + order.target, "debug travel");
        return false;
    }

    if (!ai->HasCheat(BotCheatMask::item) && !AI_VALUE2(bool, "can craft spell", craftSpellId))
    {
        ai->TellDebug(ai->GetMaster(), "Guild craft order: missing reagents for: " + order.target, "debug travel");
        return false;
    }

    // Calculate the amount to craft (what was already delivered counts).
    uint32 remaining = order.amount - std::min(order.amount, delivered + currentCount);

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return false;

    uint32 newItemId = pSpellInfo->EffectItemType[0];
    if (newItemId)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(newItemId);
        if (proto && remaining > proto->GetMaxStackSize())
            remaining = proto->GetMaxStackSize();
    }

    if (remaining < 1)
        remaining = 1;

    std::ostringstream cmd;
    cmd << "castnc " << craftSpellId << " " << remaining;
    ai->HandleCommand(CHAT_MSG_WHISPER, cmd.str(), *bot);

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    ai->TellDebug(ai->GetMaster(), "Guild craft order: crafting " + std::to_string(remaining) + "x " + std::string(proto ? proto->Name1 : order.target), "debug travel");

    return true;
}

bool GuildCraftOrderAction::isUseful()
{
    if (!bot->GetGuildId())
        return false;

    if (bot->IsMoving())
        return false;

    if (bot->IsMounted())
        return false;

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    return order.itemId && OrderStillNeedsWork(bot, order);
}