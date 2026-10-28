#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    enum class GuildOrderType : uint8
    {
        None = 0,
        Farm,
        Craft,
        AuctionHouse
    };

    struct GuildOrder
    {
        GuildOrderType type = GuildOrderType::None;
        std::string target;
        uint32 itemId = 0; // `.bot order` item entry; 0 for legacy note/share orders
        uint32 amount = 0; // Used for Craft and Farm orders

        bool IsValid() const { return type != GuildOrderType::None && !target.empty(); }
        bool IsTravelOrder() const { return type == GuildOrderType::Farm || type == GuildOrderType::AuctionHouse; }
        bool IsCraftOrder() const { return type == GuildOrderType::Craft; }

        std::string GetTypeName() const
        {
            switch (type)
            {
            case GuildOrderType::Farm:         return "Farm";
            case GuildOrderType::Craft:        return "Craft";
            case GuildOrderType::AuctionHouse: return "AuctionHouse";
            default:                           return "None";
            }
        }
    };

    class GuildOrderValue : public CalculatedValue<GuildOrder>
    {
    public:
        GuildOrderValue(PlayerbotAI* ai) : CalculatedValue<GuildOrder>(ai, "guild order", 60) {}

        GuildOrder Calculate() override;

        static uint32 FindItemByName(const std::string& name);
    private:
        static std::string TrimWhitespace(const std::string& str);
        static bool ParseOrderPrefix(const std::string& note, const std::string& prefix, std::string& outBody);
    };

    class HasGuildTravelOrderValue : public BoolCalculatedValue
    {
    public:
        HasGuildTravelOrderValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "has guild travel order", 10) {}

        bool Calculate() override
        {
            GuildOrder order = AI_VALUE(GuildOrder, "guild order");
            if (!order.IsTravelOrder())
                return false;

            // A `.bot order` waits while the bot is grouped with a player.
            if (order.itemId && ai->HasRealPlayerMaster())
                return false;

            // If a Farm order has an amount, check inventory count.
            if (order.type == GuildOrderType::Farm && order.amount > 0)
            {
                uint32 itemId = order.itemId ? order.itemId : GuildOrderValue::FindItemByName(order.target);
                if (itemId)
                {
                    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId);
                    if (currentCount >= order.amount)
                        return false;
                }
            }

            return true;
        }
    };

    class HasGuildCraftOrderValue : public BoolCalculatedValue
    {
    public:
        HasGuildCraftOrderValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "has guild craft order", 10) {}

        bool Calculate() override
        {
            GuildOrder order = AI_VALUE(GuildOrder, "guild order");
            if (!order.IsCraftOrder())
                return false;
            // A `.bot order` waits while the bot is grouped with a player.
            return !order.itemId || !ai->HasRealPlayerMaster();
        }
    };

    class PetitionSignsValue : public SingleCalculatedValue<uint8>
    {
    public:
        PetitionSignsValue(PlayerbotAI* ai) : SingleCalculatedValue<uint8>(ai, "petition signs") {}

        uint8 Calculate() override;
    };

    class CanHandInPetitionValue : public CalculatedValue<bool>
    {
    public:
        CanHandInPetitionValue(PlayerbotAI* ai) : CalculatedValue<bool>(ai, "can hand in guild petition") {}

        bool Calculate() override { return !bot->GetGuildId() && AI_VALUE2(uint32, "item count", chat->formatQItem(5863)) && AI_VALUE(uint8, "petition signs") >= sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS); };
    };

    class CanBuyTabard : public CalculatedValue<bool>
    {
    public:
        CanBuyTabard(PlayerbotAI* ai) : CalculatedValue<bool>(ai, "can buy tabard") {}

        bool Calculate() override;
    };
}