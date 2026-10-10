#pragma once

// Pure policy for elemental earth-shock execute discipline (mod-playerbots
// parity SHM-4): the donor's ele build fires earth shock only as an execute
// (target below 25% health AND below 1500 absolute hp), saving the shared
// shock cooldown and boss debuff slots on healthy targets. Enhancement keeps
// the ungated shock (melee-range threat tool); interrupts are untouched.

namespace ai
{
    inline float EarthShockExecuteHealthPercent(float health, float maxHealth)
    {
        if (maxHealth <= 0.0f)
            return 100.0f;
        return 100.0f * health / maxHealth;
    }

    // Donor thresholds verbatim: percent < 25 AND absolute < 1500.
    // Dead targets never count (a corpse at 0hp/0max would otherwise read
    // as 100% via the zero-max guard, and a 0hp live target is already dead
    // for combat purposes) — matches the shipped trigger's IsAlive gate.
    inline bool EarthShockExecuteShouldFire(float health, float maxHealth, bool alive = true)
    {
        if (!alive)
            return false;
        if (EarthShockExecuteHealthPercent(health, maxHealth) >= 25.0f)
            return false;
        if (health >= 1500.0f)
            return false;
        return true;
    }
}
