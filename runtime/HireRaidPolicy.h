#pragma once

// Issue #900: `.bot hire raid <preset|composition> [tier]` hires a whole raid
// of companions through the single-hire path. This header is the pure part:
// the composition grammar, the class-spread and preset config strings, and the
// roster builder that turns them into an ordered list of (class, role) slots.
// No core headers, so tools/test_hire_raid_policy.cpp builds it standalone.
// Prices, the cap, the group conversion and the hires themselves live in
// HireProvisionService::HireRaid.
//
// Grammar (tokens are lower case, in any order):
//   <N>t | <N>h | <N>d                role counts (also tank/healer/dps words,
//                                     "3 tanks", "12 heal", "24 dps")
//   <N> <class>[:<role>]              explicit class, e.g. "3 warrior:tank"
//   t<N> | tier<N> | mc | naxx ...    gear tier (ParseHireTierWord), last wins
//   level=<n> | level <n> | lvl=<n>   hire level (ParseHireLevelWord), as `.bot hire`
//   <preset>                          first token only, expands to a config
//                                     composition; later tokens extend it

#include "HireLevelPolicy.h"
#include "HireSpecPolicy.h"
#include "HireTierRequestPolicy.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{

// A 40-man raid holds the player and 39 companions (AiPlayerbot.HireMaxBotsPerPlayer
// clamps at the same number).
constexpr uint32_t kHireRaidMaxCompanions = 39;

// A bound raid's commander (`.bot commander bind <LeadBot> raid`) stands OUTSIDE the raid group, so the
// group has 40 free seats for bots: one more companion than a player's own raid. This is
// RemoteCommander::kMaxRaidBindSize (checked in HireProvisionService.cpp); the config clamp
// (AiPlayerbot.HireMaxBotsPerPlayer <= 39) stays as it is for players.
constexpr uint32_t kHireBoundRaidMaxCompanions = 40;

// The requester commands a bound raid and is not a member of its group: his hires join the lead bot's
// raid group and the binding, not a party of his own. A commander inside the group, an unbound
// commander and every other player hire into their own raid (player + 39).
inline bool HireIntoBoundRaid(bool commandsBoundRaid, bool inBoundRaidGroup)
{
    return commandsBoundRaid && !inBoundRaidGroup;
}

// How many companions the requester may command: the config value (already clamped to 39) for everyone;
// the bound commander outside the group gets the 40th seat when the config allows the full 39. A lower
// operator cap holds for him too.
inline uint32_t HireMaxCompanions(uint32_t configMax, bool intoBoundRaid)
{
    uint32_t const base = configMax > kHireRaidMaxCompanions ? kHireRaidMaxCompanions : configMax;
    return intoBoundRaid && base == kHireRaidMaxCompanions ? kHireBoundRaidMaxCompanions : base;
}

enum class HireSeatVerdict : uint8_t
{
    Ok,
    CapReached, // owned >= the companion limit
    RaidFull,   // the bound raid already holds 40 bots
};

struct HireSeatFacts
{
    uint32_t configMax = 0;               // AiPlayerbot.HireMaxBotsPerPlayer
    uint32_t owned = 0;                   // the requester's hires, live and mustering (CountHired)
    bool commandsBoundRaid = false;       // he is the commander of a raid binding
    bool inBoundRaidGroup = false;        // ...and a member of that raid's group
    uint32_t boundRaidSeatsTaken = 0;     // members of the bound raid's group plus his hires still mustering
};

struct HireSeatDecision
{
    HireSeatVerdict verdict = HireSeatVerdict::Ok;
    uint32_t maxCompanions = 0;
    bool intoBoundRaid = false;
    std::string message; // player-facing, empty when Ok
};

// The one rule for "may this requester hire one more companion". The party and raid seat checks of
// his own group stay in HireProvisionService::Hire; this decides the companion limit and, for the
// bound commander, the 40 seats of the bound raid.
inline HireSeatDecision DecideHireSeat(HireSeatFacts const& facts)
{
    HireSeatDecision decision;
    decision.intoBoundRaid = HireIntoBoundRaid(facts.commandsBoundRaid, facts.inBoundRaidGroup);
    decision.maxCompanions = HireMaxCompanions(facts.configMax, decision.intoBoundRaid);
    if (decision.intoBoundRaid && facts.boundRaidSeatsTaken >= kHireBoundRaidMaxCompanions)
    {
        decision.verdict = HireSeatVerdict::RaidFull;
        decision.message = "Your bound raid already holds the maximum of " + std::to_string(kHireBoundRaidMaxCompanions) +
            " bots (" + std::to_string(facts.boundRaidSeatsTaken) + " seats taken).";
        return decision;
    }
    if (facts.owned >= decision.maxCompanions)
    {
        decision.verdict = HireSeatVerdict::CapReached;
        decision.message = "You already command the maximum number of companions.";
    }
    return decision;
}

