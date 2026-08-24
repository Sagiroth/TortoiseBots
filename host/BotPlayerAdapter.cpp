#include "BotPlayerAdapter.h"

#include "../runtime/BotManager.h"
#include "../runtime/RandomBotService.h"
#include "WorldSession.h"

namespace TortoiseBots {

BotPlayerAdapter::BotPlayerAdapter()
    : PlayerScript("tortoisebots_players", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_BEFORE_LOGOUT,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_RELEASE_TO_CLIENT,
        PLAYERHOOK_IS_AI_CONTROLLED,
        PLAYERHOOK_IS_MACHINE_DRIVEN,
        PLAYERHOOK_HAS_AI_FOLLOWERS })
{
}

void BotPlayerAdapter::OnLogin(Player* player)
{
    if (player && player->GetSession() && !player->GetSession()->IsHeadless())
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
    if (player && player->GetSession() && !player->GetSession()->IsHeadless())
        RandomBotService::Instance().OnHumanLogout();
}

void BotPlayerAdapter::OnReleaseToClient(Player* player)
{
    BotManager::Instance().ReleaseToClient(player);
}

bool BotPlayerAdapter::IsAIControlled(Player const* player)
{
    return player && BotManager::Instance().IsBot(player->GetObjectGuid());
}

bool BotPlayerAdapter::IsMachineDriven(Player const* player)
{
    return player && player->GetSession() && player->GetSession()->IsHeadless() &&
        BotManager::Instance().IsBot(player->GetObjectGuid());
}

bool BotPlayerAdapter::HasAIFollowers(Player const* player)
{
    return player && !BotManager::Instance().GetBotsForMaster(player->GetObjectGuid()).empty();
}

} // namespace TortoiseBots
