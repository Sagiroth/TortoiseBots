#pragma once

// Self-tuning tick budget for the random-pool AI pass (BotManager::UpdateBots).
//
// One fixed microsecond budget cannot fit both a fast desktop (2000 bots, a
// 10 ms pool budget leaves the tick idle) and a weak machine (500 bots, the
// same budget lets the world tick run 200+ ms). This controller watches the
// measured previous world tick and moves the effective pool and combat
// budgets toward a configured tick target instead: over target it shrinks
// multiplicatively (fast retreat when the server struggles), under target it
// grows additively (slow reclaim, so it does not sawtooth). The effective
// budgets are clamped to [floor, configured ceiling], so the configured
// PoolTickBudgetUs/CombatTickBudgetUs stay the hard maximum and 0 keeps its
// "no budget" meaning at the call site.
//
// Hardware-agnostic on purpose: no CPU detection, only feedback on measured
// tick time. Oscillation is damped two ways: the tick reading is an
// exponential moving average (one spike does not yank the budget), and the
// grow/shrink steps are asymmetric (shrink fast, grow slow), with a deadband
// around the target where the budget holds steady.
//
// Pure decision logic, no AI or core types: tested on its own in
// tools/test_adaptive_budget.cpp. World-thread only, like BotManager.

#include <algorithm>
#include <cstdint>

namespace TortoiseBots
{

class AdaptiveBudget
{
public:
    // Coefficients are the policy; the constructor only takes the target and
    // the operator's configured ceilings so tests can pin them down.
    AdaptiveBudget(uint32_t targetTickMs, uint32_t poolCeilingUs, uint32_t combatCeilingUs)
        : m_targetMs(targetTickMs), m_poolCeilingUs(poolCeilingUs), m_combatCeilingUs(combatCeilingUs),
          m_poolUs(poolCeilingUs), m_combatUs(combatCeilingUs), m_tickEmaMs(targetTickMs) {}

    // Feed the measured previous world tick (UpdateBots' diff, ms). Returns
    // nothing; read the budgets back with PoolUs()/CombatUs().
    void Update(uint32_t tickMs)
    {
        if (m_targetMs == 0)
        {
            // Controller off: hold the configured ceilings (the static path).
            m_poolUs = m_poolCeilingUs;
            m_combatUs = m_combatCeilingUs;
            return;
        }

        // EMA first: a single 14 s hitch (seen live at pool start) must not
        // collapse the budget for the next minutes.
        m_tickEmaMs += (static_cast<double>(tickMs) - m_tickEmaMs) / kEmaWindow;

        uint32_t const deadbandLo = m_targetMs > kDeadbandMs ? m_targetMs - kDeadbandMs : 0;
        uint32_t const deadbandHi = m_targetMs + kDeadbandMs;
        if (m_tickEmaMs > deadbandHi)
        {
            // Over target: multiplicative retreat, so a 250 ms tick cuts much
            // harder than a 60 ms one over consecutive ticks.
            m_poolUs = ClampScaled(m_poolUs, kShrinkFactor, m_poolCeilingUs);
            m_combatUs = ClampScaled(m_combatUs, kShrinkFactor, m_combatCeilingUs);
        }
        else if (m_tickEmaMs < deadbandLo)
        {
            // Under target: additive reclaim, so the budget creeps back
            // without overshooting into the next shrink.
            m_poolUs = ClampAdd(m_poolUs, kGrowStepUs, m_poolCeilingUs);
            m_combatUs = ClampAdd(m_combatUs, kGrowStepUs, m_combatCeilingUs);
        }
        // Inside the deadband: hold steady (the anti-oscillation hinge).
    }

    uint64_t PoolUs() const { return m_poolUs; }
    uint64_t CombatUs() const { return m_combatUs; }

    // Test seam: the smoothed reading the controller actually decides on.
    double SmoothedTickMs() const { return m_tickEmaMs; }

private:
    // EMA window in ticks: ~8 ticks to absorb two-thirds of a step.
    static constexpr double kEmaWindow = 8.0;
    // Deadband half-width around the target: inside it the budget holds.
    static constexpr uint32_t kDeadbandMs = 10;
    // Shrink factor per over-target tick (x0.7: ~3 ticks to halve).
    static constexpr double kShrinkFactor = 0.7;
    // Grow step per under-target tick (reclaim a halving in ~7 ticks).
    static constexpr uint64_t kGrowStepUs = 1000;
    // Absolute floor: below ~2 ms a pass serves ~nobody and the rotation
    // still pays the scan, so stop shrinking there.
    static constexpr uint64_t kFloorUs = 2000;

    static uint64_t ClampScaled(uint64_t value, double factor, uint64_t ceiling)
    {
        uint64_t const scaled = static_cast<uint64_t>(value * factor);
        uint64_t const floored = std::max(scaled, kFloorUs);
        return std::min(floored, ceiling);
    }

    static uint64_t ClampAdd(uint64_t value, uint64_t step, uint64_t ceiling)
    {
        return std::min(value + step, ceiling);
    }

    uint32_t m_targetMs;
    uint64_t m_poolCeilingUs;
    uint64_t m_combatCeilingUs;
    uint64_t m_poolUs;
    uint64_t m_combatUs;
    double m_tickEmaMs;
};

} // namespace TortoiseBots
