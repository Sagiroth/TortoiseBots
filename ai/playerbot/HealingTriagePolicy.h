#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai
{
    struct HealingBands
    {
        float critical;
        float low;
        float medium;
        float almostFull;
    };

    struct HealingCandidate
    {
        std::uint64_t guid;
        std::uint32_t health;
        std::uint32_t maxHealth;
        std::uint32_t incomingDamage;
        bool tank;
        bool pet;
        bool incomingHeal;
        bool inLos = true;
        bool inCombat = false;
        bool patchwerkAssigned = false;
    };

    inline float PredictedHealHealth(HealingCandidate const& candidate)
    {
        if (!candidate.maxHealth)
            return 100.0f;
        auto const remaining = candidate.incomingDamage >= candidate.health
            ? 0 : candidate.health - candidate.incomingDamage;
        return 100.0f * remaining / candidate.maxHealth;
    }

    // Combat tank health is a conservative urgency signal, not a damage-rate
    // estimate. Include assigned soakers: Hateful Strike does not set a victim.
    inline bool TankHealingDanger(HealingCandidate const& candidate, HealingBands const& bands)
    {
        return candidate.tank && candidate.inCombat && candidate.health &&
            candidate.maxHealth && PredictedHealHealth(candidate) < (candidate.patchwerkAssigned ? bands.almostFull : bands.low);
    }

    inline bool FastTankHealing(HealingCandidate const& candidate, HealingBands const& bands)
    {
        return candidate.tank && candidate.inCombat && candidate.health &&
            candidate.maxHealth && PredictedHealHealth(candidate) < bands.medium;
    }

    inline bool NeedsPartyHealing(HealingCandidate const& candidate, HealingBands const& bands)
    {
        if (!candidate.health || !candidate.maxHealth)
            return false;
        float const health = PredictedHealHealth(candidate);
        // A heal in flight is not a guarantee of survival: never veto urgent help.
        return health < bands.almostFull && (!candidate.incomingHeal || health < bands.medium ||
            (candidate.patchwerkAssigned && candidate.inCombat));
    }

    inline unsigned HealingUrgency(HealingCandidate const& candidate, HealingBands const& bands)
    {
        float const health = PredictedHealHealth(candidate);
        unsigned const band = health < bands.critical ? 0 : health < bands.low ? 1
            : health < bands.medium ? 2 : 3;
        // Pets are a fallback, not competitors for an injured raid member's slot.
        return band + (candidate.pet ? 4 : 0);
    }

    // A visible eligible target prevents opportunistic pursuit behind a wall.
    // Explicit focus assignments may still use their normal reach prerequisite.
    inline void FilterHealingLineOfSight(std::vector<HealingCandidate>& candidates,
                                         bool focused, bool canReach)
    {
        bool const visible = std::any_of(candidates.begin(), candidates.end(),
            [](HealingCandidate const& candidate) { return candidate.inLos; });
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
            [&](HealingCandidate const& candidate)
            { return !candidate.inLos && (!canReach || (visible && !focused)); }), candidates.end());
    }

    inline bool HealingPriority(HealingCandidate const& left, HealingCandidate const& right,
                                HealingBands const& bands)
    {
        bool const leftDanger = TankHealingDanger(left, bands);
        bool const rightDanger = TankHealingDanger(right, bands);
        if (leftDanger != rightDanger)
            return leftDanger;
        unsigned const leftBand = HealingUrgency(left, bands);
        unsigned const rightBand = HealingUrgency(right, bands);
        if (leftBand != rightBand)
            return leftBand < rightBand;
        if (left.inLos != right.inLos)
            return left.inLos;
        float leftHealth = PredictedHealHealth(left);
        float rightHealth = PredictedHealHealth(right);
        // Small tank preference within a band; in emergencies the lowest health wins.
        if (leftBand != 0)
        {
            leftHealth -= left.tank ? 10.0f : 0.0f;
            rightHealth -= right.tank ? 10.0f : 0.0f;
        }
        if (leftHealth != rightHealth)
            return leftHealth < rightHealth;
        if (left.tank != right.tank)
            return left.tank;
        return left.guid < right.guid;
    }

    // Input is sorted by HealingPriority. Distribute only across the most urgent
    // band, so an extra healer never tops somebody up while an emergency waits.
    inline std::size_t HealingAssignment(std::vector<HealingCandidate> const& candidates,
                                         HealingBands const& bands, std::size_t healerOrdinal)
    {
        if (candidates.empty())
            return 0;
        // Share noncritical Patchwerk assignments across the main and soakers;
        // otherwise a perpetually low main monopolizes every healer until a
        // soaker is already critical. Critical patients still get the old band.
        if (candidates.front().patchwerkAssigned &&
            TankHealingDanger(candidates.front(), bands) &&
            PredictedHealHealth(candidates.front()) >= bands.critical)
        {
            auto eligible = [&](HealingCandidate const& candidate)
            {
                return candidate.patchwerkAssigned && TankHealingDanger(candidate, bands) &&
                    candidate.inLos == candidates.front().inLos;
            };
            std::size_t count = std::count_if(candidates.begin(), candidates.end(), eligible);
            std::size_t slot = healerOrdinal % count;
            for (std::size_t i = 0; i < candidates.size(); ++i)
                if (eligible(candidates[i]) && slot-- == 0)
                    return i;
        }
        unsigned const urgency = HealingUrgency(candidates.front(), bands);
        std::size_t count = 1;
        while (count < candidates.size() && HealingUrgency(candidates[count], bands) == urgency &&
               candidates[count].inLos == candidates.front().inLos &&
               TankHealingDanger(candidates[count], bands) == TankHealingDanger(candidates.front(), bands))
            ++count;
        return healerOrdinal % count;
    }
}
