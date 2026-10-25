#pragma once
#include <cstdint>

class PlayerbotAI;
class Unit;

namespace ai
{
    // Effect-scoped protection: the exact encounter aura on a friendly ZG
    // target, enabled on the curing bot. No target discovery or world scans.
    std::uint32_t GetZulGurubProtectedDispelMask(PlayerbotAI* ai, Unit* target);
    bool IsZulGurubCureSpellProtected(PlayerbotAI* ai, Unit* target, std::uint32_t spellId);
}
