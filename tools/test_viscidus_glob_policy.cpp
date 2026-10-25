#include "../ai/playerbot/strategy/ViscidusGlobPolicy.h"

#include <iostream>

namespace
{
    struct TargetState
    {
        bool nearby = true;
        bool alive = true;
        bool attackable = true;
        bool cc = false;
        bool excluded = false;
        int validations = 0;

        bool Validate()
        {
            ++validations;
            return nearby && alive && attackable && !cc && !excluded;
        }
    };
}

int main()
{
    using namespace ai;
    TargetState target;
    auto check = [&](uint32_t map, bool enabled, bool alive, bool combat, bool tank, uint32_t entry)
    {
        return CanUseViscidusGlobTarget(map, enabled, alive, combat, tank, entry,
            [&]() { return target.Validate(); });
    };

    // Count callbacks, not just returned eligibility: eager argument evaluation
    // used to scan nearby units even when the encounter/strategy gate rejected.
    if (check(0, true, true, true, false, VISCIDUS_GLOB_ENTRY) ||
        check(AQ40_MAP_ID, false, true, true, false, VISCIDUS_GLOB_ENTRY) ||
        check(AQ40_MAP_ID, true, true, true, false, VISCIDUS_ENTRY) ||
        check(AQ40_MAP_ID, true, false, true, false, VISCIDUS_GLOB_ENTRY) ||
        check(AQ40_MAP_ID, true, true, false, false, VISCIDUS_GLOB_ENTRY) ||
        check(AQ40_MAP_ID, true, true, true, true, VISCIDUS_GLOB_ENTRY) ||
        target.validations != 0)
        return 1;

    auto eligible = [&]() { return check(AQ40_MAP_ID, true, true, true, false, VISCIDUS_GLOB_ENTRY); };
    // Shared production gate accepts a passive glob for selection and retention
    // without victim/threat evidence; each evaluation revalidates its safety.
    if (!eligible() || !eligible() || target.validations != 2)
        return 2;
    target.nearby = false; // rejoined/despawned or outside the scan
    if (eligible()) return 3;
    target.nearby = true;
    target.alive = false; // dead while the cached scan still contains its GUID
    if (eligible()) return 4;
    target.alive = true;
    target.attackable = false;
    if (eligible()) return 5;
    target.attackable = true;
    target.cc = true;
    if (eligible()) return 6;
    target.cc = false;
    target.excluded = true;
    if (eligible()) return 7;
    target.excluded = false;
    const int validations = target.validations;
    if (check(AQ40_MAP_ID, false, true, true, false, VISCIDUS_GLOB_ENTRY) ||
        target.validations != validations)
        return 8;

    std::cout << "Viscidus glob lazy selection/retention policy: OK\n";
}
