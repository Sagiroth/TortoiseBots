// Standalone regression test for the warlock Soulstone rules
// (runtime/WarlockSoulstonePolicy.h): who the warlock stones in a group
// (healer, else tank, else master/leader), never in combat, nothing when the
// aura is already up or the target is not ready, no create without a Soul
// Shard, and the stone rank the bot uses.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_warlock_soulstone_policy.cpp -o /tmp/test_warlock_soulstone
//   /tmp/test_warlock_soulstone

#include "../runtime/WarlockSoulstonePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static SoulstoneFacts Grouped()
{
    SoulstoneFacts facts;
    facts.inGroup = true;
    return facts;
}

static SoulstoneCandidate Healer() { SoulstoneCandidate c; c.isHealer = true; return c; }
static SoulstoneCandidate Tank() { SoulstoneCandidate c; c.isTank = true; return c; }
static SoulstoneCandidate Leader() { SoulstoneCandidate c; c.isLeader = true; return c; }
static SoulstoneCandidate Dps() { return SoulstoneCandidate(); }

static bool PicksMember(SoulstoneChoice const& choice, size_t index)
{
    return choice.kind == SoulstoneChoiceKind::Member && choice.index == index;
}

// The healer beats the leader, whatever the order in the group list.
static void TestHealerBeatsLeader()
{
    CHECK(PicksMember(DecideSoulstoneTarget(Grouped(), {Leader(), Healer(), Dps()}), 1));
    CHECK(PicksMember(DecideSoulstoneTarget(Grouped(), {Healer(), Leader()}), 0));
    // A leader who is the healer is simply the healer.
    SoulstoneCandidate leaderHealer = Healer();
    leaderHealer.isLeader = true;
    CHECK(PicksMember(DecideSoulstoneTarget(Grouped(), {Dps(), leaderHealer}), 1));
}

// No healer: the tank (existing behaviour), then the master/leader.
static void TestFallbackTiers()
{
    CHECK(PicksMember(DecideSoulstoneTarget(Grouped(), {Leader(), Tank(), Dps()}), 1));
    CHECK(PicksMember(DecideSoulstoneTarget(Grouped(), {Dps(), Leader(), Dps()}), 1));
    // Nobody with a role and no leader: nothing to stone.
    CHECK(DecideSoulstoneTarget(Grouped(), {Dps(), Dps()}).kind == SoulstoneChoiceKind::None);
    CHECK(DecideSoulstoneTarget(Grouped(), {}).kind == SoulstoneChoiceKind::None);
}

// Aura already up: skip, and do not hand the stone to a lower tier (the
// item cooldown is 30 minutes, the healer stays the target).
static void TestSkipsWhenAuraPresent()
{
    SoulstoneCandidate stoned = Healer();
    stoned.hasSoulstoneAura = true;
    CHECK(DecideSoulstoneTarget(Grouped(), {stoned, Leader()}).kind == SoulstoneChoiceKind::None);
    // Two healers: the one without the aura gets it.
    CHECK(PicksMember(DecideSoulstoneTarget(Grouped(), {stoned, Healer()}), 1));
    // Aura falls off: stoned again.
    stoned.hasSoulstoneAura = false;
    CHECK(PicksMember(DecideSoulstoneTarget(Grouped(), {stoned, Leader()}), 0));
}

// Never in combat, in any shape of group.
static void TestSkipsInCombat()
{
    SoulstoneFacts facts = Grouped();
    facts.botInCombat = true;
    CHECK(DecideSoulstoneTarget(facts, {Healer(), Leader()}).kind == SoulstoneChoiceKind::None);
    SoulstoneFacts solo;
    solo.botInCombat = true;
    CHECK(DecideSoulstoneTarget(solo, {}).kind == SoulstoneChoiceKind::None);
}

