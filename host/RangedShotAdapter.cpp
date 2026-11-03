// The ranged shot ledger's core seam: no pointer is kept past the call. Spell::cast runs the hook once the spell's
// targets are filled in, for the auto-repeat spell Unit::_UpdateAutoRepeatSpell creates for every shot (a triggered
// spell, so the combat monitor's own cast hook skips it), so a shot CheckCast refused is never counted.
#include "RangedShotAdapter.h"
#include "../runtime/RangedShotLedger.h"
#include "Player.h"
#include "Spell.h"
#include "SpellEntry.h"

namespace TortoiseBots {

RangedShotAdapter::RangedShotAdapter()
    : PlayerScript("tortoisebots_ranged_shots", { PLAYERHOOK_ON_SPELL_CAST })
{
    DungeonRun::RangedShotLedger::Instance().Arm();
}

void RangedShotAdapter::OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/)
{
    if (!player || !spell)
        return;
    SpellEntry const* info = spell->m_spellInfo;
    if (!info || !info->IsAutoRepeatRangedSpell())
        return;
    DungeonRun::RangedShotLedger::Instance().Record(player->GetObjectGuid().GetRawValue());
}

} // namespace TortoiseBots
