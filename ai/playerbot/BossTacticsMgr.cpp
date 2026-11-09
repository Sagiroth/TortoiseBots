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
        "SELECT `map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, "
        "`hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, "
        "`spot_z` FROM `ai_boss_tactic`"));
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
            row.hpMin = fields[8].GetUInt8();
            row.hpMax = fields[9].GetUInt8();
            row.botAura = fields[10].GetUInt32();
            row.mode = ParseTacticMode(row.kind, fields[11].GetCppString());
            row.windowSec = fields[12].GetFloat();
            row.cycleSpell = fields[13].GetUInt32();
            row.cycleSec = fields[14].GetFloat();
            row.spotX = fields[15].GetFloat();
            row.spotY = fields[16].GetFloat();
            row.spotZ = fields[17].GetFloat();
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
                    "or boss, a key column it cannot use, a health band outside 0-100, a gate on a kind read straight "
                    "from the table, a mode word its kind does not take, an avoid_cast without a spell or a radius, "
                    "another kind's missing or out-of-range spell or param, a linked_death window that is not positive, "
                    "a use_object without a target, a clicker role above 2, a boss range outside 0-60, a window that is "
                    "neither 0 nor at least 10 s, a row with neither aura nor timer, a seek_aura, pull_gate or spread "
                    "row it cannot use, an anchor row it cannot use, a seek_pull row it cannot use, a gate_hold row it "
                    "cannot use, a cycle or spot column on another kind, or a second row of "
                    "the key)", row.map,
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
