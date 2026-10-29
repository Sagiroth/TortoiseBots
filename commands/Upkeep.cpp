// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "BotCommandContext.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/BotManager.h"
#include "../runtime/DowntimeService.h"
#include "../runtime/BotActivityLease.h"
#include "../runtime/HireLifecycle.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/AltBotPolicy.h"
#include "../runtime/AltBotService.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/HireRaidPolicy.h"
#include "../runtime/HireSpecPolicy.h"
#include "../runtime/HireLevelPolicy.h"
#include "../runtime/HireTierRequestPolicy.h"
#include "BotCommandContext.h"
#include "../behavior/PlayerConvenience.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/AhMarketService.h"
#include "../host/ModuleVersion.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/RandomBotAccountRegistry.h"
#include "../runtime/RandomBotPoolReset.h"
#include "../runtime/RandomBotService.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotAI.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotDbStore.h"
#include "../ai/playerbot/ChatHelper.h"
#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/PullOpeningPolicy.h"
#include "../ai/playerbot/PullStartPolicy.h"
#include "../ai/playerbot/PullRefusalPolicy.h"
#include "../ai/playerbot/strategy/actions/SellAction.h"
#include "../ai/playerbot/policy/gear/SellRefusalPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/strategy/generic/PullStrategy.h"
#include "../ai/playerbot/strategy/values/RtiTargetValue.h"
#include "../ai/playerbot/strategy/values/AttackersValue.h"
#include "../ai/playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../ai/playerbot/strategy/values/PositionValue.h"
#include "../ai/playerbot/strategy/values/ItemUsageValue.h"
#include "../ai/playerbot/strategy/actions/EquipAction.h"
#include "../ai/playerbot/strategy/actions/UnequipAction.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotFactory.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/SurvivePolicy.h"
#include "runtime/OwnedBotQolPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"
#include "../ai/playerbot/AiFactory.h"
#include "../runtime/RemoteCommanderView.h"
#include "../runtime/RemoteCommanderTasks.h"
#include "../runtime/RemoteCommanderEnter.h"
#include "../runtime/CommanderTelemetry.h"
#include "../ai/playerbot/LootObjectStack.h"
#include "../ai/playerbot/strategy/Engine.h"
#include "../ai/playerbot/strategy/Value.h"
#include "../ai/playerbot/GuidPosition.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/strategy/shaman/ShamanTotemCommandPolicy.h"
#include "../ai/playerbot/strategy/shaman/ShamanTotemGroupPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldPacket.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/PathFinder.h"
#include "Movement/spline/MoveSplineInit.h"
#include "Maps/Cell.h"
#include "Maps/Map.h"
#include "Database/DBCStores.h"
#include "Database/SQLStorages.h"
#include "Maps/CellImpl.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Creature.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Spell.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Threat/ThreatManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <memory>
#include <cstring>
#include <sstream>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {

// Companion maintenance: loot toggling plus vendor sell/repair fan-out over
// the dynamic scope (selected bot or all owned party bots). The mature chat
// actions own vendor checks (proximity, gossip flags, gold); this layer only
// resolves scope, invokes them, persists strategy state, and reports.
bool HandleLoot(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    std::string mode = Trim(args ? args : "");
    for (char& c : mode)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!mode.empty() && mode != "on" && mode != "off")
    {
        handler->PSendSysMessage("Usage: .bot loot [on/off]");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope = ResolveDynamicScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No live owned party bots are controllable.");
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        bool enabled = mode.empty() ? !ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT) : mode == "on";
        ai->ChangeStrategy(enabled ? "+loot" : "-loot", BotState::BOT_STATE_NON_COMBAT);
        sPlayerbotDbStore.Save(ai);
        ++succeeded;
        if (context.selectedBot && scope.size() == 1)
            handler->PSendSysMessage("Bot %s: loot strategy %s.", bot->GetName(), enabled ? "enabled" : "disabled");
    }
    if (!context.selectedBot || scope.size() != 1)
        handler->PSendSysMessage("Loot strategy toggled for %u bot(s).", succeeded);
    return true;
}

bool HandleRepair(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope = ResolveDynamicScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No live owned party bots are controllable.");
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "repair", ai::Event("bot command", "repair", requester)))
            ++succeeded;
    }
    handler->PSendSysMessage("Commanded %u bot(s) to repair equipment.", succeeded);
    return true;
}

bool HandleSell(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope = ResolveDynamicScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No live owned party bots are controllable.");
        return true;
    }
    uint32 succeeded = 0;
    std::vector<std::string> refusals;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "sell", ai::Event("bot command", "", requester)))
            ++succeeded;
        else
            refusals.push_back(ai::SellAction::RefusalReason(ai));
    }
    if (succeeded)
        handler->PSendSysMessage("Commanded %u bot(s) to sell vendor trash.", succeeded);
    else
        handler->PSendSysMessage("%s", ai::FormatSellRefusal(refusals).c_str());
    return true;
}

