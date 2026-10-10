#pragma once

// Pure policy for the Fire Nova totem drop gate (SHM-7): in 1.12 Fire Nova
// is a totem DROP that detonates after 4s — not a pulse of an already-down
// totem (that is the WotLK 3.3.0+ mechanic; do not port it). Refuse to place
// the totem when the target is out of detonation range of the drop point,
// mirroring the magma melee gate. Chain heal needs no change: the live
// `medium aoe heal -> chain heal` row already matches priest/druid shape.

namespace ai
{
    // Drop-point detonation range: the totem lands at our feet, so the
    // bot-to-target distance at cast time bounds the pulse.
    inline float FireNovaDropRange() { return 10.0f; }

    inline bool FireNovaDropShouldFire(float botDistanceToTarget)
    {
        return botDistanceToTarget <= FireNovaDropRange();
    }
}
