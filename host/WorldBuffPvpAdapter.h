// pi-lens-ignore: clang:pp_file_not_found
#include "ScriptObjects.h"

class Player;

namespace TortoiseBots
{

// Issue #492: Silithyst unlocks. Counts opposite-faction player kills in
// Silithus (zone 1377) toward the invisible Silithyst credit for the whole
// group in reward distance (the solo player whose bots land the killing blow
// still advances). Bots are Players, so opposite-faction bot victims count;
// same-team farming never does. Module-only PlayerScript: zero core changes.
class WorldBuffPvpAdapter final : public PlayerScript
{
public:
    WorldBuffPvpAdapter();

    void OnPVPKill(Player* killer, Player* killed) override;
};

} // namespace TortoiseBots