// C6 H1: a commander whose binding ends (unbind, his logout, the binding dropped) hires as a player again, so
// the companions above the ordinary limit (HireMaxCompanions(configMax, false)) leave, the most recently hired
// first. hireOrder holds one number per hire he owns, in any order (higher = hired later, HireLifecycle::Claim).
// Returns the indexes into hireOrder of the hires that leave, newest first; empty within the limit.
inline std::vector<size_t> PickSurplusHires(std::vector<uint64_t> const& hireOrder, uint32_t ordinaryCap)
{
    std::vector<size_t> leave;
    if (hireOrder.size() <= ordinaryCap)
        return leave;
    for (size_t i = 0; i < hireOrder.size(); ++i)
        leave.push_back(i);
    std::stable_sort(leave.begin(), leave.end(),
        [&hireOrder](size_t a, size_t b) { return hireOrder[a] > hireOrder[b]; });
    leave.resize(hireOrder.size() - ordinaryCap);
    return leave;
}

// C6: what a binding's release does with the commander's hires. A bound-raid seat hire still mustering (not
// joined yet) is cancelled first, its fee refunded (HireProvisionService::CancelBoundSeatHire); only then are
// the rest counted, and the newest above the ordinary limit are dismissed (PickSurplusHires, no refund).
struct ReleaseHire
{
    uint64_t hireOrder = 0;
    bool musteringSeat = false; // a bound-raid seat hire that has not joined the raid
};

struct ReleasePlan
{
    std::vector<size_t> cancel;  // indexes into the hires: cancelled with refund, first
    std::vector<size_t> dismiss; // then dismissed, newest first
};

inline ReleasePlan PlanBindingRelease(std::vector<ReleaseHire> const& hires, uint32_t ordinaryCap)
{
    ReleasePlan plan;
    std::vector<size_t> kept;
    std::vector<uint64_t> keptOrder;
    for (size_t i = 0; i < hires.size(); ++i)
    {
        if (hires[i].musteringSeat)
            plan.cancel.push_back(i);
        else
        {
            kept.push_back(i);
            keptOrder.push_back(hires[i].hireOrder);
        }
    }
    for (size_t index : PickSurplusHires(keptOrder, ordinaryCap))
        plan.dismiss.push_back(kept[index]);
    return plan;
}

// C6: a cancelled bound-raid seat hire gives its fee back the way Hire() does on a failed hire: only once the
// hire is durably undone. Here that is the deletion intent of its character recorded (ledger row marked
// 'dismissed' and the deletion queued, HireLifecycle::DismissForRefund). Until then no gold moves and the
// deletion is retried (Update, once a second); a refund is paid once and never again.
constexpr uint32_t kSeatCancelMaxAttempts = 60;

struct SeatCancelState
{
    bool deletionQueued = false; // the character's deletion intent is durable
    bool refunded = false;       // the fee went back (in hand or by mail)
    uint32_t attempts = 0;       // deletion attempts so far (the first one included)
};

enum class SeatCancelAction : uint8_t
{
    Refund,        // deletion durable, fee not yet returned: return it now (once)
    RetryDeletion, // deletion not durable yet: no refund, try again later
    Done,          // refunded: nothing more, ever
    GiveUp,        // the deletion never became durable: no refund (the hire left no live companion)
};

inline SeatCancelAction NextSeatCancelAction(SeatCancelState const& state)
{
    if (state.refunded)
        return SeatCancelAction::Done;
    if (state.deletionQueued)
        return SeatCancelAction::Refund;
    return state.attempts >= kSeatCancelMaxAttempts ? SeatCancelAction::GiveUp : SeatCancelAction::RetryDeletion;
}

// C6 H2: how a hire was authorized. A bound-raid seat is a hire of a bound raid's commander standing outside
// its group (it may be his 40th): it joins only that binding's raid, never a party with the commander.
enum class HireSeatMode : uint8_t
{
    Normal,
    BoundRaidSeat,
};

inline HireSeatMode HireSeatModeFor(bool intoBoundRaid)
{
    return intoBoundRaid ? HireSeatMode::BoundRaidSeat : HireSeatMode::Normal;
}

