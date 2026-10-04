// pi-lens-ignore: clang:pp_file_not_found
#include "ScriptObjects.h"

namespace TortoiseBots
{

// Issue #492: boss-kill unlocks. Onyxia (10184) OR Nefarian (11583) credit
// the invisible Rally entry 95100 for the whole group in reward distance;
// Rend (10429) and Hakkar (14834) quests use direct kill objectives that the
// core shares natively, so this adapter ignores them. Module-only
// PlayerScript: zero core changes, core builds with bots off.
class WorldBuffKillAdapter final : public PlayerScript
{
public:
    WorldBuffKillAdapter();

    void OnCreatureKill(Player* killer, Creature* killed) override;
};

} // namespace TortoiseBots
