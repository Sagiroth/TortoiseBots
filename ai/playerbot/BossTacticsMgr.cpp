#include "playerbot/playerbot.h"
#include "BossTacticsMgr.h"

using namespace ai;

BossTacticTable& ai::GetBossTactics()
{
    static BossTacticTable table;
    return table;
}

void ai::LoadBossTactics()
{
    BossTacticTable& table = GetBossTactics();
    if (table.Sealed())
        return;

    uint32 skipped = 0;
    auto result = WorldDatabase.Query(
        "SELECT `map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param` FROM `ai_boss_tactic`");
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BossTacticRow row;
            row.map = fields[0].GetUInt32();
            row.bossEntry = fields[1].GetUInt32();
            row.kind = ParseBossTacticKind(fields[2].GetCppString());
            row.targetEntry = fields[3].GetUInt32();
            row.spellId = fields[4].GetUInt32();
            row.priority = fields[5].GetUInt8();
            row.param = fields[6].GetFloat();
            if (!table.Add(row))
                ++skipped;
        } while (result->NextRow());
    }

    // Sealed even when the table is empty or missing: the tactics then do nothing.
    table.Seal();
    sLog.outString(">> Loaded %u boss tactic rows (%u skipped)", uint32(table.Size()), skipped);
    sLog.outString();
}
