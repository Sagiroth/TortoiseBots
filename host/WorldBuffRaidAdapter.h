// pi-lens-ignore: clang:pp_file_not_found
#include "ScriptObjects.h"

class Player;

namespace TortoiseBots
{

// Issue #492: KeepWorldBuffsInRaids + Upper Karazhan (814). Snapshots live
// buff auras before the teleport, restores them after the map change when
// the keep flag is on, strips them in 814 when off. Memory-only snapshots:
// a logout mid-teleport loses the buffs, same as death. Module-only
// PlayerScript: zero core changes.
class WorldBuffRaidAdapter final : public PlayerScript
{
public:
    WorldBuffRaidAdapter();

    void OnBeforeTeleport(Player* player, uint32 mapId, float x, float y, float z, float orientation) override;
    void OnMapChanged(Player* player) override;
};

} // namespace TortoiseBots
