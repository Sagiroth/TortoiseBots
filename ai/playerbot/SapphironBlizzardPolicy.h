#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace ai
{
    constexpr uint32_t SAPPHIRON_BLIZZARD_CREATURE_ENTRY = 16474;
    constexpr uint32_t SAPPHIRON_BLIZZARD_DAMAGE_SPELL_ID = 28547;
    constexpr uint32_t SAPPHIRON_BLIZZARD_DAMAGE_EFFECT_INDEX = 0;

    struct SapphironDamageAreaId
    {
        uint64_t caster;
        uint64_t area;
    };

    // Per-bot discovery only. Callers still resolve live area positions/durations.
    struct SapphironBlizzardSnapshot
    {
        static constexpr uint32_t refreshMs = 500;
        bool initialized = false;
        uint32_t map = 0;
        uint32_t instance = 0;
        uint32_t refreshedAt = 0;
        std::vector<SapphironDamageAreaId> areas;

        bool NeedsRefresh(uint32_t now, uint32_t mapId, uint32_t instanceId) const
        {
            return !initialized || map != mapId || instance != instanceId ||
                uint32_t(now - refreshedAt) >= refreshMs;
        }

        void BeginRefresh(uint32_t now, uint32_t mapId, uint32_t instanceId)
        {
            initialized = true;
            map = mapId;
            instance = instanceId;
            refreshedAt = now;
            areas.clear();
        }
    };

    inline bool IsActiveSapphironDamageArea(uint32_t casterEntry, uint32_t spellId,
        uint32_t effectIndex, int32_t remainingMs, float radius)
    {
        return casterEntry == SAPPHIRON_BLIZZARD_CREATURE_ENTRY &&
            spellId == SAPPHIRON_BLIZZARD_DAMAGE_SPELL_ID &&
            effectIndex == SAPPHIRON_BLIZZARD_DAMAGE_EFFECT_INDEX &&
            remainingMs > 0 && std::isfinite(radius) && radius > 0.0f;
    }

    inline bool IsInsideSapphironDamageArea(float x, float y, float centerX,
        float centerY, float radius)
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(centerX) &&
            std::isfinite(centerY) && std::isfinite(radius) && radius > 0.0f &&
            std::hypot(x - centerX, y - centerY) <= radius;
    }
}
