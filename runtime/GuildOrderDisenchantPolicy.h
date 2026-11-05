// Disenchant-by-mail service for guild bots: a guild member mails items
// with subject `disenchant`; the bot disenchants what its Enchanting skill
// allows and mails the results back, the rest with a reason.
//
// Header-only and free of core/database types on purpose: the decisions
// are exercised standalone in tools/test_guild_order_disenchant_policy.cpp.
// Callers feed it plain item facts (the core owns DisenchantID and the
// quality/class checks at item load).
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// One item per mail: the core caps a mail at a single attached item
// (MAX_MAIL_ITEMS in the core's Mail.h), so a result mail carries one
// returned item plus a body listing the reasons.
inline constexpr uint32_t kDisenchantMailMaxItems = 1;

// The core casts Disenchant on any owned item with a disenchant loot id and
// does not gate it on Enchanting skill, so the table below keeps a low-skill
// bot from eating items it cannot break down yet.
enum class DisenchantVerdict : uint8_t
{
    Ok,               // disenchantable at the bot's skill
    NoSkill,          // bot has no Enchanting at all
    LowSkill,         // disenchantable, but the bot's skill is too low
    CannotDisenchant, // no disenchant loot: wrong class/quality or flagged
};

inline char const* DisenchantVerdictToken(DisenchantVerdict verdict)
{
    switch (verdict)
    {
        case DisenchantVerdict::Ok:               return "ok";
        case DisenchantVerdict::NoSkill:          return "noskill";
        case DisenchantVerdict::LowSkill:         return "lowskill";
        case DisenchantVerdict::CannotDisenchant: return "cannot disenchant";
    }
    return "cannot disenchant";
}

// "disenchant" subject, case-insensitive, trimmed: every other subject keeps
// today's intake (straight into the bags).
inline std::string TrimDisenchantSubject(std::string const& subject)
{
    size_t begin = 0;
    size_t end = subject.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(subject[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(subject[end - 1])))
        --end;
    std::string out = subject.substr(begin, end - begin);
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

inline bool IsDisenchantServiceSubject(std::string const& subject)
{
    return TrimDisenchantSubject(subject) == "disenchant";
}

// Classic skill floor for disenchanting: 5 * ceil(reqLevel / 5) - 75, with
// an item-level floor 5 * ceil(itemLevel / 5) - 100 for high-level loot
// with a low required level; the higher of the two wins. Epics need 225
// from item level 61 and 300 from item level 90; lower epics use the
// shared row. Clamped to the 1-300 Enchanting range.
inline uint32_t DisenchantRequiredSkill(uint32_t itemLevel, uint32_t requiredLevel, bool isEpic)
{
    if (isEpic && itemLevel >= 90)
        return 300;
    if (isEpic && itemLevel >= 61)
        return 225;
    uint32_t byLevel = (requiredLevel + 4) / 5 * 5;
    uint32_t need = byLevel >= 20 ? (byLevel - 15) * 5 : 1;
    uint32_t byItem = itemLevel >= 5 ? (itemLevel + 4) / 5 * 5 : 5;
    uint32_t needItem = byItem > 20 ? (byItem - 20) * 5 : 1;
    uint32_t needBoth = need > needItem ? need : needItem;
    return needBoth > 300 ? 300 : needBoth;
}

inline DisenchantVerdict ClassifyDisenchantItem(bool hasEnchanting, bool disenchantable,
    uint32_t itemLevel, uint32_t requiredLevel, uint32_t enchantingSkill, bool isEpic)
{
    if (!disenchantable)
        return DisenchantVerdict::CannotDisenchant;
    if (!hasEnchanting)
        return DisenchantVerdict::NoSkill;
    if (enchantingSkill < DisenchantRequiredSkill(itemLevel, requiredLevel, isEpic))
        return DisenchantVerdict::LowSkill;
    return DisenchantVerdict::Ok;
}

// Result mails: every item travels in its own mail (the core's one-item
// cap), so `batches` counts the mails for a result set.
inline uint32_t DisenchantResultBatches(uint32_t returnedItems)
{
    if (returnedItems == 0)
        return 0;
    return (returnedItems + kDisenchantMailMaxItems - 1) / kDisenchantMailMaxItems;
}

// One disenchant result: the loot that landed per cast, kept with the names
// for the subject line. Counts are stack counts, not item counts: a cast
// yields a few dusts plus an essence shard, and one dust stack holds them.
struct DisenchantServiceResult
{
    uint32_t itemId = 0;
    uint32_t count = 0;
};

// Landed results and their persisted form (itemId:count pairs, `;` between):
// malformed pairs are skipped, so a bad row mails nothing instead of
// mis-mailing the bot's own stock.
inline std::vector<DisenchantServiceResult> ParseDisenchantResults(std::string const& text)
{
    std::vector<DisenchantServiceResult> out;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find(';', start);
        if (end == std::string::npos)
            end = text.size();
        std::string pair = text.substr(start, end - start);
        size_t colon = pair.find(':');
        if (colon != std::string::npos)
        {
            char* e1 = nullptr;
            char* e2 = nullptr;
            unsigned long itemId = std::strtoul(pair.substr(0, colon).c_str(), &e1, 10);
            unsigned long count = std::strtoul(pair.substr(colon + 1).c_str(), &e2, 10);
            if (e1 && !*e1 && e2 && !*e2 && itemId && count)
                out.push_back({static_cast<uint32_t>(itemId), static_cast<uint32_t>(count)});
        }
        start = end + 1;
    }
    return out;
}

