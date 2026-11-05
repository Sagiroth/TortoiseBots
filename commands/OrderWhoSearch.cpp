// pi-lens-ignore-file: all
// `.bot order search craft <text>` without a bot: every crafted item whose
// name matches the text (same word matching and rank order as the per-bot
// search), with all online guild bots that know an orderable recipe for it.
// One TBM:ORDER_WHO row per crafted item; the bots field carries
// name:skill:have:need:busy per bot, sorted by skill high to low. At most
// 30 rows. The reply shape already carries an enchant extension point
// (kind, spell id, slot, reqSkill); only craft rows are sent now.
// pi-lens-ignore: clang:pp_file_not_found
#include "BotCommandHandlers.h"
#include "CommandShared.h"

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
    if (kind != "craft")
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
        if (!bot || !bot->IsInWorld() || bot->GetGuildId() != guildId)
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