// A seat hire is rechecked before every join attempt until it stands in the bound raid; a normal hire never.
inline bool HireNeedsSeatRecheck(HireSeatMode mode, bool joined)
{
    return mode == HireSeatMode::BoundRaidSeat && !joined;
}

// The binding a seat hire was authorized for, recorded at hire time.
struct BoundSeatGrant
{
    uint64_t commanderGuid = 0;
    uint64_t leadGuid = 0;
    uint32_t groupId = 0;
};

// The same facts right before the join (world thread, in the same call as the join).
struct BoundSeatNow
{
    uint64_t commanderGuid = 0;
    bool commanderOnline = false;
    bool bound = false;            // a standing raid binding whose lead bot is in the bound group
    uint64_t leadGuid = 0;
    uint32_t groupId = 0;
    bool commanderInGroup = false;
    uint32_t members = 0;          // members of the bound group now
    bool botInGroup = false;       // the hire already stands in it (takes no new seat)
    uint32_t owned = 0;            // the commander's hires, this one included (CountHired)
    uint32_t configMax = 0;        // AiPlayerbot.HireMaxBotsPerPlayer
};

enum class BoundSeatVerdict : uint8_t
{
    Join,
    Cancel,
};

struct BoundSeatCheck
{
    BoundSeatVerdict verdict = BoundSeatVerdict::Cancel;
    std::string reason; // player-facing, empty on Join
};

// Join only when nothing changed since the hire was authorized: the same standing binding (commander, lead bot,
// group), the commander online and still outside the group, a free seat, and his companions within the 40 of a
// bound raid. Anything else cancels the hire (the caller never falls back to an ordinary party).
inline BoundSeatCheck RecheckBoundSeat(BoundSeatGrant const& grant, BoundSeatNow const& now)
{
    BoundSeatCheck check;
    if (!now.commanderOnline)
        check.reason = "you went offline before it arrived";
    else if (!now.bound || now.commanderGuid != grant.commanderGuid || now.leadGuid != grant.leadGuid ||
        now.groupId != grant.groupId)
        check.reason = "the raid binding it was hired for has ended";
    else if (now.commanderInGroup)
        check.reason = "you joined the raid group, so the extra seat is gone";
    else if (!now.botInGroup && now.members >= kHireBoundRaidMaxCompanions)
        check.reason = "the bound raid is full (" + std::to_string(kHireBoundRaidMaxCompanions) + " bots)";
    else if (now.owned > HireMaxCompanions(now.configMax, true))
        check.reason = "you command more companions than a bound raid allows";
    else
        check.verdict = BoundSeatVerdict::Join;
    return check;
}

// Defaults of AiPlayerbot.HireRaidSpread / AiPlayerbot.HireRaidPresets. The
// shipped template carries the same strings (AGENTS.md: fallback == template).
// Spread: per role, classes handed out round-robin; repeat a class to weight it.
constexpr char const* kHireRaidSpreadDefault =
    "tank=warrior,druid,paladin;"
    "healer=priest,priest,paladin,shaman,druid;"
    "dps=mage,warlock,rogue,hunter,warrior,mage,warlock,rogue,hunter,shaman";
// Presets: name=composition; the composition is the same grammar as the command.
constexpr char const* kHireRaidPresetsDefault =
    "zg=2t 4h 13d t2;aq20=2t 4h 13d t5;onyxia=3t 10h 26d t3;mc=3t 10h 26d t3;"
    "bwl=3t 10h 26d t4;aq40=3t 10h 26d t5;naxx=3t 10h 26d t6";

// One composition entry. classId 0 = pick from the spread by role; role 0 =
// the class default role (explicit entries only).
struct HireRaidEntry
{
    uint32_t count = 0;
    uint8_t classId = 0;
    uint8_t role = 0;
};

struct HireRaidRequest
{
    std::vector<HireRaidEntry> entries;
    uint8_t tier = 0;
    uint32_t level = 0; // 0 = the hirer's level, as `.bot hire`; clamped by ResolveHireLevel
};

struct HireRaidSlot
{
    uint8_t classId = 0;
    uint8_t role = 0;
};

// Class/role truths that need the core (ClassCanRole, DefaultRoleForClass),
// handed in so this header stays core-free.
struct HireRaidClassRules
{
    bool (*canRole)(uint8_t classId, uint8_t role) = nullptr;
    uint8_t (*defaultRole)(uint8_t classId) = nullptr;
};

