// pi-lens-ignore-file: all
// `.bot order search <craft|enchant> <text>` without a bot: every crafted
// item (or catalogue enchant) whose name matches the text, with all online
// guild bots that know an orderable recipe (or the enchant) for it.
// One TBM:ORDER_WHO row per crafted item or enchant; the bots field carries
// name:skill:have:need:busy per bot, sorted by skill high to low. At most
// 30 rows. Craft sorts by name rank, then name; enchant by required skill,
// then name.
// pi-lens-ignore: clang:pp_file_not_found
#include "BotCommandHandlers.h"
#include "CommandShared.h"

#include "../ai/playerbot/GuildOrderEnchants.h"
#include "../runtime/BotManager.h"
#include "../runtime/GuildOrderContract.h"
#include "../runtime/GuildOrderSearchPolicy.h"
#include "../runtime/GuildOrderStore.h"
#include "../runtime/GuildOrderWhoSearchPolicy.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DBCStores.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Guild/GuildMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {
namespace {

using TortoiseBots::GuildOrders::SearchNameMatches;
using TortoiseBots::GuildOrders::SearchNameRank;
using TortoiseBots::GuildOrders::SplitQueryWords;
using TortoiseBots::GuildOrders::NormalizeQueryWords;
constexpr size_t kMinSearchChars = 3;
constexpr size_t kMaxWhoResults = 30;

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

bool BotBusy(uint32_t guidLow)
{
    std::optional<GuildOrders::OrderRecord> existing = GuildOrders::Find(guidLow);
    return existing.has_value() && !existing->cancelled &&
        (!GuildOrders::IsFinished(existing->state) || existing->state == GuildOrders::OrderState::Stocked);
}

// `.bot order search enchant <text>` without a bot: every catalogue
// enchant whose name or slot matches, with all online guild bots that know
// it. Sorted by required skill ascending, then name; at most 30 rows.
bool HandleOrderWhoEnchantSearch(ChatHandler* handler, Player* requester, std::string const& query)
{
    uint32_t guildId = requester->GetGuildId();
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!guild || !guild->HasRankRight(requester->GetRank(), GR_RIGHT_EOFFNOTE))
    {
        SendActionError(handler, "order", "forbidden", "Your guild rank may not give orders.");
        return true;
    }
    std::vector<std::string> aliased;
    for (std::string const& word : SplitQueryWords(ToLower(query)))
        aliased.push_back(GuildOrders::EnchantSlotAlias(word));
    struct Match
    {
        uint32_t spellId = 0;
        std::string name;
        GuildOrders::EnchantSlot slot = GuildOrders::EnchantSlot::Unknown;
        uint32_t reqSkill = 0;
        std::vector<GuildOrders::WhoBot> bots;
    };
    std::unordered_map<uint32_t, Match> matches;
    for (Player* bot : BotManager::Instance().GetAllBots())
    {
        if (!bot || !bot->IsInWorld() || bot->GetGuildId() != guildId ||
            GuildOrders::IsOthersAlt(bot->GetGUIDLow(), requester->GetSession()->GetAccountId()))
            continue;
        std::vector<EnchantSnapshot> spells = OrderableEnchantSpells(bot, aliased);
        if (spells.empty())
            continue;
        bool busy = BotBusy(bot->GetGUIDLow());
        for (EnchantSnapshot const& snapshot : spells)
        {
            TortoiseBots::GuildOrders::EnchantEntry entry;
            for (TortoiseBots::GuildOrders::EnchantEntry const& catalogue : ai::GuildOrderEnchants())
                if (catalogue.spellId == snapshot.spellId)
                {
                    entry = catalogue;
                    break;
                }
            if (!entry.spellId)
                continue;
            Match& match = matches[entry.spellId];
            if (match.spellId == 0)
            {
                match.spellId = entry.spellId;
                match.name = entry.name;
                match.slot = entry.slot;
                match.reqSkill = entry.reqSkill;
            }
            std::vector<GuildOrders::WhoReagent> reagents;
            for (CraftRecipeReagent const& reagent : snapshot.reagents)
                reagents.push_back({ reagent.need, reagent.have });
            uint32_t have = 0, need = 0;
            GuildOrders::CountWhoReagents(reagents, have, need);
            GuildOrders::WhoBot who;
            who.name = bot->GetName();
            who.skill = snapshot.skillValue;
            who.have = have;
            who.need = need;
            who.busy = busy;
            match.bots.push_back(std::move(who));
        }
    }
    std::vector<Match const*> scored;
    scored.reserve(matches.size());
    for (auto const& found : matches)
        scored.push_back(&found.second);
    std::sort(scored.begin(), scored.end(), [](Match const* a, Match const* b)
    {
        if (a->reqSkill != b->reqSkill)
            return a->reqSkill < b->reqSkill;
        return a->name < b->name;
    });
    if (scored.size() > kMaxWhoResults)
        scored.resize(kMaxWhoResults);
    handler->PSendSysMessage("%s", GuildOrders::FormatWhoBegin("enchant", uint32_t(scored.size())).c_str());
    for (Match const* match : scored)
    {
        std::vector<GuildOrders::WhoBot> bots = match->bots;
        GuildOrders::SortWhoBots(bots);
        handler->PSendSysMessage("%s",
            GuildOrders::FormatWhoRow("enchant", match->spellId, ProtocolSafe(match->name), "-",
                GuildOrders::EnchantSlotToken(match->slot), match->reqSkill,
                GuildOrders::FormatWhoBots(bots)).c_str());
    }
    handler->PSendSysMessage("%s", GuildOrders::FormatWhoEnd().c_str());
    return true;
}

} // namespace

