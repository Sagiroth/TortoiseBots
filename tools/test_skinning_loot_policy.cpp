#include "../ai/playerbot/policy/loot/SkinningLootPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DecideGatherSkillForLoot;
using ai::IsSkinningLoot;
using ai::kNoGatherSkill;
using ai::kPickpocketLootType;
using ai::kSkinningLootType;
using ai::kSkinningSkillId;

int main()
{
    std::cout << "Starting TortoiseBots skinning-loot policy tests...\n";

    // Server-side types the core sends: corpse 1, pickpocket 2, skinning 6
    // (LootMgr.h), skinning skill 393 (SharedDefines.h).
    CHECK(kSkinningLootType == 6);
    CHECK(kPickpocketLootType == 2);
    CHECK(kSkinningSkillId == 393);

    // 1. The #403 case: a skin window (server type 6) counts as skinning even
    // though the wire byte is LOOT_PICKPOCKETING - the server type is the
    // only input, so the wire rewrite cannot hide it.
    CHECK(DecideGatherSkillForLoot(6, false, 0) == 393);
    CHECK(IsSkinningLoot(6));
    std::cout << "  [PASS] skin window (server type 6) counts as skinning\n";

    // 2. A real pickpocket window (server type 2) is NOT skinning, on a
    // corpse or (hypothetically) a game object.
    CHECK(DecideGatherSkillForLoot(2, false, 0) == kNoGatherSkill);
    CHECK(DecideGatherSkillForLoot(2, true, 0) == 0);
    CHECK(!IsSkinningLoot(2));
    std::cout << "  [PASS] real pickpocket window stays non-gather\n";

    // 3. Ordinary corpse loot is untouched by gathering telemetry.
    CHECK(DecideGatherSkillForLoot(1, false, 0) == kNoGatherSkill);
    CHECK(!IsSkinningLoot(1));
    std::cout << "  [PASS] corpse loot stays non-gather\n";

    // 4. Nodes keep the lock skill: a game-object window carries its lock
    // skill and a skill-less lock carries none. The core opens herb and ore
    // nodes with LOOT_SKINNING (EffectOpenLock), so the real node window is
    // server type 6 and must still read as Herbalism (182) / Mining (186),
    // never as a skin.
    CHECK(DecideGatherSkillForLoot(1, true, 186) == 186);
    CHECK(DecideGatherSkillForLoot(1, true, 0) == 0);
    CHECK(DecideGatherSkillForLoot(6, true, 186) == 186);
    CHECK(DecideGatherSkillForLoot(6, true, 182) == 182);
    CHECK(DecideGatherSkillForLoot(6, true, 0) == kNoGatherSkill);
    std::cout << "  [PASS] node loot keeps the lock skill, also under the skinning type\n";

    // 5. Only a skinned corpse skips the loot filter. A lock-opened game
    // object (quest chest, herb, ore) carries the skinning type too and must
    // still go through it, or a bot takes its master's quest item.
    CHECK(ai::SkipsLootFilter(6, false));
    CHECK(!ai::SkipsLootFilter(6, true));
    CHECK(!ai::SkipsLootFilter(1, false));
    CHECK(!ai::SkipsLootFilter(1, true));
    std::cout << "  [PASS] only a skinned corpse skips the loot filter\n";

    std::cout << "skinning-loot policy: OK\n";
    return 0;
}
