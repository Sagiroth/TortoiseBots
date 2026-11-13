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
#include "../ai/playerbot/strategy/generic/ClassChoicePolicy.h"
namespace TortoiseBots {
namespace BotCommands {

// One TBM:CHOICE record: the stored word for the bot's kind, then every
// value the bot has trained (may be empty).
static void SendChoiceLine(ChatHandler* handler, Player* bot, PlayerbotAI* ai, int kind)
{
    std::string stored = ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::ChoiceOrderKey(kind))->Get();
    if (stored.empty())
        stored = "auto";
    std::string known;
    for (int i = 0; i < ai::ChoiceWordCount(kind); ++i)
    {
        std::string const word = ai::ChoiceWord(kind, i);
        if (!ai->HasSpell(ai::ChoiceSpell(kind, word)))
            continue;
        known += (known.empty() ? "" : "+") + word;
    }
    handler->PSendSysMessage("TBM:CHOICE|%s|%s=%s,known=%s", ProtocolSafe(bot->GetName()).c_str(),
        ai::ChoiceKindWord(kind), stored.c_str(), known.c_str());
}

// `.bot choice <bot|party|raid> <kind> <value|auto>` - per bot, the ordered
// aura/aspect/curse. `.bot choice <scope> status` reports every bot that has
// the kind as TBM:CHOICE records.
bool HandleChoice(ChatHandler* handler, char const* args)
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

    char const* const usage = "Usage: .bot choice <bot|party|raid> <aura|aspect|curse> <value|auto> | status";
    bool const status = word2 == "status";
    int const kind = ai::ChoiceKindIndex(word2);
    if (scope.empty() || (!status && (kind < 0 || !ai::IsChoiceWord(kind, word3))))
    {
        SendActionError(handler, "choice", "invalid", usage);
        return true;
    }
    char const* const intent = status ? "choice status" : "choice";

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

    uint32 matched = 0;
    bool refused = false;
    std::string const spell = status ? std::string() : ai::ChoiceSpell(kind, word3);
    for (Player* bot : bots)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        int const rowKind = status ? ai::ChoiceKindForClass(bot->GetClass()) : kind;
        if (!ai || rowKind < 0 || bot->GetClass() != ai::ChoiceKindClassId(rowKind))
            continue;
        if (!status)
        {
            if (!spell.empty() && !ai->HasSpell(spell))
            {
                std::string msg = std::string(bot->GetName()) + " has not trained " + spell + ".";
                SendActionError(handler, intent, "unknown-spell", msg.c_str());
                refused = true;
                continue;
            }
            std::string const previous = ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::ChoiceOrderKey(kind))->Get();
            ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::ChoiceOrderKey(kind))->Set(word3);
            // The automatic aura row only fires with no aura up, so the
            // ordered one is dropped for it to take over.
            if (kind == 0 && word3 == "auto" && !previous.empty() && previous != "auto")
                ai->RemoveAura(ai::ChoiceSpell(kind, previous));
            sPlayerbotDbStore.Save(ai);
        }
        SendChoiceLine(handler, bot, ai, rowKind);
        ++matched;
    }
    if (status)
    {
        SendActionAck(handler, intent, ackScope, matched, "-");
        return true;
    }
    if (!matched)
    {
        if (!refused)
            SendActionError(handler, intent, "no-bot", "No bots of that kind's class in scope.");
        return true;
    }
    SendActionAck(handler, intent, ackScope, matched, word2 + "=" + word3);
    return true;
}

} // namespace BotCommands
} // namespace TortoiseBots
