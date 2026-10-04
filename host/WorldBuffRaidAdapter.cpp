// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "WorldBuffRaidAdapter.h"
#include "../runtime/WorldBuffPolicy.h"
#include "../runtime/WorldBuffRaidKeeper.h"
#include "../ai/playerbot/playerbot.h"

#include "ScriptObjects.h"
#include "Player.h"
#include "Spells/SpellAuras.h"

namespace TortoiseBots
{
namespace
{

WorldBuffRaidKeeper keeper;

} // namespace

WorldBuffRaidAdapter::WorldBuffRaidAdapter()
    : PlayerScript("tortoisebots_worldbuff_raids", { PLAYERHOOK_ON_BEFORE_TELEPORT, PLAYERHOOK_ON_MAP_CHANGED })
{
}

void WorldBuffRaidAdapter::OnBeforeTeleport(Player* player, uint32 mapId, float /*x*/, float /*y*/, float /*z*/, float /*orientation*/)
{
    if (!player)
        return;
    if (!sPlayerbotAIConfig.worldBuffsEnabled)
        return;
    // Snapshot only for destinations that strip: the 10 core maps plus 814.
    // Everywhere else the keeper stays empty and OnMapChanged is a no-op.
    if (!NeedsBuffSnapshot(mapId))
    {
        keeper.Clear(player->GetGUIDLow());
        return;
    }
    std::vector<WorldBuffSnapshotEntry> entries;
    uint32_t count = 0;
    for (uint32_t const* spell = RaidStripSpells(count); count > 0; --count, ++spell)
    {
        Aura* aura = player->GetAura(*spell, EFFECT_INDEX_0);
        if (!aura)
            aura = player->GetAura(*spell, EFFECT_INDEX_1);
        if (!aura)
            aura = player->GetAura(*spell, EFFECT_INDEX_2);
        if (!aura)
            continue;
        WorldBuffSnapshotEntry entry;
        entry.spellId = *spell;
        entry.remainMs = aura->GetAuraDuration();
        entry.maxMs = aura->GetAuraMaxDuration();
        entries.push_back(entry);
    }
    keeper.Snapshot(player->GetGUIDLow(), entries);
}

void WorldBuffRaidAdapter::OnMapChanged(Player* player)
{
    if (!player)
        return;
    if (!sPlayerbotAIConfig.worldBuffsEnabled)
        return;
    uint32_t mapId = player->GetMapId();
    bool keep = sPlayerbotAIConfig.worldBuffsKeepInRaids;
    if (ShouldStripUpperKara(keep, mapId))
    {
        std::vector<WorldBuffSnapshotEntry> dropped;
        keeper.Take(player->GetGUIDLow(), dropped);
        uint32_t count = 0;
        for (uint32_t const* spell = RaidStripSpells(count); count > 0; --count, ++spell)
            player->RemoveAurasDueToSpellByCancel(*spell);
        return;
    }
    if (!ShouldRestoreAfterTeleport(keep, mapId))
    {
        keeper.Clear(player->GetGUIDLow());
        return;
    }
    std::vector<WorldBuffSnapshotEntry> entries;
    if (!keeper.Take(player->GetGUIDLow(), entries))
        return;
    for (WorldBuffSnapshotEntry const& entry : entries)
    {
        // The core just stripped these; re-apply with the same remaining
        // time. The player is the caster (no recruiter guid involved), so
        // the restore never looks like a purchase to the aura-credit door.
        // Silithyst is not in the strip set and is never snapshotted.
        if (player->HasAura(entry.spellId))
            continue;
        if (player->AddAura(entry.spellId, 0, player))
        {
            // Duration lives on the holder, not the Aura: GetAura returns
            // the effect-0 Aura, whose holder owns both durations.
            if (Aura* aura = player->GetAura(entry.spellId, EFFECT_INDEX_0))
            {
                aura->GetHolder()->SetAuraMaxDuration(entry.maxMs);
                aura->GetHolder()->SetAuraDuration(entry.remainMs);
            }
        }
    }
}

} // namespace TortoiseBots
