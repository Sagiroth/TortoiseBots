#pragma once

// Issue #884, phase 1: the remote commander. A GM, or the owner of every bot
// in a bot-only group of up to five, binds that group to themselves with
// `.bot commander bind <LeadBot> [<Bot>...]` and then leads it from anywhere
// with `.bot cmd <order>`: the orders name their target by GUID or a 3D point,
// so they need neither the commander's selection nor the commander's presence
// in the group. `AiPlayerbot.RemoteCommander` (default 0) switches it on.
//
// Everything that decides here is a pure function of plain facts, so
// tools/test_remote_commander_policy.cpp exercises it standalone: the binding
// table (one commander per group, one group per commander), the release rule,
// the order auth gate, GUID and move-point parsing, and the target and move
// verdicts. The world-facing glue (group formation, the per-order executors,
// the 1 s release sweep) lives in commands/BotCommands.cpp and runs on the world
// thread only, like every `.bot` command (commands/BotCommandQueue.h).
//
// Plain data and the standard library only.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace TortoiseBots {
namespace RemoteCommander {

// A party is five; a bound group is never a raid.
constexpr size_t kMaxGroupSize = 5;
// The release sweep runs at most this often, and only while a binding exists.
constexpr uint32_t kSweepIntervalMs = 1000;
// A move point further than this from the walkable end of its path is not on
// the ground (inside a wall, in the air): refused as off-mesh.
constexpr float kMaxMoveEndOffset = 5.0f;
// Coordinates outside this box are not on any 1.12 map.
constexpr float kMaxMapCoordinate = 20000.0f;

// ---------------------------------------------------------------------------
// Binding table
// ---------------------------------------------------------------------------

struct Binding
{
    uint64_t commanderGuid = 0;
    uint64_t leadGuid = 0;
    // Group::GetId() of the lead bot's group when the binding was made. The
    // group is looked up again through the lead bot every time, never kept as
    // a pointer: a disbanded group is freed.
    uint32_t groupId = 0;
    // Followers whose follow target the binding set to the lead bot. Only
    // these are put back on release.
    std::vector<uint64_t> followers;
};

enum class BindResult : uint8_t
{
    Bound,
    CommanderAlreadyBound,
    GroupAlreadyBound,
    Invalid,
};

// One commander per group and one group per commander. World thread only.
class BindingTable
{
public:
    BindResult Bind(Binding const& binding)
    {
        if (!binding.commanderGuid || !binding.leadGuid || !binding.groupId ||
            binding.commanderGuid == binding.leadGuid)
            return BindResult::Invalid;
        if (m_byCommander.count(binding.commanderGuid))
            return BindResult::CommanderAlreadyBound;
        if (FindByGroup(binding.groupId))
            return BindResult::GroupAlreadyBound;
        m_byCommander[binding.commanderGuid] = binding;
        return BindResult::Bound;
    }

    // Removes and returns the commander's binding (empty Binding when none).
    Binding Unbind(uint64_t commanderGuid)
    {
        Binding removed;
        auto it = m_byCommander.find(commanderGuid);
        if (it == m_byCommander.end())
            return removed;
        removed = it->second;
        m_byCommander.erase(it);
        return removed;
    }

    Binding const* FindByCommander(uint64_t commanderGuid) const
    {
        auto it = m_byCommander.find(commanderGuid);
        return it == m_byCommander.end() ? nullptr : &it->second;
    }

    Binding* FindByCommander(uint64_t commanderGuid)
    {
        auto it = m_byCommander.find(commanderGuid);
        return it == m_byCommander.end() ? nullptr : &it->second;
    }

    Binding const* FindByGroup(uint32_t groupId) const
    {
        if (!groupId)
            return nullptr;
        for (auto const& entry : m_byCommander)
            if (entry.second.groupId == groupId)
                return &entry.second;
        return nullptr;
    }

    // Records a follower whose follow target the binding set (once).
    void AddFollower(uint64_t commanderGuid, uint64_t botGuid)
    {
        Binding* binding = FindByCommander(commanderGuid);
        if (!binding || !botGuid || botGuid == binding->leadGuid)
            return;
        for (uint64_t guid : binding->followers)
            if (guid == botGuid)
                return;
        binding->followers.push_back(botGuid);
    }

