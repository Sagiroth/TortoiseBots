#include "BotPlayerAdapter.h"

#include "../behavior/PlayerConvenience.h"
#include "../runtime/BotManager.h"
#include "../runtime/RandomBotService.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "WorldSession.h"

namespace TortoiseBots {

BotPlayerAdapter::BotPlayerAdapter()
    : PlayerScript("tortoisebots_players", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_BEFORE_LOGOUT,
        PLAYERHOOK_ON_LOGOUT })
{
}

void BotPlayerAdapter::OnLogin(Player* player)
{
    if (player && player->GetSession() && player->GetSession()->HasNetworkTransport())
        RandomBotService::Instance().OnHumanLogin();
    BotManager::Instance().OnPlayerLogin(player);
}

void BotPlayerAdapter::OnMapChanged(Player* player)
{
    if (!player || !player->GetSession() || !player->GetSession()->HasNetworkTransport() ||
        !player->IsInWorld() || !player->GetMap() || player->GetMap()->IsDungeon())
    {
        return;
    }

    // This is a player-convenience transition, not lifecycle work. BotManager
    // supplies a live owned-bot snapshot; PlayerConvenience owns the queued
    // summon and its cancellation/completion state.
    for (Player* bot : BotManager::Instance().GetBotsForMaster(player->GetObjectGuid()))
    {
        if (!bot || !bot->GetMap() || !bot->GetMap()->IsDungeon())
            continue;

        if (PlayerConvenience::Instance().RequestSummon(player, bot))
        {
            sLog.outString("TortoiseBots: returning bot %s after master %s left a dungeon",
                bot->GetName(), player->GetName());
        }
        else
        {
            sLog.outError("TortoiseBots: bot %s remained in a dungeon after master %s left; "
                "the native summon preconditions rejected its return",
                bot->GetName(), player->GetName());
        }
    }
}

void BotPlayerAdapter::OnBeforeLogout(Player* player)
{
    BotManager::Instance().OnPlayerBeforeLogout(player);
}

void BotPlayerAdapter::OnLogout(Player* player)
{
    BotManager::Instance().OnPlayerLogout(player);
    if (player && player->GetSession() && player->GetSession()->HasNetworkTransport())
        RandomBotService::Instance().OnHumanLogout();
}



} // namespace TortoiseBots
