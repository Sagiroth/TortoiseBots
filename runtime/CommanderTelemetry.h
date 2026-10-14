#pragma once

// Remote commander telemetry (issue #884 follow-up): what the observability
// dashboard's Commanders tab shows. A COMMANDERS datagram lists each standing
// binding (commander, lead bot, the bound bots with their vitals and
// position, and the commander's last orders with the module's answer).
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

// A standing binding as the commands layer reports it: live objects, read on
// the world thread.
struct BindingRef
{
    std::string commander;      // the commander's name; "offline #<guid>" when not found
    bool commanderOnline = false;
    bool viewOn = false;        // the TBM:VIEW perception feed is on
    uint32_t groupId = 0;
    Player* lead = nullptr;     // may be null while the lead bot is between maps
    std::vector<Player*> bots;  // lead bot first
    std::vector<OrderRecord> orders;
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
};

struct BindingInfo
{
    std::string commander;
    bool commanderOnline = false;
    bool viewOn = false;
    uint32_t groupId = 0;
    std::string lead;
    std::vector<BotInfo> bots;
    std::vector<OrderRecord> orders;
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
    for (BindingInfo const& b : bindings)
    {
        if (shown >= kMaxBindings)
            break;
        if (shown++)
            ss << ",";
        ss << "{\"commander\":\"" << JsonEscape(b.commander) << "\""
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
               << ",\"x\":" << bot.x << ",\"y\":" << bot.y << ",\"z\":" << bot.z << "}";
        }
        ss << "],\"orders\":[";
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
