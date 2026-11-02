#include "playerbot/playerbot.h"
#include "BossTacticsMgr.h"

#include <cstdlib>
#include <memory>

using namespace ai;

ReloadableTable<BossTacticTable>& ai::BossTacticsHolder()
{
    static ReloadableTable<BossTacticTable> holder;
    return holder;
}

BossTacticTable& ai::GetBossTactics()
{
    return BossTacticsHolder().Current();
}

// One pass over `ai_boss_tactic` into `table` (empty, not sealed), then the seal; returns the rows skipped. The startup
// load and `.bot run reloadplans` both read through here.
std::uint32_t ai::ReadBossTacticRows(BossTacticTable& table, std::uint64_t* rowsRead)
{
    uint32 skipped = 0;
    // The core's Query hands over a raw QueryResult* the caller frees (src/shared/Database/Database.h).
    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order` "
        "FROM `ai_boss_tactic`"));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            if (rowsRead)
                ++*rowsRead;
            BossTacticRow row;
            row.map = fields[0].GetUInt32();
            row.bossEntry = fields[1].GetUInt32();
            row.kind = ParseBossTacticKind(fields[2].GetCppString());
            row.targetEntry = fields[3].GetUInt32();
            row.spellId = fields[4].GetUInt32();
            row.priority = fields[5].GetUInt8();
            row.param = fields[6].GetFloat();
            // A linked_death group's burn order: comma-separated creature entries, in the order the group dies.
            // Every other kind leaves the column empty (the shipped default).
            if (row.kind == BossTacticKind::LinkedDeath)
            {
                std::string const text = fields[7].GetCppString();
                std::size_t at = 0;
                while (at < text.size())
                {
                    std::size_t const comma = text.find(',', at);
                    std::string const token = text.substr(at, comma == std::string::npos ? std::string::npos
                                                                                        : comma - at);
                    std::size_t first = token.find_first_not_of(" \t");
                    std::size_t const last = token.find_last_not_of(" \t");
                    if (first != std::string::npos)
                    {
                        std::uint32_t const entry = static_cast<std::uint32_t>(std::strtoul(
                            token.substr(first, last - first + 1).c_str(), nullptr, 10));
                        if (entry)
                            row.burnOrder.push_back(entry);
                    }
                    if (comma == std::string::npos)
                        break;
                    at = comma + 1;
                }
            }
            if (!table.Add(row))
            {
                ++skipped;
                sLog.outError("ai_boss_tactic: map %u boss %u kind '%s' target %u spell %u skipped (unknown kind, a zero map "
                    "or boss, an interrupt without a spell, a behind without a radius, "
                    "an avoid_cast without a spell or a radius, a linked_death without a partner or a hold percent, or a "
                    "second row of the key)", row.map,
                    row.bossEntry, fields[2].GetCppString().c_str(), row.targetEntry, row.spellId);
            }
        } while (result->NextRow());
    }

    // Sealed even when the table is empty or missing: the tactics then do nothing.
    table.Seal();
    return skipped;
}

void ai::LoadBossTactics()
{
    BossTacticTable& table = GetBossTactics();
    if (table.Sealed())
        return;
    uint32 const skipped = ReadBossTacticRows(table);
    sLog.outString(">> Loaded %u boss tactic rows (%u skipped)", uint32(table.Size()), skipped);
    sLog.outString();
}