// `.bot downtime [botName]`: ordered town errands for own alts. The alt gets
// the pool's errand kit for a bounded window (class trainer it can afford,
// junk vendor, repair) and returns to the master afterwards. Hired
// companions and pool bots are refused; the DowntimeService owns the window.
bool HandleDowntime(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }

    std::vector<Player*> scope;
    if (Trim(args ? args : "").empty())
    {
        BotCommandContext context = BuildContext(requester);
        scope = ResolveDynamicScope(context);
    }
    else
    {
        Player* bot = nullptr;
        BotRecord* record = nullptr;
        std::string name;
        if (!ResolveOwnedBot(handler, args, bot, record, name))
        {
            SendActionError(handler, "downtime", "no-bot",
                "Usage: .bot downtime [online bot name] (one of your own alts)");
            return true;
        }
        scope.push_back(bot);
    }

    if (scope.empty())
    {
        SendActionError(handler, "downtime", "no-alt",
            "No controllable bot matches. Select or name one of your own alt bots.");
        return true;
    }

    uint32 ordered = 0;
    std::string firstRefusal;
    std::string alreadyOut;
    for (Player* bot : scope)
    {
        std::string reply;
        DowntimeStartVerdict verdict = DowntimeService::Instance().Start(bot, requester, reply);
        if (verdict == DowntimeStartVerdict::Ok)
        {
            ++ordered;
            handler->PSendSysMessage("Bot %s %s", bot->GetName(), reply.c_str());
        }
        else if (verdict == DowntimeStartVerdict::AlreadyOut)
        {
            if (alreadyOut.empty())
                alreadyOut = std::string(bot->GetName()) + " " + reply;
        }
        else if (firstRefusal.empty())
            firstRefusal = std::string(bot->GetName()) + " " + reply;
    }

    if (!ordered)
    {
        // A repeat order changes nothing: report it as already-out, not as a
        // fresh success, so retry logic can tell the two apart.
        if (!alreadyOut.empty() && firstRefusal.empty())
        {
            SendActionError(handler, "downtime", "already-out", alreadyOut.c_str());
            return true;
        }
        SendActionError(handler, "downtime", "refused", firstRefusal.c_str());
        return true;
    }
    std::string const ackScope = scope.size() == 1 ? "bot:" + std::string(scope.front()->GetName()) : "party";
    SendActionAck(handler, "downtime", ackScope, ordered);
    return true;
}

// Pre-combat rest: order scoped bots to sit and consume food/drink until
// full, combat, or the master moves away. The mature food/drink actions own
// inventory, sit state, and regen checks; this layer only fans out and
// reports over the dynamic scope.
bool HandleRest(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope = ResolveDynamicScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No live owned party bots are controllable.");
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        bool rested = false;
        if (bot->GetPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA))
            rested = ExecuteQuietAction(ai, "drink", ai::Event("bot command", "drink", requester)) || rested;
        if (bot->GetHealth() < bot->GetMaxHealth())
            rested = ExecuteQuietAction(ai, "food", ai::Event("bot command", "food", requester)) || rested;
        if (rested)
            ++succeeded;
        else if (bot->GetHealth() >= bot->GetMaxHealth() && (bot->GetPowerType() != POWER_MANA || bot->GetPower(POWER_MANA) >= bot->GetMaxPower(POWER_MANA)))
            ++succeeded;
    }
    handler->PSendSysMessage("Commanded %u companion bot(s) to rest and restore health/mana.", succeeded);
    return true;
}

static std::vector<Player*> DeadBotsInScope(BotCommandContext const& context)
{
    std::vector<Player*> scope = ResolveDynamicScope(context);
    std::vector<Player*> dead;
    for (Player* bot : scope)
        if (bot && !bot->IsAlive())
            dead.push_back(bot);
    return dead;
}

bool HandleRelease(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope = DeadBotsInScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No dead companion bots found.");
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "release", ai::Event("bot command", "release", requester)))
            ++succeeded;
    }
    handler->PSendSysMessage("Released spirit for %u companion bot(s).", succeeded);
    return true;
}

bool HandleCorpseRun(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope = DeadBotsInScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No dead companion bots found.");
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "corpse run", ai::Event("bot command", "corpse run", requester)))
            ++succeeded;
    }
    handler->PSendSysMessage("Commanded %u companion bot(s) to run back to corpse.", succeeded);
    return true;
}

bool HandleLearn(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    std::vector<Player*> scope = ResolveDynamicScope(context);
    if (scope.empty())
    {
        handler->PSendSysMessage("No live owned party bots are controllable.");
        return true;
    }
    uint32 succeeded = 0;
    for (Player* bot : scope)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        if (ExecuteQuietAction(ai, "trainer", ai::Event("bot command", "learn", requester)))
            ++succeeded;
    }
    handler->PSendSysMessage("Commanded %u companion bot(s) to learn spells from nearby trainers.", succeeded);
    return true;
}

bool HandleTrade(ChatHandler* handler, char const* args)
{
    (void)args;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game.");
        return true;
    }
    BotCommandContext context = BuildContext(requester);
    Player* bot = context.selectedBot;
    if (!bot || !bot->IsAlive())
    {
        handler->PSendSysMessage("Target an alive companion bot to trade.");
        return true;
    }
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        handler->PSendSysMessage("Bot %s has no AI yet.", bot->GetName());
        return true;
    }
    if (ExecuteQuietAction(ai, "trade", ai::Event("bot command", requester->GetName(), requester)))
        handler->PSendSysMessage("Trade requested with %s.", bot->GetName());
    else
        handler->PSendSysMessage("Bot %s could not open trade.", bot->GetName());
    return true;
}

// `.bot corpse` guard: accepts bare "corpse" and "corpse run", rejects the rest.
bool HandleCorpseShortcut(ChatHandler* handler, char const* args)
{
    std::string rest = Trim(args ? args : "");
    for (char& c : rest)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (rest == "run" || rest.empty())
        return HandleCorpseRun(handler, "");
    handler->PSendSysMessage("Usage: .bot corpse run");
    return true;
}


} // namespace BotCommands
} // namespace TortoiseBots