bool HandleOrderWhoSearch(ChatHandler* handler, Player* requester, std::string const& kind,
                          std::string const& text)
{
    if (!handler || !requester)
        return false;
    if (!requester->GetGuildId())
    {
        SendActionError(handler, "order", "not-guild", "You are not in a guild.");
        return true;
    }
    if (kind != "craft" && kind != "enchant")
    {
        SendActionError(handler, "order", "invalid", "Usage: order search <craft|enchant> <text>.");
        return true;
    }
    std::string query = Trim(text);
    if (query.size() < kMinSearchChars)
    {
        SendActionError(handler, "order", "invalid", "Search text needs at least 3 characters.");
        return true;
    }
    if (kind == "enchant")
        return HandleOrderWhoEnchantSearch(handler, requester, query);
    uint32_t guildId = requester->GetGuildId();
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!guild || !guild->HasRankRight(requester->GetRank(), GR_RIGHT_EOFFNOTE))
    {
        SendActionError(handler, "order", "forbidden", "Your guild rank may not give orders.");
        return true;
    }

    std::vector<std::string> const words = SplitQueryWords(ToLower(query));
    std::string const needle = NormalizeQueryWords(words);

    struct Match
    {
        uint32_t itemId = 0;
        std::string name;
        std::string lower;
        uint32_t quality = 0;
        uint32_t rank = 3;
        std::vector<GuildOrders::WhoBot> bots;
        uint32_t reqSkill = 0;
    };
    std::unordered_map<uint32_t, Match> matches;
    for (Player* bot : BotManager::Instance().GetAllBots())
    {
        if (!bot || !bot->IsInWorld() || bot->GetGuildId() != guildId ||
            GuildOrders::IsOthersAlt(bot->GetGUIDLow(), requester->GetSession()->GetAccountId()))
            continue;
        std::vector<CraftRecipeSnapshot> recipes = OrderableCraftRecipes(bot, words);
        if (recipes.empty())
            continue;
        bool busy = BotBusy(bot->GetGUIDLow());
        for (CraftRecipeSnapshot const& recipe : recipes)
        {
            if (!recipe.itemId)
                continue;
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(recipe.itemId);
            if (!proto)
                continue;
            // Prefiltered by OrderableCraftRecipes: the name already matches.
            Match& match = matches[recipe.itemId];
            if (match.itemId == 0)
            {
                match.itemId = recipe.itemId;
                match.name = proto->Name1;
                match.lower = ToLower(match.name);
                match.quality = proto->Quality;
                match.rank = SearchNameRank(match.lower, needle);
            }
            std::vector<GuildOrders::WhoReagent> reagents;
            for (CraftRecipeReagent const& reagent : recipe.reagents)
                reagents.push_back({ reagent.need, reagent.have });
            uint32_t have = 0, need = 0;
            GuildOrders::CountWhoReagents(reagents, have, need);
            GuildOrders::WhoBot entry;
            entry.name = bot->GetName();
            entry.skill = recipe.skillValue;
            entry.have = have;
            entry.need = need;
            entry.busy = busy;
            match.bots.push_back(std::move(entry));
            if (match.reqSkill == 0 || recipe.reqSkillValue < match.reqSkill)
                match.reqSkill = recipe.reqSkillValue;
        }
    }
    std::vector<Match const*> scored;
    scored.reserve(matches.size());
    for (auto const& entry : matches)
        scored.push_back(&entry.second);
    std::sort(scored.begin(), scored.end(), [](Match const* a, Match const* b)
    {
        if (a->rank != b->rank)
            return a->rank < b->rank;
        return a->name < b->name;
    });
    if (scored.size() > kMaxWhoResults)
        scored.resize(kMaxWhoResults);

    handler->PSendSysMessage("%s", GuildOrders::FormatWhoBegin(kind, uint32_t(scored.size())).c_str());
    for (Match const* match : scored)
    {
        std::vector<GuildOrders::WhoBot> bots = match->bots;
        GuildOrders::SortWhoBots(bots);
        handler->PSendSysMessage("%s",
            GuildOrders::FormatWhoRow(kind, match->itemId, ProtocolSafe(match->name),
                std::to_string(match->quality), "-", match->reqSkill,
                GuildOrders::FormatWhoBots(bots)).c_str());
    }
    handler->PSendSysMessage("%s", GuildOrders::FormatWhoEnd().c_str());
    return true;
}

} // namespace BotCommands
} // namespace TortoiseBots
