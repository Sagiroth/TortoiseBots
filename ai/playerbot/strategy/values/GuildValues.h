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
        uint32 itemId = 0; // `.bot order` item entry
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
    };

    // True while the stored order still needs farm/craft work: not waiting,
    // not delivering, and delivered plus bags plus mailbox below the amount.
    bool OrderStillNeedsWork(Player* bot, GuildOrder const& order);

    // First missing farmable reagent for a Working craft order: the same
    // pick the "guild order farm item" value makes, shared so order mode
    // and travel never farm different items.
    uint32 GuildOrderCraftReagentFor(PlayerbotAI* ai, Player* bot, uint32 itemId, uint32 amount,
        uint32 delivered);

    // Expected kills for the hardest farmable reagent still missing from the
    // bags for a craft order, or -1 when every reagent comes from the bags
    // or from a vendor. The caller turns it into a difficulty estimate.
    double GuildOrderCraftHardestReagentKills(PlayerbotAI* ai, Player* bot, uint32 itemId, uint32 amount,
        uint32 delivered);

    class HasGuildTravelOrderValue : public BoolCalculatedValue
    {
    public:
        HasGuildTravelOrderValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "has guild travel order", 10) {}

        bool Calculate() override;
    };

    // The item the bot actually farms for its guild order: the order item for
    // farm orders, the first missing reagent the planner can source for craft
    // orders, 0 when none (no order, reagents complete, nothing sourcable).
    class GuildOrderFarmItemValue : public CalculatedValue<uint32>
    {
    public:
        GuildOrderFarmItemValue(PlayerbotAI* ai) : CalculatedValue<uint32>(ai, "guild order farm item", 10) {}

        uint32 Calculate() override;
    };

    // First missing craft-order reagent sold unlimited by a vendor, 0 when
    // none. This is the order-driven vendor trip: anything it returns is
    // bought, never farmed.
    class GuildOrderVendorReagentValue : public CalculatedValue<uint32>
    {
    public:
        GuildOrderVendorReagentValue(PlayerbotAI* ai) : CalculatedValue<uint32>(ai, "guild order vendor reagent", 10) {}

        uint32 Calculate() override;
    };

    // True when the recipe needs a spell focus (anvil, forge, fire),
    // reagents for at least one cast are ready, and no matching focus
    // object is in sight.
    class GuildOrderNeedsFocusValue : public BoolCalculatedValue
    {
    public:
        GuildOrderNeedsFocusValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "guild order needs focus", 10) {}

        bool Calculate() override;
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
            if (ai->HasRealPlayerMaster())
                return false;
            return OrderStillNeedsWork(bot, order);
        }
    };

    // True while the stored order is Delivering and the bot still carries
    // order items: it needs a guild banker. Any order type lands here.
    bool OrderNeedsDelivery(Player* bot);

    class HasGuildOrderDeliveryValue : public BoolCalculatedValue
    {
    public:
        HasGuildOrderDeliveryValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "has guild order delivery", 10) {}

        bool Calculate() override;
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