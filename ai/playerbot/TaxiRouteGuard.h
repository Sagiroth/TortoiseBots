#pragma once

#include "playerbot/BotInstantFlightPolicy.h"
#include "playerbot/TaxiPathPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "ObjectMgr.h"

#include <vector>

namespace ai
{
    // True when every leg of a bot's flight (consecutive taxi nodes) uses a
    // path the bot may fly (TaxiPathPolicy.h). Checked before each
    // Player::ActivateTaxiPathTo a bot starts.
    inline bool BotTaxiRouteAllowed(std::vector<uint32> const& nodes)
    {
        for (size_t i = 1; i < nodes.size(); ++i)
        {
            uint32 path = 0;
            uint32 cost = 0;
            sObjectMgr.GetTaxiPath(nodes[i - 1], nodes[i], path, cost);
            if (!TaxiPathAllowed(path, sPlayerbotAIConfig.allowRepairedTaxiPaths))
                return false;
        }
        return true;
    }

    // Test shortcut (AiPlayerbot.BotInstantFlight): finish a started bot
    // flight at once on the destination flight-master node. Destinations go
    // first so the expiry below cannot restart a leg, then the flight state
    // is cleared the way a summon interrupts one (SummonAction::Teleport)
    // and the bot lands where an arrival would put it
    // (Player::TaxiStepFinished). True when the bot landed; false leaves the
    // flight flying as normal.
    inline bool BotInstantFlightFinish(Player* bot, std::vector<uint32> const& nodes)
    {
        uint32 destNode = BotInstantFlightDestNode(sPlayerbotAIConfig.botInstantFlight, bot->IsTaxiFlying(), nodes);
        if (!destNode)
            return false;
        TaxiNodesEntry const* dest = sObjectMgr.GetTaxiNodeEntry(destNode);
        if (!dest)
            return false;
        bot->GetTaxi().ClearTaxiDestinations();
        bot->CleanupFlagsOnTaxiPathFinished();
        bot->GetMotionMaster()->MovementExpired();
        return bot->TeleportTo(dest->map_id, dest->x, dest->y, dest->z, bot->GetOrientation());
    }
}