// A tier that exists but is not ready is waited for: out of range, no line
// of sight, dead, or another warlock already casting on it.
static void TestWaitsForTheHealer()
{
    SoulstoneCandidate far = Healer();
    far.inRange = false;
    CHECK(DecideSoulstoneTarget(Grouped(), {far, Tank(), Leader()}).kind == SoulstoneChoiceKind::None);
    SoulstoneCandidate hidden = Healer();
    hidden.inLineOfSight = false;
    CHECK(DecideSoulstoneTarget(Grouped(), {hidden, Leader()}).kind == SoulstoneChoiceKind::None);
    SoulstoneCandidate dead = Healer();
    dead.alive = false;
    CHECK(DecideSoulstoneTarget(Grouped(), {dead, Leader()}).kind == SoulstoneChoiceKind::None);
    SoulstoneCandidate claimed = Healer();
    claimed.targetedByOtherWarlock = true;
    CHECK(DecideSoulstoneTarget(Grouped(), {claimed, Leader()}).kind == SoulstoneChoiceKind::None);
    // Once the healer is back in range it is stoned.
    far.inRange = true;
    CHECK(PicksMember(DecideSoulstoneTarget(Grouped(), {far, Tank()}), 0));
}

// Solo: the warlock stones itself unless the buff is up.
static void TestSolo()
{
    SoulstoneFacts solo;
    CHECK(DecideSoulstoneTarget(solo, {}).kind == SoulstoneChoiceKind::Self);
    solo.botHasSoulstoneAura = true;
    CHECK(DecideSoulstoneTarget(solo, {}).kind == SoulstoneChoiceKind::None);
}

// In a group the warlock never stones itself.
static void TestGroupNeverSelf()
{
    CHECK(DecideSoulstoneTarget(Grouped(), {Dps()}).kind != SoulstoneChoiceKind::Self);
}

// No Soul Shard, no create; one shard is enough; a held stone, a low level
// and combat each stop it.
static void TestCreateNeedsShard()
{
    SoulstoneCreateFacts facts;
    facts.level = 40;
    facts.soulShards = 0;
    CHECK(!ShouldCreateSoulstone(facts));
    facts.soulShards = 1;
    CHECK(ShouldCreateSoulstone(facts));
    facts.soulstonesHeld = 1;
    CHECK(!ShouldCreateSoulstone(facts));
    facts.soulstonesHeld = 0;
    facts.inCombat = true;
    CHECK(!ShouldCreateSoulstone(facts));
    facts.inCombat = false;
    facts.level = 17;
    CHECK(!ShouldCreateSoulstone(facts));
    facts.level = 18;
    CHECK(ShouldCreateSoulstone(facts));
}

// Rank by level, and the best held rank wins over the level's rank.
static void TestStoneRank()
{
    CHECK(SoulstoneItemForLevel(17) == 0);
    CHECK(SoulstoneItemForLevel(18) == WARLOCK_SOULSTONE_MINOR && WARLOCK_SOULSTONE_MINOR == 5232);
    CHECK(SoulstoneItemForLevel(29) == 5232);
    CHECK(SoulstoneItemForLevel(30) == WARLOCK_SOULSTONE_LESSER && WARLOCK_SOULSTONE_LESSER == 16892);
    CHECK(SoulstoneItemForLevel(40) == WARLOCK_SOULSTONE_PLAIN && WARLOCK_SOULSTONE_PLAIN == 16893);
    CHECK(SoulstoneItemForLevel(50) == WARLOCK_SOULSTONE_GREATER && WARLOCK_SOULSTONE_GREATER == 16895);
    CHECK(SoulstoneItemForLevel(59) == 16895);
    CHECK(SoulstoneItemForLevel(60) == WARLOCK_SOULSTONE_MAJOR && WARLOCK_SOULSTONE_MAJOR == 16896);

    auto none = [](uint32_t) { return 0; };
    CHECK(PickSoulstoneItem(35, none) == 16892);
    CHECK(PickSoulstoneItem(10, none) == 0);
    // A Minor stone made at 29 and carried past level 30 is still the stone to use.
    auto holdsMinor = [](uint32_t id) { return id == 5232 ? 1 : 0; };
    CHECK(PickSoulstoneItem(31, holdsMinor) == 5232);
    // Two ranks held: the better one goes first.
    auto holdsTwo = [](uint32_t id) { return (id == 5232 || id == 16893) ? 1 : 0; };
    CHECK(PickSoulstoneItem(45, holdsTwo) == 16893);
}

int main()
{
    TestHealerBeatsLeader();
    TestFallbackTiers();
    TestSkipsWhenAuraPresent();
    TestSkipsInCombat();
    TestWaitsForTheHealer();
    TestSolo();
    TestGroupNeverSelf();
    TestCreateNeedsShard();
    TestStoneRank();
    std::printf("test_warlock_soulstone_policy: %d checks passed\n", checks);
    return 0;
}
