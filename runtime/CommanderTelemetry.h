#pragma once

// Remote commander telemetry (issue #884 follow-up): what the observability
// dashboard's Commanders tab shows. A COMMANDERS datagram lists each standing
// binding (commander, lead bot, the bound bots with their vitals and
// position, and the commander's last orders with the module's answer).
//
// Map data (additive fields, old daemons ignore them): the commander's own
// position when online, each bound bot's target, and the hostile units near
// the lead (the list the commander's TBM:VIEW feed builds, capped), so the
// dashboard map can draw mobs, threat lines and aggro rings.
//
// Unit info (additive, same rule): each bot's activity (state letter, newest
// action, spell being cast, crowd control, drinking/eating, moving, target
// name), each unit's level, rank (elite/rare/boss), spell being cast and
// crowd control, and when the binding began, so the dashboard can say what
// every character is doing and answer a hover over any unit.
//
// Cost rule: nothing here runs per bot tick. The emitter asks for the
// bindings once per 2 s snapshot cycle, and only while at least one binding
// exists; an order is noted when its reply is sent (a few per minute). The
// order log is bounded per commander and in the number of commanders.
//
// Standard library only (Player is a forward declaration), so
// tools/test_commander_telemetry.cpp compiles it without a game header.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

namespace TortoiseBots {
namespace CommanderTelemetry {

constexpr size_t kMaxOrdersPerCommander = 8;
constexpr size_t kMaxTrackedCommanders = 16;
constexpr size_t kMaxBindings = 8;     // per payload: keeps one datagram well under the loopback MTU
constexpr size_t kMaxBotsPerBinding = 6;
constexpr size_t kMaxMobsPerBinding = 24; // the perception feed's own cap (kMaxHostiles)
constexpr size_t kMaxMobsPerPayload = 64; // all bindings together: the datagram stays well under 64 KiB
constexpr size_t kMaxMobName = 24;
constexpr size_t kMaxCastName = 24;     // a spell name
constexpr size_t kMaxActionName = 32;   // the bot's newest action name
constexpr size_t kMaxTargetName = 24;
constexpr size_t kMaxRunWord = 32;      // plan, state, leg, kind and phase names of a run
constexpr size_t kMaxRunNote = 120;     // what the run does now, or why it paused
constexpr size_t kMaxOrderDetail = 80;
constexpr size_t kMaxOrderResult = 120;

// One order of a commander and the module's answer to it.
struct OrderRecord
{
    int64_t at = 0;       // unix seconds
    std::string verb;     // "move", "attack", ... (the intent without "cmd ")
    std::string detail;   // the order's arguments as typed (clipped)
    bool ok = false;      // TBM:ACTION_ACK (true) or TBM:ACTION_ERR (false)
    std::string result;   // ack: the scope; err: "<code>: <message>" (clipped)
};

// Crowd control as one letter, the first that holds: s stunned, c confused (polymorph, sleep and the like), f fleeing
// (feared or running), r rooted; 0 = free.
inline char CcKind(bool stunned, bool confused, bool fleeing, bool rooted)
{
    return stunned ? 's' : confused ? 'c' : fleeing ? 'f' : rooted ? 'r' : 0;
}

// A creature's rank as one letter: b boss (world boss rank or one that binds the instance), e elite, r rare (or rare
// elite); 0 = an ordinary unit.
inline char RankKind(uint32_t rank, bool bindsInstance)
{
    // rank: 0 normal, 1 elite, 2 rare elite, 3 world boss, 4 rare
    if (rank == 3 || bindsInstance)
        return 'b';
    if (rank == 1)
        return 'e';
    if (rank == 2 || rank == 4)
        return 'r';
    return 0;
}

// A UTF-8-safe clip: never cuts inside a multi-byte sequence.
inline std::string Clip(std::string const& s, size_t max)
{
    if (s.size() <= max)
        return s;
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    return s.substr(0, cut);
}

// Bounded log of the commanders' last orders. World thread only.
class OrderLog
{
public:
    void Note(uint64_t commander, OrderRecord rec)
    {
        if (!commander)
            return;
        rec.detail = Clip(rec.detail, kMaxOrderDetail);
        rec.result = Clip(rec.result, kMaxOrderResult);
        auto it = m_orders.find(commander);
        if (it == m_orders.end())
        {
            if (m_orders.size() >= kMaxTrackedCommanders)
            {
                // Drop the commander noted longest ago.
                uint64_t const oldest = m_arrival.front();
                m_arrival.pop_front();
                m_orders.erase(oldest);
            }
            it = m_orders.emplace(commander, std::deque<OrderRecord>()).first;
            m_arrival.push_back(commander);
        }
        it->second.push_back(std::move(rec));
        while (it->second.size() > kMaxOrdersPerCommander)
            it->second.pop_front();
    }

