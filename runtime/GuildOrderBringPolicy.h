#pragma once

// "Materials I bring" (bring=) rules: the giver ticks base reagents they will
// deliver themselves, so the ladder never sources those legs and the preview
// shows them as the player's share. Header-only and free of core/database
// types on purpose: exercised standalone in
// tools/test_guild_order_bring_policy.cpp.
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// Comma-separated item ids as given on `bring=1,2,3`. Empty parts are
// skipped so a trailing comma stays harmless; anything unparseable or zero
// refuses the whole option, like gold= refuses a bad cap.
inline bool ParseBringList(std::string const& token, std::vector<uint32_t>& out)
{
    out.clear();
    size_t start = 0;
    bool any = false;
    while (start <= token.size())
    {
        size_t end = token.find(',', start);
        std::string part = token.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty())
        {
            if (part.size() > 10)
                return false;
            for (char c : part)
                if (c < '0' || c > '9')
                    return false;
            unsigned long id = std::stoul(part);
            if (id == 0 || id > 0xFFFFFFFFul)
                return false;
            uint32_t itemId = static_cast<uint32_t>(id);
            bool known = false;
            for (uint32_t knownId : out)
                if (knownId == itemId)
                {
                    known = true;
                    break;
                }
            if (!known)
                out.push_back(itemId);
            any = true;
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return any;
}

inline bool BringListHas(std::vector<uint32_t> const& bring, uint32_t itemId)
{
    if (!itemId)
        return false;
    for (uint32_t ticked : bring)
        if (ticked == itemId)
            return true;
    return false;
}

// Wire and database token: ticked ids joined by `,`, or `-` when empty.
inline std::string BringItemsToken(std::vector<uint32_t> const& bring)
{
    std::string out;
    for (uint32_t itemId : bring)
    {
        if (!itemId)
            continue;
        if (!out.empty())
            out += ',';
        out += std::to_string(itemId);
    }
    return out.empty() ? "-" : out;
}

inline std::vector<uint32_t> ParseBringItemsToken(std::string const& token)
{
    std::vector<uint32_t> out;
    if (token.empty() || token == "-")
        return out;
    std::vector<uint32_t> parsed;
    if (ParseBringList(token, parsed))
        out = parsed;
    return out;
}

// A ticked base reagent is never sourced on any leg: no bank withdrawal,
// auction buy, farm or vendor trip, self craft, or sub-order. Bags and
// mailbox still count (the ladder nets them before naming a need), so the
// rest lands on the giver's need list through the normal need path.
inline bool BringTickedBlocksLeg(std::vector<uint32_t> const& bring, uint32_t itemId)
{
    return BringListHas(bring, itemId);
}

// Keep only ids that are base reagents of the craft: anything else was never
// the bot's to source and stays out of the persisted set.
inline std::vector<uint32_t> FilterBringToBaseReagents(std::vector<uint32_t> const& bring,
    uint32_t const* baseIds, size_t baseCount)
{
    std::vector<uint32_t> out;
    for (uint32_t itemId : bring)
    {
        if (!itemId)
            continue;
        bool base = false;
        if (baseIds)
            for (size_t i = 0; i < baseCount; ++i)
                if (baseIds[i] == itemId)
                {
                    base = true;
                    break;
                }
        if (!base)
            continue;
        if (!BringListHas(out, itemId))
            out.push_back(itemId);
    }
    return out;
}

} // namespace GuildOrders
} // namespace TortoiseBots
