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
#include <string>
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

// Result mails: one mail per returned item (the core's one-item cap),
// bodies name the reason for refused items. `batches` counts the mails.
inline uint32_t DisenchantResultBatches(uint32_t returnedItems)
{
    if (returnedItems == 0)
        return 0;
    return (returnedItems + kDisenchantMailMaxItems - 1) / kDisenchantMailMaxItems;
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

// Queued service items, keyed by the sender's low guid: the intake appends,
// the service action drains from the front. A restart empties it; the
// attachments stay in the bags and the member's mail is gone, so the
// service action hands them to the maintenance disenchant instead of
// stranding them.
struct DisenchantServiceItem
{
    uint32_t senderLow = 0; // member the item and its reasons go back to
    uint32_t itemGuidLow = 0;
    uint32_t itemId = 0;
};

inline std::vector<DisenchantServiceItem>& DisenchantServiceQueue()
{
    static std::vector<DisenchantServiceItem> queue;
    return queue;
}

inline void QueueDisenchantServiceItem(uint32_t senderLow, uint32_t itemGuidLow, uint32_t itemId)
{
    if (!senderLow || !itemGuidLow || !itemId)
        return;
    DisenchantServiceItem queued;
    queued.senderLow = senderLow;
    queued.itemGuidLow = itemGuidLow;
    queued.itemId = itemId;
    DisenchantServiceQueue().push_back(queued);
}

// True while the item waits for its own service cast: the maintenance
// disenchant skips it next to the order-stock guard (matches the item id:
// the queued guid is the mail attachment, the live guid the bag stack after
// the intake move).
inline bool DisenchantServiceHasItem(uint32_t itemId)
{
    if (!itemId)
        return false;
    for (DisenchantServiceItem const& queued : DisenchantServiceQueue())
        if (queued.itemId == itemId)
            return true;
    return false;
}
} // namespace GuildOrders
} // namespace TortoiseBots