struct HireRaidSpread
{
    std::vector<uint8_t> tank;
    std::vector<uint8_t> healer;
    std::vector<uint8_t> dps;
};

struct HireRaidPreset
{
    std::string name;
    std::vector<std::string> tokens;
};

inline std::string HireRaidLower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline std::vector<std::string> HireRaidSplit(std::string const& text, char delim)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
    {
        if (c == delim)
        {
            out.push_back(cur);
            cur.clear();
        }
        else
            cur += c;
    }
    out.push_back(cur);
    return out;
}

inline std::string HireRaidTrim(std::string const& s)
{
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos)
        return std::string();
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

inline std::vector<std::string> HireRaidWords(std::string const& text)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
    {
        if (c == ' ' || c == '\t')
        {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        }
        else
            cur += c;
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

// Class word -> class id; 0 for anything else (death knights do not exist
// here). Lower case input.
inline uint8_t HireRaidClassFromWord(std::string const& w)
{
    if (w == "warrior" || w == "war")
        return kHireClassWarrior;
    if (w == "paladin" || w == "pally" || w == "pal")
        return kHireClassPaladin;
    if (w == "hunter" || w == "hunt")
        return kHireClassHunter;
    if (w == "rogue")
        return kHireClassRogue;
    if (w == "priest")
        return kHireClassPriest;
    if (w == "shaman" || w == "sham")
        return kHireClassShaman;
    if (w == "mage")
        return kHireClassMage;
    if (w == "warlock" || w == "lock")
        return kHireClassWarlock;
    if (w == "druid")
        return kHireClassDruid;
    return 0;
}

inline char const* HireRaidClassName(uint8_t classId)
{
    switch (classId)
    {
        case kHireClassWarrior: return "warrior";
        case kHireClassPaladin: return "paladin";
        case kHireClassHunter: return "hunter";
        case kHireClassRogue: return "rogue";
        case kHireClassPriest: return "priest";
        case kHireClassShaman: return "shaman";
        case kHireClassMage: return "mage";
        case kHireClassWarlock: return "warlock";
        case kHireClassDruid: return "druid";
        default: return "class";
    }
}

inline uint8_t HireRaidRoleFromWord(std::string const& w)
{
    if (w == "t" || w == "tank" || w == "tanks")
        return kHireRoleTank;
    if (w == "h" || w == "heal" || w == "healer" || w == "healers")
        return kHireRoleHealer;
    if (w == "d" || w == "dps")
        return kHireRoleDps;
    return 0;
}

inline char const* HireRaidRoleName(uint8_t role)
{
    switch (role)
    {
        case kHireRoleTank: return "tank";
        case kHireRoleHealer: return "healer";
        case kHireRoleDps: return "dps";
        default: return "role";
    }
}

// 1..99 digits only.
inline bool HireRaidParseCount(std::string const& s, uint32_t& out)
{
    if (s.empty() || s.size() > 2)
        return false;
    uint32_t v = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + static_cast<uint32_t>(c - '0');
    }
    if (v == 0)
        return false;
    out = v;
    return true;
}