    // Drops a follower whose follow target was put back (it left the group).
    void RemoveFollower(uint64_t commanderGuid, uint64_t botGuid)
    {
        Binding* binding = FindByCommander(commanderGuid);
        if (!binding)
            return;
        for (auto it = binding->followers.begin(); it != binding->followers.end(); ++it)
            if (*it == botGuid)
            {
                binding->followers.erase(it);
                return;
            }
    }

    // The binding that holds this bot as its lead or as a follower, if any.
    Binding const* FindByBot(uint64_t botGuid) const
    {
        if (!botGuid)
            return nullptr;
        for (auto const& entry : m_byCommander)
        {
            if (entry.second.leadGuid == botGuid)
                return &entry.second;
            for (uint64_t follower : entry.second.followers)
                if (follower == botGuid)
                    return &entry.second;
        }
        return nullptr;
    }

    std::vector<uint64_t> Commanders() const
    {
        std::vector<uint64_t> result;
        result.reserve(m_byCommander.size());
        for (auto const& entry : m_byCommander)
            result.push_back(entry.first);
        return result;
    }

    bool Empty() const { return m_byCommander.empty(); }
    size_t Size() const { return m_byCommander.size(); }
    void Clear() { m_byCommander.clear(); }

private:
    std::unordered_map<uint64_t, Binding> m_byCommander;
};

// ---------------------------------------------------------------------------
// Release rule
// ---------------------------------------------------------------------------

// What the world sees about a binding right now (looked up by guid each time).
struct BindingState
{
    bool enabled = true;          // AiPlayerbot.RemoteCommander
    bool commanderOnline = false; // live player with a network session
    bool leadOnline = false;      // lead bot in world
    uint32_t leadGroupId = 0;     // the lead bot's current group, 0 = none
    bool leadIsLeader = false;    // the lead bot still leads that group
    bool leadGroupIsRaidOrBg = false; // the group converted to a raid (or is a battleground group)
    bool realPlayerInGroup = false; // a human joined the bound group
    size_t memberCount = 0;
};

enum class ReleaseReason : uint8_t
{
    None,
    Disabled,
    CommanderLeft,
    LeadLeft,
    GroupChanged,
    LeaderChanged,
    PlayerJoined,
    GroupTooLarge,
    GroupRaid,
    // Not a CheckBinding verdict: the commander's own `.bot commander unbind`.
    Unbound,
};

// None while the binding still stands; otherwise why it ends. A binding never
// outlives its commander, its lead bot or its group, and the group stays a
// bot-only party led by the lead bot.
inline ReleaseReason CheckBinding(Binding const& binding, BindingState const& state)
{
    if (!state.enabled)
        return ReleaseReason::Disabled;
    if (!state.commanderOnline)
        return ReleaseReason::CommanderLeft;
    if (!state.leadOnline)
        return ReleaseReason::LeadLeft;
    if (!state.leadGroupId || state.leadGroupId != binding.groupId)
        return ReleaseReason::GroupChanged;
    if (!state.leadIsLeader)
        return ReleaseReason::LeaderChanged;
    // A bound party is never a raid, even one of five: a conversion ends the binding.
    if (state.leadGroupIsRaidOrBg)
        return ReleaseReason::GroupRaid;
    if (state.realPlayerInGroup)
        return ReleaseReason::PlayerJoined;
    if (state.memberCount > kMaxGroupSize)
        return ReleaseReason::GroupTooLarge;
    return ReleaseReason::None;
}

// Followers the binding pointed at the lead bot that are no longer members of
// the bound group (kicked, invited elsewhere, left): their follow target goes
// back at once, while the rest of the binding stands.
inline std::vector<uint64_t> DepartedFollowers(Binding const& binding,
    std::vector<uint64_t> const& currentMembers)
{
    std::vector<uint64_t> departed;
    for (uint64_t follower : binding.followers)
    {
        bool member = false;
        for (uint64_t guid : currentMembers)
            if (guid == follower)
            {
                member = true;
                break;
            }
        if (!member)
            departed.push_back(follower);
    }
    return departed;
}

inline char const* ReleaseReasonText(ReleaseReason reason)
{
    switch (reason)
    {
        case ReleaseReason::None: return "bound";
        case ReleaseReason::Disabled: return "the remote commander is switched off";
        case ReleaseReason::CommanderLeft: return "the commander logged out";
        case ReleaseReason::LeadLeft: return "the lead bot left the world";
        case ReleaseReason::GroupChanged: return "the group was disbanded or the lead bot left it";
        case ReleaseReason::LeaderChanged: return "the lead bot no longer leads the group";
        case ReleaseReason::PlayerJoined: return "a player joined the group";
        case ReleaseReason::GroupTooLarge: return "the group grew beyond five";
        case ReleaseReason::GroupRaid: return "the group became a raid";
        case ReleaseReason::Unbound: return "unbound by the commander";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Bind gate
// ---------------------------------------------------------------------------

// One bot the bind would put (or keep) in the group.
struct BindMember
{
    bool controllable = false; // CanControlBot: owner, master, claim or GM
    bool liveBot = false;      // live module-owned Headless bot with an AI
    bool hired = false;        // hired companion: bound to its hirer's party
    bool inOtherGroup = false; // in a group that is neither the lead's nor the commander's
    bool inOtherBinding = false; // lead or follower of another commander's binding
};

struct BindFacts
{
    bool enabled = false;
    bool commanderIsBot = false;
    bool commanderAlreadyBound = false;
    BindMember lead;
    // The lead bot's current group (when it has one).
    bool leadGrouped = false;
    bool leadGroupIsRaidOrBg = false;
    bool leadGroupHasRealPlayer = false; // a human other than the commander
    bool leadGroupBound = false;         // another commander holds it
    // Every bot the group would contain besides the lead: current members of
    // the lead's group plus the named bots not yet in it.
    std::vector<BindMember> members;
};

enum class BindRefusal : uint8_t
{
    None,
    Disabled,
    CommanderIsBot,
    AlreadyBound,
    LeadNotControllable,
    LeadHired,
    LeadGroupRaid,
    GroupHasPlayer,
    GroupBound,
    NoMembers,
    GroupTooLarge,
    MemberNotControllable,
    MemberHired,
    MemberInOtherGroup,
    BotInOtherBinding,
};

// The bind is refused (fail closed) unless the commander controls every bot,
// no bot is a hired companion (its hire ends when it leaves its hirer's
// party), the result is a bot-only party of two to five, and nobody else
// commands it.
inline BindRefusal CheckBind(BindFacts const& facts)
{
    if (!facts.enabled)
        return BindRefusal::Disabled;
    if (facts.commanderIsBot)
        return BindRefusal::CommanderIsBot;
    if (facts.commanderAlreadyBound)
        return BindRefusal::AlreadyBound;
    if (!facts.lead.controllable || !facts.lead.liveBot)
        return BindRefusal::LeadNotControllable;
    if (facts.lead.hired)
        return BindRefusal::LeadHired;
    if (facts.lead.inOtherBinding)
        return BindRefusal::BotInOtherBinding;
    if (facts.lead.inOtherGroup)
        return BindRefusal::GroupHasPlayer;
    if (facts.leadGrouped)
    {
        if (facts.leadGroupIsRaidOrBg)
            return BindRefusal::LeadGroupRaid;
        if (facts.leadGroupHasRealPlayer)
            return BindRefusal::GroupHasPlayer;
        if (facts.leadGroupBound)
            return BindRefusal::GroupBound;
    }
    if (facts.members.empty())
        return BindRefusal::NoMembers;
    if (facts.members.size() + 1 > kMaxGroupSize)
        return BindRefusal::GroupTooLarge;
    for (BindMember const& member : facts.members)
    {
        if (!member.controllable || !member.liveBot)
            return BindRefusal::MemberNotControllable;
        if (member.hired)
            return BindRefusal::MemberHired;
        if (member.inOtherGroup)
            return BindRefusal::MemberInOtherGroup;
        if (member.inOtherBinding)
            return BindRefusal::BotInOtherBinding;
    }
    return BindRefusal::None;
}

inline char const* BindRefusalCode(BindRefusal refusal)
{
    switch (refusal)
    {
        case BindRefusal::None: return "ok";
        case BindRefusal::Disabled: return "disabled";
        case BindRefusal::CommanderIsBot: return "denied";
        case BindRefusal::AlreadyBound: return "already-bound";
        case BindRefusal::LeadNotControllable: return "no-bot";
        case BindRefusal::LeadHired: return "hired";
        case BindRefusal::LeadGroupRaid: return "group-raid";
        case BindRefusal::GroupHasPlayer: return "group-has-player";
        case BindRefusal::GroupBound: return "group-bound";
        case BindRefusal::NoMembers: return "no-members";
        case BindRefusal::GroupTooLarge: return "group-too-large";
        case BindRefusal::MemberNotControllable: return "no-bot";
        case BindRefusal::MemberHired: return "hired";
        case BindRefusal::MemberInOtherGroup: return "member-grouped";
        case BindRefusal::BotInOtherBinding: return "bot-bound";
    }
    return "rejected";
}

inline char const* BindRefusalText(BindRefusal refusal)
{
    switch (refusal)
    {
        case BindRefusal::None: return "ok";
        case BindRefusal::Disabled: return "The remote commander is off (AiPlayerbot.RemoteCommander = 0).";
        case BindRefusal::CommanderIsBot: return "A bot cannot be a commander.";
        case BindRefusal::AlreadyBound: return "You already command a group. Use .bot commander unbind first.";
        case BindRefusal::LeadNotControllable: return "The lead bot is not an online bot you control.";
        case BindRefusal::LeadHired: return "A hired companion cannot lead a commanded group (its hire ends when it leaves your party).";
        case BindRefusal::LeadGroupRaid: return "The lead bot is in a raid or battleground group.";
        case BindRefusal::GroupHasPlayer: return "The lead bot's group has a player in it. Commanded groups are bots only.";
        case BindRefusal::GroupBound: return "Another commander already commands that group.";
        case BindRefusal::NoMembers: return "Name the bots to group with the lead bot: .bot commander bind <LeadBot> <Bot> [<Bot>...].";
        case BindRefusal::GroupTooLarge: return "A commanded group holds at most five bots.";
        case BindRefusal::MemberNotControllable: return "A named or grouped bot is not an online bot you control.";
        case BindRefusal::MemberHired: return "Hired companions cannot join a commanded group (their hire ends when they leave your party).";
        case BindRefusal::MemberInOtherGroup: return "A named bot is in another group. Remove it from that group first.";
        case BindRefusal::BotInOtherBinding: return "A named bot is still in another commander's group. That commander must unbind first.";
    }
    return "Request rejected.";
}

// ---------------------------------------------------------------------------
// Bind plan: keep the party or re-form it
// ---------------------------------------------------------------------------

// What the bind does with the party the lead bot is in. An instance bind lives
// on the Group (Group::m_boundInstances); a new group has none until somebody
// enters the map, so a bot that is re-grouped inside a dungeon becomes
// invalid and is sent to its homebind about 60 s later (Group::
// _homebindIfInstance, Player::UpdateHomebindTime). The core keeps a group's
// non-permanent binds across a leader change (Group::_setLeader drops only the
// permanent ones, and a five-man bind is not permanent) and across a member
// leaving (RemoveMember only starts the homebind timer for the member who
// left). So the commander's own party is kept, with the lead bot made leader
// and the commander taken out, instead of being rebuilt.
enum class BindGroupPlan : uint8_t
{
    // No group yet: the lead bot invites the named bots (a new, unbound group).
    FormNew,
    // The lead bot is in a group of its own: adopted as it is.
    AdoptLeadGroup,
    // The lead bot is in the commander's party and nobody but bots and the
    // commander is in it: the lead bot becomes leader, the commander leaves,
    // the group (and its instance bind) stays.
    KeepCommanderParty,
    // The lead bot is in the commander's party, but it is a raid or battleground
    // group or holds another player: the old behaviour (the bots leave it and
    // form a new group).
    ReformFromCommanderParty,
};

struct BindPlanFacts
{
    bool leadInCommanderParty = false;  // the lead bot shares the commander's party
    bool leadInOwnGroup = false;        // the lead bot is in some other group
    bool partyIsRaidOrBg = false;       // the commander's party (when shared)
    bool partyHasOtherPlayer = false;   // a human other than the commander (when shared)
};

inline BindGroupPlan DecideBindGroup(BindPlanFacts const& facts)
{
    if (facts.leadInCommanderParty)
        return (facts.partyIsRaidOrBg || facts.partyHasOtherPlayer)
            ? BindGroupPlan::ReformFromCommanderParty
            : BindGroupPlan::KeepCommanderParty;
    if (facts.leadInOwnGroup)
        return BindGroupPlan::AdoptLeadGroup;
    return BindGroupPlan::FormNew;
}

// The core disbands a party that would be left with fewer than two members
// (Group::RemoveMember with GetMembersCount() <= 2), so the commander may leave
// a kept party only while it holds the commander and at least two bots.
inline bool CanDropCommanderFromParty(size_t partyMembers)
{
    return partyMembers >= 3;
}

// While a bot is in a group a commander commands, its master leaving the party
// is not a reason to leave: the commander is meant to be out of the party, and
// the bind's instance would be lost with the group. Every other bot keeps the
// general rule (a bot leaves when its master does).
inline bool LeavesWhenMasterLeaves(bool inCommandedGroup)
{
    return !inCommandedGroup;
}

// ---------------------------------------------------------------------------
// Order auth gate
// ---------------------------------------------------------------------------

// Same tiers as the whisper gate (PlayerbotAI::AuthTierForCommand, issue
// #294): tactical orders are what any group member may give, and the bound
// commander counts as a group member of its group. Revive and summon are
// owner-or-GM there, and stay so here.
enum class OrderTier : uint8_t
{
    Tactical,
    OwnerOrGm,
};

inline OrderTier TierForOrder(std::string const& verb)
{
    // lootmethod changes the bound group's rule, like any group member's order
    // to its leader would; the per-bot loot/open/use orders move items and open
    // things for one bot, so they stay with its owner (or a GM).
    if (verb == "attack" || verb == "pull" || verb == "pullback" || verb == "cc" ||
        verb == "focus" || verb == "move" || verb == "stay" || verb == "follow" ||
        verb == "rest" || verb == "lootmethod")
        return OrderTier::Tactical;
    // revive, summon, loot, open, use and anything unknown: fail closed.
    return OrderTier::OwnerOrGm;
}

inline bool IsKnownOrder(std::string const& verb)
{
    return TierForOrder(verb) == OrderTier::Tactical || verb == "revive" || verb == "summon" ||
        verb == "loot" || verb == "open" || verb == "use";
}

// enabled: AiPlayerbot.RemoteCommander. boundCommander: the requester holds a
// binding that still stands. controlsTarget: CanControlBot for the order's
// target bot (only asked for owner-or-GM orders).
inline bool MayIssueOrder(bool enabled, bool boundCommander, OrderTier tier, bool controlsTarget)
{
    if (!enabled || !boundCommander)
        return false;
    if (tier == OrderTier::Tactical)
        return true;
    return controlsTarget;
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

// A raw 64-bit object GUID: "0x" + 1-16 hex digits (the form SuperWoW and the
// server logs print), or a decimal number. Zero and anything else is refused.
inline bool ParseGuid(std::string const& token, uint64_t& out)
{
    out = 0;
    if (token.empty() || token.size() > 20)
        return false;
    size_t start = 0;
    bool hex = false;
    if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))
    {
        start = 2;
        hex = true;
        if (token.size() - start > 16)
            return false;
    }
    uint64_t value = 0;
    for (size_t i = start; i < token.size(); ++i)
    {
        char c = token[i];
        uint64_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint64_t>(c - '0');
        else if (hex && c >= 'a' && c <= 'f')
            digit = static_cast<uint64_t>(c - 'a' + 10);
        else if (hex && c >= 'A' && c <= 'F')
            digit = static_cast<uint64_t>(c - 'A' + 10);
        else
            return false;
        if (hex)
            value = (value << 4) | digit;
        else
        {
            if (value > (UINT64_MAX - digit) / 10)
                return false;
            value = value * 10 + digit;
        }
    }
    if (!value)
        return false;
    out = value;
    return true;
}

struct MovePoint
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Splits on spaces, tabs, commas and semicolons ("x y z", "x,y,z", "x;y;z").
inline std::vector<std::string> SplitOrderArgs(std::string const& text)
{
    std::vector<std::string> parts;
    std::string current;
    for (char c : text)
    {
        if (c == ' ' || c == '\t' || c == ',' || c == ';')
        {
            if (!current.empty())
                parts.push_back(current);
            current.clear();
        }
        else
            current += c;
    }
    if (!current.empty())
        parts.push_back(current);
    return parts;
}

inline bool ParseCoordinate(std::string const& token, float& out)
{
    if (token.empty() || token.size() > 32)
        return false;
    char* end = nullptr;
    float value = std::strtof(token.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(value) || std::fabs(value) > kMaxMapCoordinate)
        return false;
    out = value;
    return true;
}

// Exactly three finite map coordinates.
inline bool ParseMovePoint(std::string const& text, MovePoint& out)
{
    std::vector<std::string> parts = SplitOrderArgs(text);
    if (parts.size() != 3)
        return false;
    MovePoint point;
    if (!ParseCoordinate(parts[0], point.x) || !ParseCoordinate(parts[1], point.y) ||
        !ParseCoordinate(parts[2], point.z))
        return false;
    out = point;
    return true;
}

// ---------------------------------------------------------------------------
// Per-bot player orders (loot, open, use) and the loot method: arguments
// ---------------------------------------------------------------------------

// An item entry: decimal, 1 to 999999.
inline bool ParseItemId(std::string const& token, uint32_t& out)
{
    out = 0;
    if (token.empty() || token.size() > 6)
        return false;
    uint32_t value = 0;
    for (char c : token)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    if (!value)
        return false;
    out = value;
    return true;
}

// cmd loot <corpse-or-go-guid> <bot> [itemId]
struct LootOrder
{
    uint64_t guid = 0;
    std::string bot;
    uint32_t itemId = 0; // 0 = everything
};

inline bool ParseLootOrder(std::vector<std::string> const& parts, LootOrder& out)
{
    if (parts.size() < 2 || parts.size() > 3)
        return false;
    LootOrder order;
    if (!ParseGuid(parts[0], order.guid))
        return false;
    order.bot = parts[1];
    if (parts.size() == 3 && !ParseItemId(parts[2], order.itemId))
        return false;
    out = order;
    return true;
}

// cmd open <go-guid> <bot>
struct OpenOrder
{
    uint64_t guid = 0;
    std::string bot;
};

inline bool ParseOpenOrder(std::vector<std::string> const& parts, OpenOrder& out)
{
    if (parts.size() != 2)
        return false;
    OpenOrder order;
    if (!ParseGuid(parts[0], order.guid))
        return false;
    order.bot = parts[1];
    out = order;
    return true;
}

// cmd use <itemId> <bot> [target-guid]
struct UseOrder
{
    uint32_t itemId = 0;
    std::string bot;
    uint64_t target = 0; // 0 = no target (the item on its own)
};

inline bool ParseUseOrder(std::vector<std::string> const& parts, UseOrder& out)
{
    if (parts.size() < 2 || parts.size() > 3)
        return false;
    UseOrder order;
    if (!ParseItemId(parts[0], order.itemId))
        return false;
    order.bot = parts[1];
    if (parts.size() == 3 && !ParseGuid(parts[2], order.target))
        return false;
    out = order;
    return true;
}

// The bot an owner-or-GM order names, or an empty string when the arguments
// do not fit the verb (revive/summon take the bot alone; loot/open/use name it
// second, after the object or item).
inline std::string OwnerOrderBotName(std::string const& verb, std::vector<std::string> const& parts)
{
    if ((verb == "revive" || verb == "summon") && parts.size() == 1)
        return parts[0];
    if ((verb == "loot" || verb == "open" || verb == "use") && parts.size() >= 2)
        return parts[1];
    return std::string();
}

// ---------------------------------------------------------------------------
// Target and move verdicts
// ---------------------------------------------------------------------------

struct TargetFacts
{
    bool found = false;      // the GUID resolves to a unit on the lead bot's map/instance
    bool inWorld = false;
    bool groupMemberOrBot = false; // a module bot or a member of the bound group
    bool alive = false;
    bool friendly = false;   // friendly to the lead bot
    bool attackable = false; // the lead bot may attack it
    bool visible = false;    // a live bot of the group sees or detects it (stealth, GM invisibility) and it is targetable
};

enum class TargetVerdict : uint8_t
{
    Ok,
    NotFound,
    NotAnEnemy,
    Dead,
    Friendly,
    NotAttackable,
    NotVisible,
};

// Same map and instance (the lookup runs on the lead bot's map), in world,
// never a bot, alive, hostile, attackable and seen by the group.
inline TargetVerdict CheckOrderTarget(TargetFacts const& facts)
{
    if (!facts.found || !facts.inWorld)
        return TargetVerdict::NotFound;
    if (facts.groupMemberOrBot)
        return TargetVerdict::NotAnEnemy;
    if (!facts.alive)
        return TargetVerdict::Dead;
    if (facts.friendly)
        return TargetVerdict::Friendly;
    if (!facts.attackable)
        return TargetVerdict::NotAttackable;
    // A GUID is no way to see what the group cannot: a stealthed rogue, an
    // invisible GM, an untargetable unit.
    if (!facts.visible)
        return TargetVerdict::NotVisible;
    return TargetVerdict::Ok;
}

inline char const* TargetVerdictCode(TargetVerdict verdict)
{
    switch (verdict)
    {
        case TargetVerdict::Ok: return "ok";
        case TargetVerdict::NotFound: return "invalid-target";
        case TargetVerdict::NotAnEnemy: return "invalid-target";
        case TargetVerdict::Dead: return "dead-target";
        case TargetVerdict::Friendly: return "friendly";
        case TargetVerdict::NotAttackable: return "invalid-target";
        case TargetVerdict::NotVisible: return "no-target";
    }
    return "invalid-target";
}

inline char const* TargetVerdictText(TargetVerdict verdict)
{
    switch (verdict)
    {
        case TargetVerdict::Ok: return "ok";
        case TargetVerdict::NotFound: return "No unit with that GUID is on the group's map and instance.";
        case TargetVerdict::NotAnEnemy: return "That GUID is a bot or a member of the group, not an enemy.";
        case TargetVerdict::Dead: return "The target is dead.";
        case TargetVerdict::Friendly: return "The target is friendly to the group.";
        case TargetVerdict::NotAttackable: return "The group cannot attack that target.";
        case TargetVerdict::NotVisible: return "No bot of the group can see that target.";
    }
    return "Invalid target.";
}

// PathFinder::getPathType bits (core PathType), copied as plain numbers so the
// policy stays standalone (the same values as TravelRepickPolicy.h).
constexpr uint32_t kPathNoPath = 0x0008;
constexpr uint32_t kPathIncomplete = 0x0004;

struct MoveFacts
{
    float distanceFromLead = 0.0f; // 3D, lead bot to the point
    float maxDistance = 0.0f;      // AiPlayerbot.ReactDistance (a farther stay spot is dropped)
    uint32_t pathType = 0;         // PathFinder::getPathType()
    float endOffset = 0.0f;        // the path's actual end to the point
};

enum class MoveVerdict : uint8_t
{
    Ok,
    TooFar,
    NoPath,
    OffMesh,
};

// A point the lead bot can walk to: within ReactDistance (the stay strategy
// drops a stay spot farther than that, ReturnToStayPositionAction), with a
// navmesh path. An incomplete path is the path finder's length cap on a long
// route and is walked in legs; a complete path that ends far from the point
// means the point is not on walkable ground.
inline MoveVerdict CheckMove(MoveFacts const& facts)
{
    if (facts.maxDistance <= 0.0f || facts.distanceFromLead > facts.maxDistance)
        return MoveVerdict::TooFar;
    if (facts.pathType & kPathNoPath)
        return MoveVerdict::NoPath;
    if (!(facts.pathType & kPathIncomplete) && facts.endOffset > kMaxMoveEndOffset)
        return MoveVerdict::OffMesh;
    return MoveVerdict::Ok;
}

inline char const* MoveVerdictCode(MoveVerdict verdict)
{
    switch (verdict)
    {
        case MoveVerdict::Ok: return "ok";
        case MoveVerdict::TooFar: return "too-far";
        case MoveVerdict::NoPath: return "unreachable";
        case MoveVerdict::OffMesh: return "off-mesh";
    }
    return "invalid";
}

} // namespace RemoteCommander
} // namespace TortoiseBots
