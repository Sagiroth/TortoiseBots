#pragma once
// The ranged shot ledger's core seam (runtime/RangedShotLedger.h): one PlayerScript on the spell cast hook, on the
// map threads. It counts the auto-repeat shots (Auto Shot, a wand's Shoot) a player executed.
#include "ScriptObjects.h"

namespace TortoiseBots {

class RangedShotAdapter final : public PlayerScript
{
public:
    RangedShotAdapter();

    void OnSpellCast(Player* player, Spell* spell, bool skipCheck) override;
};

} // namespace TortoiseBots