inline std::string FormatDisenchantResults(std::vector<DisenchantServiceResult> const& results)
{
    std::string out;
    for (DisenchantServiceResult const& result : results)
    {
        if (!result.itemId || !result.count)
            continue;
        if (!out.empty())
            out += ';';
        out += std::to_string(result.itemId) + ':' + std::to_string(result.count);
    }
    return out;
}

// The bot only casts when it can finish one: idle, not mounted, not already
// casting, no trader open. Queued items wait; the maintenance disenchant
// must not eat them first (that guard is the caller's, next to the rare+
// guard, keyed on the sender set below).
inline bool DisenchantCastReady(bool inCombat, bool moving, bool mounted,
    bool casting, bool trading, bool hasWork)
{
    return hasWork && !inCombat && !moving && !mounted && !casting && !trading;
}

// Queued service items for one bot, oldest first: the intake appends, the
// service action drains from the front and mails the refused item back at
// once. Successful casts stay queued as pending, with the landed results
// the loot window gave, until one mailbox visit mails them back. The queue
// lives in the store (persisted per row), so a restart re-queues instead of
// stranding the item; the process list only mirrors what the service action
// works on this session.
struct DisenchantServiceItem
{
    uint32_t senderLow = 0; // member the item and its reasons go back to
    uint32_t itemGuidLow = 0;
    uint32_t itemId = 0;
    std::string itemName;
    bool pendingResults = false; // cast landed, results wait for the mailbox
    bool collectLootOpen = false; // cast done, loot window still to collect
    std::unordered_map<uint32_t, uint32_t> bagBefore; // bag counts before the cast
    std::vector<DisenchantServiceResult> results;
};

// Room to take one more mailed item: a slot for it and one for its result,
// on top of the result slot every item already waiting for its cast needs.
// Whatever does not fit stays in the mailbox for a later pass.
inline bool DisenchantIntakeHasRoom(uint32_t freeSlots, uint32_t waitingItems)
{
    return freeSlots >= 2 + waitingItems;
}
// Disenchant drop-off by trade: a guild member whispers `disenchant`, then
// opens a trade with only items in the normal slots. Memory only (no row):
// the trade intake writes NoteDisenchantQueued rows exactly like the mail
// intake, and the window holder below mirrors the meet-request shape.
inline bool IsDisenchantDropOffWhisper(std::string const& message)
{
    return TrimDisenchantSubject(message) == "disenchant";
}

// Two minutes in server uptime ms, matching the order-trade try budget.
inline constexpr uint32_t kDisenchantDropOffWindowMs = 2u * 60u * 1000u;

struct DisenchantDropOffWindow
{
    uint32_t memberGuidLow = 0; // who may drop off, 0 = none open
    uint32_t openedMs = 0;      // server uptime ms the whisper opened it
};

// Still open for this member: correct member and the window not elapsed.
inline bool DisenchantDropOffOpen(DisenchantDropOffWindow const& window,
    uint32_t memberGuidLow, uint32_t nowMs)
{
    if (!window.memberGuidLow || window.memberGuidLow != memberGuidLow)
        return false;
    return nowMs - window.openedMs < kDisenchantDropOffWindowMs;
}