    // Oldest first.
    std::vector<OrderRecord> Recent(uint64_t commander) const
    {
        auto it = m_orders.find(commander);
        if (it == m_orders.end())
            return {};
        return std::vector<OrderRecord>(it->second.begin(), it->second.end());
    }

    // Forget every commander that is not in keep (a released binding).
    void Retain(std::unordered_set<uint64_t> const& keep)
    {
        for (auto it = m_orders.begin(); it != m_orders.end();)
            it = keep.count(it->first) ? std::next(it) : m_orders.erase(it);
        for (auto it = m_arrival.begin(); it != m_arrival.end();)
            it = m_orders.count(*it) ? std::next(it) : m_arrival.erase(it);
    }

    void Clear()
    {
        m_orders.clear();
        m_arrival.clear();
    }

    size_t Commanders() const { return m_orders.size(); }

private:
    std::unordered_map<uint64_t, std::deque<OrderRecord>> m_orders;
    std::deque<uint64_t> m_arrival; // commanders in the order they first noted an order
};

// One hostile-ish unit near the lead, as the dashboard shows it.
struct MobInfo
{
    uint64_t id = 0;       // raw guid (a decimal string on the wire: 64 bits do not fit a JSON number)
    uint32_t entry = 0;
    std::string name;
    uint32_t hpPct = 0;
    float x = 0.0f;
    float y = 0.0f;
    uint64_t victim = 0;   // who the unit targets (raw guid), 0 = nobody
    char reaction = 'h';   // h hostile, n neutral, f friendly (to the bound bots)
    char kind = 0;         // 0 ordinary, 'k' critter, 'v' civilian, 'g' guard, 'u' unattackable
    uint32_t level = 0;    // 0 = unknown
    char rank = 0;         // RankKind: 0 ordinary, 'e' elite, 'r' rare, 'b' boss
    char cc = 0;           // CcKind: 0 free, 's' stunned, 'c' confused, 'f' fleeing, 'r' rooted
    std::string cast;      // name of the spell it is casting, empty = none
};

// The run leader's run of a binding (`.bot run`, behind AiPlayerbot.DungeonSelfClear): where it stands in the dungeon plan. Only a binding
// with a run carries it; the dashboard shows it as the "observe" mode of the dungeon modes (the bots clear, the commander watches).
struct RunInfo
{
    bool present = false;
    std::string plan;       // the plan's name ("uldaman")
    std::string state;      // running, paused or done
    uint32_t step = 0;      // 1-based step shown (0 before the first)
    uint32_t steps = 0;     // steps in the plan
    std::string leg;        // the current leg's name (the section)
    std::string kind;       // the step kind (move, pull, boss ...)
    std::string phase;      // what the step is doing (walk, pull, rest ...)
    std::string note;       // what it does now, or why it paused
    int64_t changedAt = 0;  // unix seconds the step, phase, state or note last changed
};

// What a bound bot is doing, read next to the view snapshot (same cadence, no extra scan).
struct BotExtra
{
    uint64_t id = 0;        // the bot's raw guid
    char state = 'n';       // c combat, n non-combat, d dead, r reaction (the AI's state)
    std::string action;     // the engine's newest action verdict name
    char result = '-';      // O ok, F failed, I impossible, U useless, P prereq, V vetoed, - none
    std::string cast;       // spell being cast or channelled, empty = none
    char cc = 0;            // CcKind
    char rest = 0;          // 'd' drinking, 'e' eating, 0 neither
    bool moving = false;
    std::string targetName; // name of its target, empty = none or unknown
};

// A standing binding as the commands layer reports it: live objects, read on
// the world thread.
struct BindingRef
{
    std::string commander;      // the commander's name; "offline #<guid>" when not found
    uint32_t commanderGuid = 0; // the commander's character guid (low part), also when offline
    bool commanderOnline = false;
    bool viewOn = false;        // the TBM:VIEW perception feed is on
    uint32_t groupId = 0;
    Player* lead = nullptr;     // may be null while the lead bot is between maps
    std::vector<Player*> bots;  // lead bot first
    std::vector<OrderRecord> orders;
    // Map data, filled by the commands layer (see CollectCommanderTelemetry).
    bool commanderPlaced = false; // commanderMap/X/Y are valid (online and in the world)
    uint32_t commanderMap = 0;
    float commanderX = 0.0f;
    float commanderY = 0.0f;
    bool mobsReported = false;    // the mob list was read (it may be empty)
    uint32_t mobsMap = 0;         // the map the mobs are on (the lead's)
    std::vector<MobInfo> mobs;
    std::vector<BotExtra> botExtras; // one per bot read from the view snapshot, matched by id
    int64_t boundAt = 0;             // unix seconds the binding was first seen standing, 0 = unknown
    RunInfo run;                     // the run leader's run of this binding, when it has one
};

// One bound bot as the dashboard shows it.
struct BotInfo
{
    std::string name;
    uint32_t guid = 0;
    std::string className;
    std::string role;
    uint32_t level = 0;
    uint32_t hp = 0;
    uint32_t maxHp = 0;
    uint32_t power = 0;
    uint32_t maxPower = 0;
    std::string powerType;
    bool alive = true;
    bool lead = false;
    uint32_t mapId = 0;
    uint32_t zoneId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint64_t id = 0;     // raw guid, 0 = not reported
    uint64_t target = 0; // raw guid of its target, 0 = none
    bool hasExtra = false; // the fields below were read
    BotExtra extra;
};

struct BindingInfo
{
    std::string commander;
    uint32_t commanderGuid = 0; // the dashboard opens this character's detail view from it
    bool commanderOnline = false;
    bool viewOn = false;
    uint32_t groupId = 0;
    std::string lead;
    std::vector<BotInfo> bots;
    std::vector<OrderRecord> orders;
    bool commanderPlaced = false;
    uint32_t commanderMap = 0;
    float commanderX = 0.0f;
    float commanderY = 0.0f;
    bool mobsReported = false;
    uint32_t mobsMap = 0;
    std::vector<MobInfo> mobs;
    int64_t boundAt = 0;
    RunInfo run;
};

inline std::string JsonEscape(std::string const& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
                out += static_cast<char>(c);
        }
    }
    return out;
}

