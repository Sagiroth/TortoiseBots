#include "playerbot/playerbot.h"
#include "BossStandMgr.h"

#include <memory>

using namespace ai;

BossStandTable& ai::GetBossStands()
{
    static BossStandTable table;
    return table;
}

// One pass over `ai_boss_stand` into `table` (empty, not sealed), then the seal; returns the rows skipped.
std::uint32_t ai::ReadBossStandRows(BossStandTable& table)
{
    uint32 skipped = 0;
    // The core's Query hands over a raw QueryResult* the caller frees (src/shared/Database/Database.h).
    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `map`, `boss_entry`, `anchor`, `anchor_x`, `anchor_y`, `anchor_r`, `role`, `window_spell`, `aura`, `side`, "
        "`seq`, `x`, `y`, `z`, `dx`, `dy`, `slots`, `hold_ms`, `release_anchor`, `window_signal` FROM `ai_boss_stand`"));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BossStandRow row;
            row.map = fields[0].GetUInt32();
            row.bossEntry = fields[1].GetUInt32();
            row.anchor = fields[2].GetUInt32();
            row.anchorX = fields[3].GetFloat();
            row.anchorY = fields[4].GetFloat();
            row.anchorR = fields[5].GetFloat();
            row.role = ParseStandRole(fields[6].GetCppString());
            row.windowSpell = fields[7].GetUInt32();
            row.aura = fields[8].GetUInt32();
            row.side = fields[9].GetUInt8();
            row.seq = fields[10].GetUInt8();
            row.x = fields[11].GetFloat();
            row.y = fields[12].GetFloat();
            row.z = fields[13].GetFloat();
            row.dx = fields[14].GetFloat();
            row.dy = fields[15].GetFloat();
            row.slots = fields[16].GetUInt8();
            row.holdMs = fields[17].GetUInt32();
            row.releaseAnchor = fields[18].GetUInt32();
            row.windowSignal = ParseStandSignal(fields[19].GetCppString());
            if (!table.Add(row))
            {
                ++skipped;
                sLog.outError("ai_boss_stand: map %u boss %u anchor %u role '%s' side %u seq %u skipped (unknown role, a zero "
                    "map or boss, no anchor radius, a side above 1, slots outside 1-%u, a release anchor equal to the anchor, a hold_ms outside 1000-15000, an unknown window_signal or a stopped_no_target one without an aura and a window spell, an anchor id with two places, or a "
                    "second row of the key)", row.map, row.bossEntry, row.anchor, fields[6].GetCppString().c_str(),
                    uint32(row.side), uint32(row.seq), uint32(kBossStandMaxSlots));
            }
        } while (result->NextRow());
    }

    // Sealed even when the table is empty or missing: the stand sets then do nothing. The seal refuses the whole set of
    // a boss whose rows disagree (a release anchor no row of the boss holds).
    table.Seal();
    for (const BossStandRefused& refused : table.Refused())
    {
        ++skipped;
        sLog.outError("ai_boss_stand: map %u boss %u refused as a whole (a release_anchor names an anchor no row of the "
            "boss holds)", refused.map, refused.bossEntry);
    }
    return skipped;
}

void ai::LoadBossStands()
{
    BossStandTable& table = GetBossStands();
    if (table.Sealed())
        return;
    uint32 const skipped = ReadBossStandRows(table);
    sLog.outString(">> Loaded %u boss stand rows (%u skipped)", uint32(table.Size()), skipped);
    sLog.outString();
}
