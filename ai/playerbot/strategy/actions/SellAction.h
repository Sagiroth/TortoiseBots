#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

namespace ai
{
    class SellAction : public ChatCommandAction
    {
    public:
        SellAction(PlayerbotAI* ai, std::string name = "sell") : ChatCommandAction(ai, name) {}
        virtual bool Execute(Event& event) override;

        bool Sell(Player* requester, FindItemVisitor* visitor);
        bool Sell(Player* requester, Item* item);

        // True when the NPC actually has vendor stock (npc_vendor rows or template rows).
        // Flagged-but-stockless NPCs (issue #404: entry 1650) would only print a core
        // "empty trading item list" error on gossip, so both sell and buy skip them here.
        static bool HasVendorStock(Creature* vendor);
        // What a sell order that sold nothing says why: `.bot sell`, TBM "Sell junk".
        static std::string RefusalReason(PlayerbotAI* botAI);

        // The bag items an autonomous vendor sale would actually sell: the sale's
        // own item filter (FindVendorItemsVisitor), a real sell price, and the
        // claimed-bot quality rule. The vendor trip need ("can sell", the batch
        // and spell-funding rule) and the nearby-service sell verb read this same
        // list, so a bot only walks to a vendor that will buy something.
        // includeAH adds AH-usage stock, as the sale does now and then on full bags.
        static std::list<Item*> VendorSaleStock(PlayerbotAI* ai, bool includeAH = false);

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "sell"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This command will make bots sell items to a nearby vendor.\n"
                "Usage: sell qualifier or itemlinks\n"
                "Example: sell vendor (sell based on item use)\n"
                "Example: sell gray\n"
                "Example: sell [itemlink] [itemlink]\n"
                "Example: sell equip \n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return { "nearest npcs", "item usage" }; }
#endif
    };
}