// The COMMANDERS datagram. An empty list is a valid payload: it tells the
// daemon the last binding ended.
inline std::string BuildPayload(int protocolVersion, uint64_t session, uint64_t seq, int64_t ts,
                                std::vector<BindingInfo> const& bindings)
{
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << "{\"v\":" << protocolVersion
       << ",\"session\":" << session
       << ",\"seq\":" << seq
       << ",\"ts\":" << ts
       << ",\"type\":\"COMMANDERS\",\"commanders\":[";
    size_t shown = 0;
    size_t mobsTotal = 0;
    for (BindingInfo const& b : bindings)
    {
        if (shown >= kMaxBindings)
            break;
        if (shown++)
            ss << ",";
        ss << "{\"commander\":\"" << JsonEscape(b.commander) << "\""
           << ",\"commander_guid\":" << b.commanderGuid
           << (b.boundAt > 0 ? ",\"bound_at\":" + std::to_string(b.boundAt) : std::string())
           << ",\"online\":" << (b.commanderOnline ? "true" : "false")
           << ",\"view\":" << (b.viewOn ? "true" : "false")
           << ",\"group\":" << b.groupId
           << ",\"lead\":\"" << JsonEscape(b.lead) << "\",\"bots\":[";
        size_t bots = 0;
        for (BotInfo const& bot : b.bots)
        {
            if (bots >= kMaxBotsPerBinding)
                break;
            if (bots++)
                ss << ",";
            ss << "{\"name\":\"" << JsonEscape(bot.name) << "\""
               << ",\"guid\":" << bot.guid
               << ",\"class\":\"" << JsonEscape(bot.className) << "\""
               << ",\"role\":\"" << JsonEscape(bot.role) << "\""
               << ",\"level\":" << bot.level
               << ",\"hp\":" << bot.hp
               << ",\"max_hp\":" << bot.maxHp
               << ",\"power\":" << bot.power
               << ",\"max_power\":" << bot.maxPower
               << ",\"power_type\":\"" << JsonEscape(bot.powerType) << "\""
               << ",\"alive\":" << (bot.alive ? "true" : "false")
               << ",\"lead\":" << (bot.lead ? "true" : "false")
               << ",\"map\":" << bot.mapId
               << ",\"zone\":" << bot.zoneId
               << ",\"x\":" << bot.x << ",\"y\":" << bot.y << ",\"z\":" << bot.z;
            if (bot.id)
                ss << ",\"id\":\"" << bot.id << "\"";
            if (bot.target)
                ss << ",\"target\":\"" << bot.target << "\"";
            if (bot.hasExtra)
            {
                BotExtra const& x = bot.extra;
                ss << ",\"st\":\"" << (x.state == 'c' || x.state == 'd' || x.state == 'r' ? x.state : 'n') << "\"";
                if (!x.action.empty())
                    ss << ",\"act\":\"" << JsonEscape(Clip(x.action, kMaxActionName)) << "\""
                       << ",\"res\":\"" << (x.result >= 'A' && x.result <= 'Z' ? x.result : '-') << "\"";
                if (!x.cast.empty())
                    ss << ",\"cast\":\"" << JsonEscape(Clip(x.cast, kMaxCastName)) << "\"";
                if (x.cc == 's' || x.cc == 'c' || x.cc == 'f' || x.cc == 'r')
                    ss << ",\"cc\":\"" << x.cc << "\"";
                if (x.rest == 'd' || x.rest == 'e')
                    ss << ",\"rest\":\"" << x.rest << "\"";
                if (x.moving)
                    ss << ",\"mv\":true";
                if (!x.targetName.empty())
                    ss << ",\"tname\":\"" << JsonEscape(Clip(x.targetName, kMaxTargetName)) << "\"";
            }
            ss << "}";
        }
        ss << "]";
        if (b.run.present)
        {
            RunInfo const& r = b.run;
            ss << ",\"run\":{\"plan\":\"" << JsonEscape(Clip(r.plan, kMaxRunWord)) << "\""
               << ",\"state\":\"" << JsonEscape(Clip(r.state, kMaxRunWord)) << "\""
               << ",\"step\":" << r.step << ",\"steps\":" << r.steps
               << ",\"leg\":\"" << JsonEscape(Clip(r.leg, kMaxRunWord)) << "\""
               << ",\"kind\":\"" << JsonEscape(Clip(r.kind, kMaxRunWord)) << "\""
               << ",\"phase\":\"" << JsonEscape(Clip(r.phase, kMaxRunWord)) << "\""
               << ",\"note\":\"" << JsonEscape(Clip(r.note, kMaxRunNote)) << "\""
               << ",\"changed\":" << r.changedAt << "}";
        }
        if (b.commanderPlaced)
            ss << ",\"commander_pos\":{\"map\":" << b.commanderMap << ",\"x\":" << b.commanderX << ",\"y\":" << b.commanderY << "}";
        if (b.mobsReported)
        {
            ss << ",\"mobs_map\":" << b.mobsMap << ",\"mobs\":[";
            size_t mobs = 0;
            for (MobInfo const& m : b.mobs)
            {
                if (mobs >= kMaxMobsPerBinding || mobsTotal >= kMaxMobsPerPayload)
                    break;
                if (mobs++)
                    ss << ",";
                ++mobsTotal;
                ss << "{\"id\":\"" << m.id << "\",\"entry\":" << m.entry
                   << ",\"name\":\"" << JsonEscape(Clip(m.name, kMaxMobName)) << "\""
                   << ",\"hp\":" << m.hpPct
                   << ",\"x\":" << m.x << ",\"y\":" << m.y;
                if (m.victim)
                    ss << ",\"victim\":\"" << m.victim << "\"";
                ss << ",\"reaction\":\"" << (m.reaction == 'n' || m.reaction == 'f' ? m.reaction : 'h') << "\"";
                if (m.kind == 'k' || m.kind == 'v' || m.kind == 'g' || m.kind == 'u')
                    ss << ",\"kind\":\"" << m.kind << "\"";
                if (m.level)
                    ss << ",\"lvl\":" << m.level;
                if (m.rank == 'b' || m.rank == 'e' || m.rank == 'r')
                    ss << ",\"rank\":\"" << m.rank << "\"";
                if (m.cc == 's' || m.cc == 'c' || m.cc == 'f' || m.cc == 'r')
                    ss << ",\"cc\":\"" << m.cc << "\"";
                if (!m.cast.empty())
                    ss << ",\"cast\":\"" << JsonEscape(Clip(m.cast, kMaxCastName)) << "\"";
                ss << "}";
            }
            ss << "]";
        }
        ss << ",\"orders\":[";
        size_t orders = 0;
        for (OrderRecord const& o : b.orders)
        {
            if (orders >= kMaxOrdersPerCommander)
                break;
            if (orders++)
                ss << ",";
            ss << "{\"at\":" << o.at
               << ",\"verb\":\"" << JsonEscape(o.verb) << "\""
               << ",\"detail\":\"" << JsonEscape(o.detail) << "\""
               << ",\"ok\":" << (o.ok ? "true" : "false")
               << ",\"result\":\"" << JsonEscape(o.result) << "\"}";
        }
        ss << "]}";
    }
    ss << "]}";
    return ss.str();
}

} // namespace CommanderTelemetry
} // namespace TortoiseBots
