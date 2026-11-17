#pragma once

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

// Pure rules for the loot catch-up stream (`.bot lootlog`, TBM:LOOT_*). Kept
// free of core headers so the rule is unit-testable
// (tools/test_loot_history_policy.cpp).
//
// A row is one bot_events.csv line for StoreLootAction/LootMoney; the writer
// lays it out as timestamp,bot,event,position,race,class,level then the info
// fields, each wrapped in quotes with no escaping. The stream only ever looks
// at the last 24 h of pieces and answers at most 40 rows per reply; a cursor
// that points at a pruned piece answers `expired`.

namespace ai
{

inline namespace LootHistoryPolicy
{

constexpr long kLootHistoryWindowSeconds = 24 * 3600;
constexpr unsigned kLootHistoryMaxRows = 40;
constexpr size_t kLootHistoryMaxWireBytes = 250;

struct LootHistoryRow
{
    std::string timestamp;
    std::string bot;
    std::string event;
    std::string info1;
    std::string info2;
    std::string info3;
    std::string info4;
    std::string info5;
};

// Splits one CSV line on commas, honouring the writer's quotes. Names are
// stored verbatim (no escaping ever written), so a quote inside a field is
// read literally and never terminates it early.
inline std::vector<std::string> SplitLootCsv(std::string const& line)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];
        if (c == '"')
        {
            quoted = !quoted;
            continue;
        }
        if (c == ',' && !quoted)
        {
            fields.push_back(field);
            field.clear();
            continue;
        }
        field += c;
    }
    fields.push_back(field);
    return fields;
}

// True when the line is a loot row the stream can serve (StoreLootAction,
// LootMoney, or LootRoll with at least the base nine columns). Older short
// events and malformed lines are skipped by the reader.
inline bool ParseLootHistoryRow(std::string const& line, LootHistoryRow& row)
{
    std::vector<std::string> fields = SplitLootCsv(line);
    if (fields.size() < 9)
        return false;
    if (fields[2] != "StoreLootAction" && fields[2] != "LootMoney" && fields[2] != "LootRoll")
        return false;
    row.timestamp = fields[0];
    row.bot = fields[1];
    row.event = fields[2];
    row.info1 = fields[7];
    row.info2 = fields[8];
    row.info3 = fields.size() > 9 ? fields[9] : "";
    row.info4 = fields.size() > 10 ? fields[10] : "";
    row.info5 = fields.size() > 11 ? fields[11] : "";
    return true;
}

// "YYYY-MM-DD HH:MM:SS[+00]" in the server's local time, as GetTimestampStr
// writes it. Out-of-range or malformed stamps read as 0 so the window check
// below treats them as too old rather than crashing on them.
inline time_t ParseLootTimestamp(std::string const& stamp)
{
    if (stamp.size() < 19)
        return 0;
    struct std::tm when = {};
    when.tm_year = std::atoi(stamp.substr(0, 4).c_str()) - 1900;
    when.tm_mon = std::atoi(stamp.substr(5, 2).c_str()) - 1;
    when.tm_mday = std::atoi(stamp.substr(8, 2).c_str());
    when.tm_hour = std::atoi(stamp.substr(11, 2).c_str());
    when.tm_min = std::atoi(stamp.substr(14, 2).c_str());
    when.tm_sec = std::atoi(stamp.substr(17, 2).c_str());
    when.tm_isdst = -1;
    if (when.tm_mon < 0 || when.tm_mon > 11 || when.tm_mday < 1 || when.tm_mday > 31 ||
        when.tm_hour < 0 || when.tm_hour > 23 || when.tm_min < 0 || when.tm_min > 59 ||
        when.tm_sec < 0 || when.tm_sec > 60)
        return 0;
    return std::mktime(&when);
}

// True while the row stamp is inside the 24 h catch-up window. Stamps a
// little in the future (clock skew between writer and reader) still count.
inline bool WithinLootWindow(std::string const& stamp, time_t now)
{
    time_t when = ParseLootTimestamp(stamp);
    if (when == 0)
        return false;
    return now - when <= kLootHistoryWindowSeconds;
}

// Server-opaque resume position: which packed piece to continue in plus the
// uncompressed byte offset where the next row starts. The live file is never
// addressable (it is renamed under the reader); a cursor naming a piece that
// no longer exists answers `expired`.
struct LootHistoryCursor
{
    std::string piece;
    uint64_t offset = 0;
};

inline std::string EncodeLootCursor(LootHistoryCursor const& cursor)
{
    if (cursor.piece.empty())
        return "";
    return cursor.piece + "@" + std::to_string(cursor.offset);
}

inline bool DecodeLootCursor(std::string const& text, LootHistoryCursor& cursor)
{
    if (text.empty())
    {
        cursor = LootHistoryCursor();
        return true;
    }
    size_t at = text.rfind('@');
    if (at == std::string::npos || at == 0 || at + 1 >= text.size())
        return false;
    std::string offset = text.substr(at + 1);
    for (char c : offset)
        if (c < '0' || c > '9')
            return false;
    try
    {
        cursor.offset = static_cast<uint64_t>(std::stoull(offset));
    }
    catch (...)
    {
        return false;
    }
    cursor.piece = text.substr(0, at);
    return true;
}

// Shortens the two display names until the wire line fits the 250-byte
// addon budget. The row is never dropped for length; ids, event and source
// type stay intact so the addon can still file it.
inline void FitLootWireLine(std::string& item, std::string& source, size_t fixedBytes)
{
    while (!item.empty() && !source.empty() && fixedBytes + item.size() + source.size() > kLootHistoryMaxWireBytes)
    {
        if (item.size() >= source.size())
            item.resize(item.size() - 1);
        else
            source.resize(source.size() - 1);
    }
    while (!item.empty() && fixedBytes + item.size() + source.size() > kLootHistoryMaxWireBytes)
        item.resize(item.size() - 1);
    while (!source.empty() && fixedBytes + item.size() + source.size() > kLootHistoryMaxWireBytes)
        source.resize(source.size() - 1);
}

// Shortens a LootRoll's trailing reason in <vote>:<itemId>:<reason> (keeping
// the vote and itemId intact) and, only if still needed, the item name so the
// wire line fits the 250-byte addon budget without dropping the item name.
inline void FitLootRollWireLine(std::string& info2, std::string& item, size_t fixedBytes)
{
    size_t firstColon = info2.find(':');
    size_t secondColon = (firstColon != std::string::npos) ? info2.find(':', firstColon + 1) : std::string::npos;
    size_t minPrefix = (secondColon != std::string::npos) ? secondColon + 1 : info2.size();
    while (info2.size() > minPrefix && fixedBytes + info2.size() + item.size() > kLootHistoryMaxWireBytes)
        info2.resize(info2.size() - 1);
    while (item.size() > 1 && fixedBytes + info2.size() + item.size() > kLootHistoryMaxWireBytes)
        item.resize(item.size() - 1);
}

} // namespace LootHistoryPolicy
} // namespace ai
