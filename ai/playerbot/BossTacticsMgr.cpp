#include "playerbot/playerbot.h"
#include "BossTacticsMgr.h"

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
        "SELECT `map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param` FROM `ai_boss_tactic`"));
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
            if (!table.Add(row))
            {
                ++skipped;
                sLog.outError("ai_boss_tactic: map %u boss %u kind '%s' target %u spell %u skipped (unknown kind, a zero map "
                    "or boss, an interrupt without a spell, a behind without a radius, an avoid_cast without a spell or a "
                    "radius, or a second row of the key)", row.map,
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
