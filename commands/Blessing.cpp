// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "BotCommandContext.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/BotManager.h"
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
#include "../ai/playerbot/strategy/paladin/PaladinBlessingOrderPolicy.h"
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

static void SendBlessingLine(ChatHandler* handler, Player* bot, PlayerbotAI* ai)
{
    std::string line;
    for (int i = 0; i < ai::kBlessingOrderClassCount; ++i)
    {
        std::string stored = ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::BlessingOrderKey(i))->Get();
        if (ai::BlessingOrderSpell(stored).empty())
            stored = "auto";
        line += std::string(ai::BlessingOrderClassWord(i)) + "=" + stored + ",";
    }
    line += "known=";
    bool first = true;
    for (int i = 0; i < ai::kBlessingOrderBlessingCount; ++i)
    {
        std::string const word = ai::BlessingOrderWord(i);
        if (!ai->HasSpell(ai::BlessingOrderSpell(word)))
            continue;
        line += (first ? "" : "+") + word;
        first = false;
    }
    handler->PSendSysMessage("TBM:BLESSING|%s|%s", ProtocolSafe(bot->GetName()).c_str(), line.c_str());
}

// `.bot blessing <bot|party|raid> <class> <blessing|auto>` - per paladin, the
// blessing one class gets; saved per bot. `.bot blessing <scope> status`
// reports every paladin's assignment as TBM:BLESSING records.
bool HandleBlessing(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::istringstream input(Trim(args ? args : ""));
    std::string scope;
    std::string word2;
    std::string word3;
    input >> scope >> word2 >> word3;
    for (std::string* word : { &scope, &word2, &word3 })
        for (char& c : *word)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    char const* const usage =
        "Usage: .bot blessing <bot|party|raid> <warrior|paladin|hunter|rogue|priest|shaman|mage|warlock|druid> "
        "<might|wisdom|kings|salvation|sanctuary|light|auto> | status";
    bool const status = word2 == "status";
    int const classIndex = ai::BlessingOrderClassIndex(word2);
    if (scope.empty() || (!status && (classIndex < 0 || !ai::IsBlessingOrderWord(word3))))
    {
        SendActionError(handler, "blessing", "invalid", usage);
        return true;
    }
    char const* const intent = status ? "blessing status" : "blessing";

    bool const scopeIsParty = scope == "party" || scope == "raid";
    std::vector<Player*> bots;
    if (scopeIsParty)
    {
        if (!requester)
            return true;
        BotCommandContext context = BuildContext(requester);
        bots = ResolveDynamicScope(context);
        if (bots.empty())
        {
            SendActionError(handler, intent, "no-bot", "No live owned party bots are controllable.");
            return true;
        }
    }
    else
    {
        Player* bot = nullptr;
        BotRecord* record = nullptr;
        std::string name;
        if (!requester || !ResolveOwnedBot(handler, scope.c_str(), bot, record, name))
        {
            SendActionError(handler, intent, "no-bot", "Unknown or uncontrollable bot name.");
            return true;
        }
        bots.push_back(bot);
    }
    std::string const ackScope = scopeIsParty ? "party" : std::string("bot:") + bots.front()->GetName();

    uint32 paladins = 0;
    std::string const spell = ai::BlessingOrderSpell(word3);
    for (Player* bot : bots)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai || bot->GetClass() != CLASS_PALADIN)
            continue;
        if (!status)
        {
            if (!spell.empty() && !ai->HasSpell(spell))
            {
                std::string msg = std::string(bot->GetName()) + " has not trained " + spell + ".";
                SendActionError(handler, intent, "unknown-spell", msg.c_str());
                continue;
            }
            ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::BlessingOrderKey(classIndex))->Set(word3);
            sPlayerbotDbStore.Save(ai);
        }
        SendBlessingLine(handler, bot, ai);
        ++paladins;
    }
    if (status)
    {
        SendActionAck(handler, intent, ackScope, paladins, "-");
        return true;
    }
    if (!paladins)
    {
        SendActionError(handler, intent, "no-paladin", "No paladin bots in scope could take that order.");
        return true;
    }
    SendActionAck(handler, intent, ackScope, paladins, word2 + "=" + word3);
    return true;
}

} // namespace BotCommands
} // namespace TortoiseBots
