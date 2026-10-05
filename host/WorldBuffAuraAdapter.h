// pi-lens-ignore: clang:pp_file_not_found
#include "ScriptObjects.h"

class Player;
class Unit;
class Aura;

namespace TortoiseBots
{

// Issue #492: aura-gain unlocks. Watches every real aura apply and completes
// the receiver's own event quest (DM any-one, Sayge any-of-8, Songflower) —
// never the group, and never when the caster is a hire recruiter (purchased
// auras must not unlock). Module-only UnitScript: zero core changes.
class WorldBuffAuraAdapter final : public UnitScript
{
public:
    WorldBuffAuraAdapter();

    void OnAuraApply(Unit* unit, Aura* aura) override;
};

} // namespace TortoiseBots
