// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "BotCommandContext.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/BotManager.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/PlayerbotAIStorage.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotAI.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/MagePortalPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"

#include <sstream>

namespace TortoiseBots {
namespace BotCommands {

// Conjure/Create spell ids by spellbook scan: any rank counts. The policy
// owns the names; the ids come from the bot's own book (highest/only rank
// wins for the cast, any rank counts for the query).
static bool KnowsSpellLine(Player* bot, PlayerbotAI* ai, char const* lineName)
{
    if (!bot || !ai)
        return false;
    for (PlayerSpellMap::const_iterator it = bot->GetSpellMap().begin(); it != bot->GetSpellMap().end(); ++it)
    {
        uint32_t spellId = it->first;
        if (it->second.state == PLAYERSPELL_REMOVED || it->second.disabled)
            continue;
        const SpellEntry* info = sServerFacade.LookupSpellInfo(spellId);
        if (!info)
            continue;
        std::string name = info->SpellName[0];
        for (char& c : name)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        std::string line = lineName;
        for (char& c : line)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (name.compare(0, line.size(), line) == 0)
            return true;
    }
    return false;
}

static std::vector<uint32_t> KnownPortalIds(Player* bot)
{
    std::vector<uint32_t> known;
    size_t count = 0;
    ai::MagePortalSpell const* table = ai::MagePortalTable(&count);
    for (size_t i = 0; i < count; ++i)
        if (bot->HasSpell(table[i].spellId))
            known.push_back(table[i].spellId);
    return known;
}

// `.bot service <bot>`: one TBM:SERVICE line for the addon's bot panel.
// food/water/healthstone = 1 when the bot knows any rank of the line;
// portals = "spellId:City" pairs or "-" when none.
static bool HandleServiceQuery(ChatHandler* handler, Player* bot, std::string const& intent)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        SendActionError(handler, intent, "no-bot", "Unknown or uncontrollable bot name.");
        return true;
    }
    uint32_t food = KnowsSpellLine(bot, ai, ai::ConjureFoodSpellName()) ? 1 : 0;
    uint32_t water = KnowsSpellLine(bot, ai, ai::ConjureWaterSpellName()) ? 1 : 0;
    uint32_t stone = KnowsSpellLine(bot, ai, ai::CreateHealthstoneSpellName()) ? 1 : 0;
    std::string portals = ai::FormatServicePortals(KnownPortalIds(bot));
    handler->PSendSysMessage("TBM:SERVICE|%s|%u|%u|%u|%s", bot->GetName(), food, water, stone, portals.c_str());
    return true;
}

// `.bot service <bot> portal <spellId>`: the addon portal button. Same
// refusal order as the whisper (the policy decides), answered with the
// action ack/err contract.
static bool HandleServicePortal(ChatHandler* handler, Player* requester, Player* bot, uint32_t spellId,
    std::string const& intent, std::string const& scope)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        SendActionError(handler, intent, "no-bot", "Unknown or uncontrollable bot name.");
        return true;
    }
    size_t count = 0;
    ai::MagePortalSpell const* table = ai::MagePortalTable(&count);
    bool listed = false;
    for (size_t i = 0; i < count; ++i)
        if (table[i].spellId == spellId)
            listed = true;
    ai::PortalCastFacts facts;
    facts.alive = bot->IsAlive();
    facts.inCombat = bot->IsInCombat();
    facts.casting = bot->IsNonMeleeSpellCasted(false);
    facts.isMage = bot->GetClass() == CLASS_MAGE;
    ai::PortalCastDecision decision = !listed
        ? ai::PortalCastDecision{ai::PortalRefusal::UnknownCity, false, false}
        : ai::DecidePortalCast(facts, spellId, bot->HasSpell(spellId),
            bot->HasItemCount(ai::PortalRuneItemId(), 1), bot->IsMoving());
    if (decision.refusal != ai::PortalRefusal::None)
    {
        SendActionError(handler, intent, ai::PortalRefusalCode(decision.refusal),
            ai::PortalRefusalText(decision.refusal));
        return true;
    }
    if (decision.needsRune)
        bot->StoreNewItemInInventorySlot(ai::PortalRuneItemId(), 1);
    if (decision.needsStop)
        ai->StopMoving();
    std::ostringstream cast;
    cast << spellId;
    if (!ai->DoSpecificAction("cast", ai::Event("service portal", cast.str(), requester), true))
    {
        SendActionError(handler, intent, "busy", "The portal fizzled; try again.");
        return true;
    }
    SendActionAck(handler, intent, scope, 1, bot->GetName());
    return true;
}

bool HandleService(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::istringstream input(Trim(args ? args : ""));
    std::string botName;
    input >> botName;
    std::string intent = "service portal";
    if (botName.empty())
    {
        SendActionError(handler, "service", "invalid", "Usage: .bot service <bot> [portal <spellId>]");
        return true;
    }

    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, botName.c_str(), bot, record, name))
    {
        SendActionError(handler, intent, "no-bot", "Unknown or uncontrollable bot name.");
        return true;
    }
    std::string const scope = "bot:" + std::string(bot->GetName());

    std::string rest;
    std::getline(input, rest);
    rest = Trim(rest);
    if (rest.empty())
        return HandleServiceQuery(handler, bot, "service");

    std::istringstream words(rest);
    std::string verb;
    words >> verb;
    for (char& c : verb)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (verb != "portal")
    {
        SendActionError(handler, intent, "invalid", "Usage: .bot service <bot> [portal <spellId>]");
        return true;
    }
    uint32_t spellId = 0;
    if (!(words >> spellId) || !spellId)
    {
        SendActionError(handler, intent, "unknown", "Unknown portal city.");
        return true;
    }
    return HandleServicePortal(handler, requester, bot, spellId, intent, scope);
}

} // namespace BotCommands
} // namespace TortoiseBots
