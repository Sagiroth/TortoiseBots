#pragma once

// Pure decision rules for the Anub'Rekhan fight (mod-playerbots parity:
// NaxxActions_Anubrekhan.cpp, scoped to vanilla-reliable pieces).
// Verified in tw_world + core boss_anubrekhan.cpp: boss 15956, Crypt
// Guard 16573, Locust Swarm 28785 (self-buff on the boss), Impale
// 28783. Omitted: MT kite ring (waypoints need live coords — the tank
// holds position instead); guard-enrage has no bot counter.

namespace ai
{
// Locust Swarm is a self-buff on Anub'Rekhan: the whole non-tank raid
// collapses to the room center while it is up.
inline bool IsAnubrekhanSwarmUp(bool bossHasSwarmAura)
{
    return bossHasSwarmAura;
}
} // namespace ai