// Composition tokens -> request. False with a player-facing error.
inline bool ParseHireRaidSpec(std::vector<std::string> const& tokens, HireRaidRequest& out, std::string& error)
{
    out = HireRaidRequest();
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        std::string const& tok = tokens[i];
        size_t digits = 0;
        while (digits < tok.size() && tok[digits] >= '0' && tok[digits] <= '9')
            ++digits;

        if (digits > 0 && digits < tok.size())
        {
            // "3t", "12h", "24dps": count glued to a role word.
            uint32_t count = 0;
            uint8_t role = HireRaidRoleFromWord(tok.substr(digits));
            if (!HireRaidParseCount(tok.substr(0, digits), count) || !role)
            {
                error = "I do not understand '" + tok + "'. Use counts like 3t 12h 24d.";
                return false;
            }
            HireRaidEntry entry;
            entry.count = count;
            entry.role = role;
            out.entries.push_back(entry);
            continue;
        }
        if (digits > 0)
        {
            // Bare count: "3 tanks" or "3 warrior:tank".
            uint32_t count = 0;
            if (!HireRaidParseCount(tok, count))
            {
                error = "'" + tok + "' is not a count between 1 and 99.";
                return false;
            }
            if (i + 1 >= tokens.size())
            {
                error = "Name what the " + tok + " are, for example " + tok + " warrior:tank or " + tok + "t.";
                return false;
            }
            std::string const& next = tokens[++i];
            HireRaidEntry entry;
            entry.count = count;
            if (uint8_t roleOnly = HireRaidRoleFromWord(next))
                entry.role = roleOnly;
            else
            {
                size_t colon = next.find(':');
                std::string classWord = colon == std::string::npos ? next : next.substr(0, colon);
                entry.classId = HireRaidClassFromWord(classWord);
                if (!entry.classId)
                {
                    error = "'" + classWord + "' is not a class I can hire.";
                    return false;
                }
                if (colon != std::string::npos)
                {
                    entry.role = HireRaidRoleFromWord(next.substr(colon + 1));
                    if (!entry.role)
                    {
                        error = "'" + next.substr(colon + 1) + "' is not a role. Use tank, healer or dps.";
                        return false;
                    }
                }
            }
            out.entries.push_back(entry);
            continue;
        }
        // A level word, as `.bot hire`: level=60, lvl:42, level 60. The raid's
        // whole roster hires at one level (no bare number: that is a count here).
        uint32_t levelWord = 0;
        HireLevelWord const levelKind = ParseHireLevelWord(tok, levelWord);
        if (levelKind == HireLevelWord::Bad)
        {
            error = "Bad level '" + tok + "': use level=<1-60>, for example .bot hire raid 3t 12h 26d level=60.";
            return false;
        }
        if (levelKind == HireLevelWord::Ok)
        {
            if (out.level)
            {
                error = "Give only one level.";
                return false;
            }
            out.level = levelWord;
            continue;
        }
        if (levelKind == HireLevelWord::Prefix)
        {
            if (i + 1 >= tokens.size() || !ParseHireLevelNumber(tokens[i + 1], levelWord))
            {
                error = "Bad level: use level=<1-60>, for example .bot hire raid 3t 12h 26d level=60.";
                return false;
            }
            if (out.level)
            {
                error = "Give only one level.";
                return false;
            }
            out.level = levelWord;
            ++i;
            continue;
        }
        uint8_t tier = 0;
        if (ParseHireTierWord(tok, tier))
        {
            out.tier = tier;
            continue;
        }
        error = "I do not understand '" + tok + "'.";
        return false;
    }
    if (out.entries.empty())
    {
        error = "Name the raid: .bot hire raid 3t 12h 24d [tier], .bot hire raid 3 warrior:tank 12 priest:healer 24 mage, or a preset.";
        return false;
    }
    return true;
}

// "tank=warrior,druid;healer=priest;dps=mage,rogue". All three roles needed.
inline bool ParseHireRaidSpread(std::string const& text, HireRaidSpread& out)
{
    out = HireRaidSpread();
    bool seen[3] = { false, false, false };
    for (std::string const& part : HireRaidSplit(text, ';'))
    {
        std::string item = HireRaidTrim(part);
        if (item.empty())
            continue;
        size_t eq = item.find('=');
        if (eq == std::string::npos)
            return false;
        uint8_t role = HireRaidRoleFromWord(HireRaidLower(HireRaidTrim(item.substr(0, eq))));
        if (role != kHireRoleTank && role != kHireRoleHealer && role != kHireRoleDps)
            return false;
        std::vector<uint8_t>& list = role == kHireRoleTank ? out.tank : role == kHireRoleHealer ? out.healer : out.dps;
        bool& flag = role == kHireRoleTank ? seen[0] : role == kHireRoleHealer ? seen[1] : seen[2];
        if (flag)
            return false;
        for (std::string const& word : HireRaidSplit(item.substr(eq + 1), ','))
        {
            uint8_t classId = HireRaidClassFromWord(HireRaidLower(HireRaidTrim(word)));
            if (!classId)
                return false;
            list.push_back(classId);
        }
        flag = true;
    }
    return seen[0] && seen[1] && seen[2];
}

// "zg=2t 4h 13d t2;mc=3t 10h 26d t3". Every composition must parse.
inline bool ParseHireRaidPresets(std::string const& text, std::vector<HireRaidPreset>& out)
{
    out.clear();
    for (std::string const& part : HireRaidSplit(text, ';'))
    {
        std::string item = HireRaidTrim(part);
        if (item.empty())
            continue;
        size_t eq = item.find('=');
        if (eq == std::string::npos)
            return false;
        HireRaidPreset preset;
        preset.name = HireRaidLower(HireRaidTrim(item.substr(0, eq)));
        preset.tokens = HireRaidWords(HireRaidLower(item.substr(eq + 1)));
        if (preset.name.empty() || preset.tokens.empty())
            return false;
        HireRaidRequest probe;
        std::string error;
        if (!ParseHireRaidSpec(preset.tokens, probe, error))
            return false;
        out.push_back(preset);
    }
    return true;
}