// The offer the bot accepts: normal slots only, no gold, and at least one
// item. Extras (a non-traded slot or the bot's own side) stay the caller's
// read; the intake runs before the generic trade gates.
struct DisenchantDropOffOffer
{
    uint32_t itemCount = 0; // trader's normal-slot stacks, in slot order
    bool traderGold = false;
};

enum class DisenchantDropOffVerdict : uint8_t
{
    Accept,   // the whole offer fits the room check
    Empty,    // no items offered
    HasGold,  // gold in the window
    TooMany,  // offer exceeds the free room: refuse, the whisper names it
};

inline char const* DisenchantDropOffToken(DisenchantDropOffVerdict verdict)
{
    switch (verdict)
    {
        case DisenchantDropOffVerdict::Accept:  return "accept";
        case DisenchantDropOffVerdict::Empty:   return "empty";
        case DisenchantDropOffVerdict::HasGold: return "gold";
        case DisenchantDropOffVerdict::TooMany: return "toomany";
    }
    return "empty";
}

// How many more items the intake can take: each one fills a bag slot and
// keeps a result slot free, exactly like the mail intake. 0 when not even
// one more fits.
inline uint32_t DisenchantDropOffRoom(uint32_t freeSlots, uint32_t waitingItems)
{
    if (freeSlots < 2 + waitingItems)
        return 0;
    return (freeSlots - waitingItems - 2) / 2 + 1;
}

// A trade accept takes the whole offer at once, so an offer past the room
// is refused outright (the member re-offers fewer): never a partial take.
inline DisenchantDropOffVerdict CheckDisenchantDropOff(DisenchantDropOffOffer const& offer,
    uint32_t freeSlots, uint32_t waitingItems)
{
    if (offer.traderGold)
        return DisenchantDropOffVerdict::HasGold;
    if (offer.itemCount == 0)
        return DisenchantDropOffVerdict::Empty;
    if (offer.itemCount > DisenchantDropOffRoom(freeSlots, waitingItems))
        return DisenchantDropOffVerdict::TooMany;
    return DisenchantDropOffVerdict::Accept;
}

inline std::string DisenchantDropOffRoomText(uint32_t room)
{
    return "I only have room for " + std::to_string(room) + " item" +
        (room == 1 ? "." : "s.");
}

// One work list per bot, touched only on that bot's own map thread; the
// lock covers the lookup, map nodes keep their address.
inline std::vector<DisenchantServiceItem>& DisenchantServiceQueue(uint32_t botGuidLow)
{
    static std::mutex lock;
    static std::unordered_map<uint32_t, std::vector<DisenchantServiceItem>> queues;
    std::lock_guard<std::mutex> guard(lock);
    auto found = queues.find(botGuidLow);
    if (found == queues.end())
        found = queues.emplace(botGuidLow, std::vector<DisenchantServiceItem>()).first;
    return found->second;
}

inline void QueueDisenchantServiceItem(std::vector<DisenchantServiceItem>& queue, uint32_t senderLow,
    uint32_t itemGuidLow, uint32_t itemId)
{
    if (!senderLow || !itemGuidLow || !itemId)
        return;
    for (DisenchantServiceItem const& queued : queue)
        if (queued.itemGuidLow == itemGuidLow)
            return;
    DisenchantServiceItem queued;
    queued.senderLow = senderLow;
    queued.itemGuidLow = itemGuidLow;
    queued.itemId = itemId;
    queue.push_back(queued);
}
// True while the item waits for its own service cast, or its results wait
// for the mailbox: the maintenance disenchant skips it, and the usage gate
// shields the results, next to the order-stock guard (matches the item id:
// the queued guid is the mail attachment, the live guid the bag stack after
// the intake move).
// Reads the persisted rows (store copy, any thread), not the map-thread list.
template <typename Rows>
inline bool DisenchantRowsHoldItem(Rows const& rows, uint32_t itemId)
{
    if (!itemId)
        return false;
    for (auto const& row : rows)
    {
        if (row.itemId == itemId)
            return true;
        for (DisenchantServiceResult const& result : ParseDisenchantResults(row.results))
            if (result.itemId == itemId)
                return true;
    }
    return false;
}

// Subject for one result mail: names what was disenchanted so the member
// can match the mail to the item they sent.
inline std::string DisenchantServiceMailSubject(std::string const& itemName)
{
    if (itemName.empty())
        return "Disenchanted";
    return "Disenchanted: " + itemName;
}
} // namespace GuildOrders
} // namespace TortoiseBots
