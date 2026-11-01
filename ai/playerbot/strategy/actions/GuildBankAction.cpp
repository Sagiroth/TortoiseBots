#include "playerbot/playerbot.h"
#include "GuildBankAction.h"

#include "MailAction.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "runtime/GuildOrderDeliveryPolicy.h"
#include "runtime/GuildOrderDeliveryQueue.h"
#include "runtime/GuildOrderRefundQueue.h"
#include "runtime/GuildOrderSupplyQueue.h"
#include "runtime/GuildOrderStore.h"

using namespace ai;
using namespace TortoiseBots::GuildOrders;

namespace
{
    // Entries the core accepts as guild bankers outright; anything else must
    // carry the guild-banker gossip option (same list as the bank itself).
    bool IsGuildBankerEntry(uint32 entry)
    {
        switch (entry)
        {
            case 62008:
            case 62009:
            case 62010:
            case 62011:
            case 62012:
            case 80917:
            case 80918:
                return true;
            default:
                return false;
        }
    }

    bool HasGuildBankerGossip(Creature* banker)
    {
        if (!banker || !banker->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP))
            return false;
        GossipMenuItemsMapBounds menuItems =
            sObjectMgr.GetGossipMenuItemsMapBounds(banker->GetDefaultGossipMenuId());
        if (menuItems.first == menuItems.second)
            menuItems = sObjectMgr.GetGossipMenuItemsMapBounds(0);
        for (auto itr = menuItems.first; itr != menuItems.second; ++itr)
        {
            GossipMenuItems const& item = itr->second;
            if (item.option_id == GOSSIP_OPTION_GUILD_BANKER &&
                (item.npc_option_npcflag & banker->GetUInt32Value(UNIT_NPC_FLAGS)))
                return true;
        }
        return false;
    }

    bool IsGuildBanker(Creature* creature)
    {
        return creature && (IsGuildBankerEntry(creature->GetEntry()) || HasGuildBankerGossip(creature));
    }
}

bool GuildBankAction::isUseful()
{
    if (!bot->GetGuildId())
        return false;
    if (bot->IsInCombat() || bot->IsDead())
        return false;
    if (!ai::OrderBotMayTravel(ai, bot))
        return false;
    std::optional<OrderRecord> stored = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->guildId != bot->GetGuildId())
        return false;
    // A cancelled stub with unspent guild gold still uses the banker: the
    // travel leg sees no order, so the action itself must stay useful.
    if (stored->cancelled)
        return stored->guildGoldHeld > 0 && FindNearbyGuildBanker() != nullptr;
    bool bankSupplyEnabled = stored->supplyGoldCount ? sPlayerbotAIConfig.guildOrderBankGold :
        sPlayerbotAIConfig.guildOrderBankWithdraw;
    if (stored->state == OrderState::Working && stored->supplyStep == SupplyStep::Bank &&
        stored->supplyCount && bankSupplyEnabled)
        return FindNearbyGuildBanker() != nullptr;
    if (stored->state != OrderState::Delivering || !stored->itemId)
        return false;
    if (bot->GetItemCount(stored->itemId) == 0)
        return false;
    // Only at a banker: the travel leg brings the bot here, and the drain
    // re-checks before touching the bank. Anywhere else the action stays
    // quiet so the bot keeps walking instead of mailing from the field.
    if (FindNearbyGuildBanker() != nullptr)
        return true;
    // No banker in reach after repeated searches: a mailbox does, and the
    // drain mails the guild master from there.
    return MailLegActive() && !MailProcessor::FindMailbox(ai).IsEmpty();
}

Unit* GuildBankAction::FindNearbyGuildBanker()
{
    std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (ObjectGuid& guid : npcs)
    {
        Unit* npc = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
        if (npc && npc->IsCreature() && IsGuildBanker(npc->ToCreature()))
            return npc;
    }
    return nullptr;
}

bool GuildBankAction::MailLegActive() const
{
    return TortoiseBots::GuildOrders::DeliveryMailsAfterNoBanker(
        uint32(AI_VALUE2(int32, "manual int", "guild order no banker")));
}

bool GuildBankAction::Execute(Event& event)
{
    (void)event;
    std::optional<OrderRecord> stored = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->guildId != bot->GetGuildId())
        return false;
    // Unspent guild gold goes back first: a live order and a cancelled stub
    // both repay at any banker visit, before supply or delivery runs.
    if (stored->guildGoldHeld > 0 && bot->GetMoney() > 0 && FindNearbyGuildBanker())
        TortoiseBots::GuildOrders::RefundQueue::Instance().Enqueue(bot->GetGUIDLow());
    bool bankSupplyEnabled = stored->supplyGoldCount ? sPlayerbotAIConfig.guildOrderBankGold :
        sPlayerbotAIConfig.guildOrderBankWithdraw;
    if (stored->state == OrderState::Working && stored->supplyStep == SupplyStep::Bank &&
        stored->supplyCount && bankSupplyEnabled && FindNearbyGuildBanker())
        return TortoiseBots::GuildOrders::SupplyQueue::Instance().Enqueue(bot->GetGUIDLow());
    if (stored->state != OrderState::Delivering)
        return false;
    if (!stored->itemId || bot->GetItemCount(stored->itemId) == 0)
        return false;
    if (!FindNearbyGuildBanker() &&
        !(MailLegActive() && !MailProcessor::FindMailbox(ai).IsEmpty()))
        return false;
    // One request per bot pending; the drain runs one bank pass per world
    // tick and re-checks bot, guild and banker before each pass.
    return TortoiseBots::GuildOrders::DeliveryQueue::Instance().Enqueue(bot->GetGUIDLow(), stored->itemId);
}