// If tokens[0] names a preset, replace it by the preset's composition (the
// remaining tokens follow, so a later tier word overrides the preset's tier).
inline bool ExpandHireRaidPreset(std::vector<HireRaidPreset> const& presets, std::vector<std::string>& tokens)
{
    if (tokens.empty())
        return false;
    for (HireRaidPreset const& preset : presets)
    {
        if (preset.name != tokens[0])
            continue;
        std::vector<std::string> expanded = preset.tokens;
        expanded.insert(expanded.end(), tokens.begin() + 1, tokens.end());
        tokens.swap(expanded);
        return true;
    }
    return false;
}

// Request -> ordered slots: tanks first, then healers, then dps (the raid
// fills its subgroups in join order, so tanks end up beside healers), each
// role in request order. availableClassMask has bit (1 << classId) per class
// the requester's faction can field. requested is the pre-cap total; the slots
// are cut to cap from the dps end. maxTotal is the most the request may ask
// for: 39 for a player's raid, kHireBoundRaidMaxCompanions for a bound
// commander outside his raid. False with a player-facing error.
inline bool BuildHireRaidRoster(HireRaidRequest const& request, HireRaidSpread const& spread,
    uint32_t availableClassMask, HireRaidClassRules const& rules, uint32_t cap,
    std::vector<HireRaidSlot>& out, uint32_t& requested, std::string& error,
    uint32_t maxTotal = kHireRaidMaxCompanions)
{
    out.clear();
    requested = 0;
    if (!rules.canRole || !rules.defaultRole)
    {
        error = "Internal error: missing class rules.";
        return false;
    }
    uint32_t total = 0;
    for (HireRaidEntry const& entry : request.entries)
        total += entry.count;
    requested = total;
    if (total == 0)
    {
        error = "Nothing to hire.";
        return false;
    }
    if (total > maxTotal)
    {
        error = maxTotal == kHireBoundRaidMaxCompanions
            ? "A bound raid holds at most 40 companions; you asked for " + std::to_string(total) + "."
            : "A raid holds at most 39 companions plus you; you asked for " + std::to_string(total) + ".";
        return false;
    }

    // Spread lists restricted to what the faction can field in that role.
    std::vector<uint8_t> usable[3];
    for (int r = 0; r < 3; ++r)
    {
        uint8_t role = static_cast<uint8_t>(1u << r);
        std::vector<uint8_t> const& list = role == kHireRoleTank ? spread.tank : role == kHireRoleHealer ? spread.healer : spread.dps;
        for (uint8_t classId : list)
            if ((availableClassMask & (1u << classId)) && rules.canRole(classId, role))
                usable[r].push_back(classId);
    }
    size_t cursor[3] = { 0, 0, 0 };

    std::vector<HireRaidSlot> byRole[3];
    for (HireRaidEntry const& entry : request.entries)
    {
        uint8_t classId = entry.classId;
        uint8_t role = entry.role;
        if (classId)
        {
            if (!(availableClassMask & (1u << classId)))
            {
                error = std::string("Your faction cannot field a ") + HireRaidClassName(classId) + ".";
                return false;
            }
            if (!role)
                role = rules.defaultRole(classId);
            if (!rules.canRole(classId, role))
            {
                error = std::string("A ") + HireRaidClassName(classId) + " cannot be a " + HireRaidRoleName(role) + ".";
                return false;
            }
        }
        int ri = role == kHireRoleTank ? 0 : role == kHireRoleHealer ? 1 : 2;
        for (uint32_t n = 0; n < entry.count; ++n)
        {
            HireRaidSlot slot;
            slot.role = role;
            if (classId)
                slot.classId = classId;
            else
            {
                if (usable[ri].empty())
                {
                    error = std::string("No class in the ") + HireRaidRoleName(role) +
                        " spread (AiPlayerbot.HireRaidSpread) is available to your faction.";
                    return false;
                }
                slot.classId = usable[ri][cursor[ri]++ % usable[ri].size()];
            }
            byRole[ri].push_back(slot);
        }
    }
    for (int r = 0; r < 3; ++r)
        out.insert(out.end(), byRole[r].begin(), byRole[r].end());
    if (out.size() > cap)
        out.resize(cap);
    return true;
}

} // namespace TortoiseBots
