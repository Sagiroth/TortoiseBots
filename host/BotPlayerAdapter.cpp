#include "BotPlayerAdapter.h"

#include "../behavior/PlayerConvenience.h"
#include "../runtime/BotManager.h"
#include "../runtime/HireLifecycle.h"
#include "../runtime/RandomBotService.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../ai/playerbot/AiFactory.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "WorldSession.h"
#include "Chat.h"
#include "../runtime/ObservabilityEmitter.h"
#include "ModuleLog.h"
#include "ModuleVersion.h"

namespace TortoiseBots {

BotPlayerAdapter::BotPlayerAdapter()
    : PlayerScript("tortoisebots_players", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_BEFORE_LOGOUT,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_IS_MANAGED_BOT,
        PLAYERHOOK_GET_BOT_ROLES })
{
}

bool BotPlayerAdapter::IsManagedBot(Player* who)
{
    if (!who)
        return false;
    return BotManager::Instance().IsBot(who->GetObjectGuid());
}

uint8 BotPlayerAdapter::GetBotRoles(Player* who)
{
    if (!who)
        return 0;
    if (!BotManager::Instance().IsBot(who->GetObjectGuid()))
        return 0;
    // TBM role/spec buttons send mature spec changes (`.bot command <Name>
    // <strat>` -> HandleMatureCommand -> HandleCommand whisper queue ->
    // reaction "co" trigger -> ChangeCombatStrategyAction on BOT_STATE_COMBAT
    // only). So the click lands in the combat engine alone; the non-combat
    // engine keeps install-time strategies (e.g. `protection`/`tank assist`
    // from AddDefaultNonCombatStrategies) no matter what the player clicks.
    // The rolecheck therefore reads combat-engine NAMED specs, never ambient
    // helpers: `tank assist`/`dps assist` carry TANK/DPS bits but are added
    // next to every spec and are never removed by `-protection` etc, so a
    // bit test over every engine answers TANK forever after one tank click.
    // Forced roles (storage override or AI) still win outright; without them
    // and without a named combat spec the talent/spec auto-detect applies.
    if (PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(who))
        if (botAi->HasActivePlayerMaster() &&
            PlayerbotAIStorage::Instance().GetPlayerForcedRole(who->GetObjectGuid()) == 0 &&
            botAi->GetForcedRole() == 0)
        {
            // TANK: installed only by a tank click (`+protection`,
            // `+tank feral`) or install-time prot/feral defaults.
            if (botAi->HasStrategy("protection", BotState::BOT_STATE_COMBAT) ||
                botAi->HasStrategy("tank feral", BotState::BOT_STATE_COMBAT) ||
                botAi->HasStrategy("bear", BotState::BOT_STATE_COMBAT) ||
                botAi->HasStrategy("tank", BotState::BOT_STATE_COMBAT))
                return static_cast<uint8>(ai::BOT_ROLE_TANK);
            // HEALER: holy/discipline/restoration (plus legacy heal/resto).
            if (botAi->HasStrategy("holy", BotState::BOT_STATE_COMBAT) ||
                botAi->HasStrategy("discipline", BotState::BOT_STATE_COMBAT) ||
                botAi->HasStrategy("restoration", BotState::BOT_STATE_COMBAT) ||
                botAi->HasStrategy("heal", BotState::BOT_STATE_COMBAT) ||
                botAi->HasStrategy("resto", BotState::BOT_STATE_COMBAT))
                return static_cast<uint8>(ai::BOT_ROLE_HEALER);
            // DPS: named combat specs only. Ambient `dps assist` is ignored on
            // purpose: it rides along with every spec including healers, and a
            // bare assist set (no spec) falls through to auto-detect.
            static char const* const dpsSpecs[] = { "arms", "fury",
                "retribution", "beast mastery", "marksmanship", "survival",
                "combat", "assassination", "subtlety", "shadow",
                "elemental", "enhancement", "frost", "fire", "arcane",
                "affliction", "demonology", "destruction", "dps feral",
                "balance", "cat" };
            for (char const* spec : dpsSpecs)
                if (botAi->HasStrategy(spec, BotState::BOT_STATE_COMBAT))
                    return static_cast<uint8>(ai::BOT_ROLE_DPS);
        }
    return static_cast<uint8>(AiFactory::GetPlayerRoles(who));
}

void BotPlayerAdapter::OnLogin(Player* player)
{
    if (player && player->GetSession() && player->GetSession()->HasNetworkTransport())
    {
        RandomBotService::Instance().OnHumanLogin();
        HireLifecycle::Instance().OnMasterLogin(player);
        ChatHandler(player).PSendSysMessage("|cff00ff00[TortoiseBots]|r %s", AttributionLine().c_str());
        if (player->GetSession()->GetSecurity() >= SEC_DEVELOPER && sObservabilityEmitter.IsEnabled())
        {
            ChatHandler(player).PSendSysMessage("|cff00ff00[TortoiseBots]|r Observability dashboard active: http://localhost:8095/dashboard");
        }
    }
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
            TB_LOG_DETAIL("TortoiseBots: returning bot %s after master %s left a dungeon",
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
    // Start the hire grace clock while the master object is still valid.
    // OnLogout fires after the session tears down; OnPlayerBeforeLogout only
    // detaches AI pointers, so ordering here is safe.
    if (player && player->GetSession() && player->GetSession()->HasNetworkTransport())
        HireLifecycle::Instance().OnMasterLogout(player);
    BotManager::Instance().OnPlayerBeforeLogout(player);
}

void BotPlayerAdapter::OnLogout(Player* player)
{
    BotManager::Instance().OnPlayerLogout(player);
    if (player && player->GetSession() && player->GetSession()->HasNetworkTransport())
        RandomBotService::Instance().OnHumanLogout();
}

BotUnitAdapter::BotUnitAdapter()
    : UnitScript("tortoisebots_units", { UNITHOOK_ON_UNIT_DEATH })
{
}

void BotUnitAdapter::OnUnitDeath(Unit* unit, Unit* killer)
{
    if (!unit || !unit->IsPlayer())
        return;

    Player* player = unit->ToPlayer();
    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    if (!ai)
        return;

    ai->SetLastKiller(killer);
}

} // namespace TortoiseBots

