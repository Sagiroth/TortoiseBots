#include "BotPlayerAdapter.h"

#include "../runtime/BotManager.h"
#include "../runtime/RandomBotService.h"
#include "WorldSession.h"

namespace TortoiseBots {

BotPlayerAdapter::BotPlayerAdapter()
    : PlayerScript("tortoisebots_players", {
        PLAYERHOOK_ON_LOGIN,
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
