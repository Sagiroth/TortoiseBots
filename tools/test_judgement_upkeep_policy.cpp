// Pure decision rules for the opt-in paladin Judgement upkeep
// (AiPlayerbot.PaladinJudgementUpkeep): which debuff a paladin owns, whether
// the target is worth it, whether the bot can afford it, and when to swap the
// Seal and Judge.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_judgement_upkeep_policy.cpp -o /tmp/test_judgement_upkeep
//   /tmp/test_judgement_upkeep

#include "../runtime/policy/class/JudgementUpkeepPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace TortoiseBots;

#define CHECK(x) do { \
    if (!(x)) { \
        std::fprintf(stderr, "Assertion failed at line %d: %s\n", __LINE__, #x); \
        std::exit(1); \
    } \
} while (0)

// Paladin with both seals by default; `healer` picks the role.
static JudgementPaladin P(uint64_t id, bool healer, bool wisdom = true, bool light = true, bool tank = false)
{
    JudgementPaladin p;
    p.id = id;
    p.healer = healer;
    p.tank = tank;
    p.canWisdom = wisdom;
    p.canLight = light;
    return p;
}

static JudgementKind Kind(uint64_t self, const std::vector<JudgementPaladin>& group, bool tankMayOwnWisdom = false)
{
    return JudgementKindFor(self, group.data(), group.size(), tankMayOwnWisdom);
}

static void TestOwnership()
{
    // Nothing to decide.
    CHECK(JudgementKindFor(1, nullptr, 0) == JudgementKind::None);
    CHECK(Kind(7, {}) == JudgementKind::None);

    // One paladin: the role picks the debuff (donor: tank/dps Wisdom, heal Light).
    CHECK(Kind(1, { P(1, false) }) == JudgementKind::Wisdom);
    CHECK(Kind(1, { P(1, true) }) == JudgementKind::Light);

    // Tank-or-dps paladin plus a healing paladin: each keeps its own.
    std::vector<JudgementPaladin> mixed = { P(10, true), P(20, false) };
    CHECK(Kind(10, mixed) == JudgementKind::Light);
    CHECK(Kind(20, mixed) == JudgementKind::Wisdom);

    // Same answer whatever the order the group lists them in.
    std::vector<JudgementPaladin> mixedRev = { P(20, false), P(10, true) };
    CHECK(Kind(10, mixedRev) == JudgementKind::Light);
    CHECK(Kind(20, mixedRev) == JudgementKind::Wisdom);

    // Two Retribution/Protection paladins: lowest guid Wisdom, the other Light.
    std::vector<JudgementPaladin> twoDps = { P(5, false), P(3, false) };
    CHECK(Kind(3, twoDps) == JudgementKind::Wisdom);
    CHECK(Kind(5, twoDps) == JudgementKind::Light);

    // Two healing paladins: lowest guid Light, the other backfills Wisdom.
    std::vector<JudgementPaladin> twoHeal = { P(8, true), P(6, true) };
    CHECK(Kind(6, twoHeal) == JudgementKind::Light);
    CHECK(Kind(8, twoHeal) == JudgementKind::Wisdom);

    // A third paladin owns nothing; the two lowest guids of the role keep their debuffs.
    std::vector<JudgementPaladin> three = { P(9, false), P(4, false), P(6, false) };
    CHECK(Kind(4, three) == JudgementKind::Wisdom);
    CHECK(Kind(6, three) == JudgementKind::Light);
    CHECK(Kind(9, three) == JudgementKind::None);

    // Every debuff has at most one owner, whatever the mix.
    std::vector<JudgementPaladin> raid = { P(11, true), P(12, true), P(13, false), P(14, false), P(15, false) };
    int wisdomOwners = 0, lightOwners = 0;
    for (const JudgementPaladin& p : raid)
    {
        JudgementKind k = Kind(p.id, raid);
        if (k == JudgementKind::Wisdom) ++wisdomOwners;
        if (k == JudgementKind::Light) ++lightOwners;
    }
    CHECK(wisdomOwners == 1);
    CHECK(lightOwners == 1);
    CHECK(Kind(13, raid) == JudgementKind::Wisdom);
    CHECK(Kind(11, raid) == JudgementKind::Light);
    CHECK(Kind(12, raid) == JudgementKind::None);

    // A self that is not in the list owns nothing.
    CHECK(Kind(99, mixed) == JudgementKind::None);
}

static void TestTankNeverOwns()
{
    // Lone Protection tank: nothing is upkept.
    CHECK(Kind(1, { P(1, false, true, true, true) }) == JudgementKind::None);
    // Protection tank (lowest guid) + Retribution: Retribution owns Wisdom, the tank nothing.
    std::vector<JudgementPaladin> protRet = { P(1, false, true, true, true), P(2, false) };
    CHECK(Kind(1, protRet) == JudgementKind::None);
    CHECK(Kind(2, protRet) == JudgementKind::Wisdom);
    // Tank + Holy: Holy owns Light, the tank nothing, Wisdom is not upkept.
    std::vector<JudgementPaladin> protHoly = { P(1, false, true, true, true), P(2, true) };
    CHECK(Kind(1, protHoly) == JudgementKind::None);
    CHECK(Kind(2, protHoly) == JudgementKind::Light);
    // Tank + Retribution + Holy: Wisdom and Light go to the non-tanks.
    std::vector<JudgementPaladin> trio = { P(1, false, true, true, true), P(2, false), P(3, true) };
    CHECK(Kind(1, trio) == JudgementKind::None);
    CHECK(Kind(2, trio) == JudgementKind::Wisdom);
    CHECK(Kind(3, trio) == JudgementKind::Light);
    // Tank + two Retribution: leftovers go to the second non-tank, never the tank.
    std::vector<JudgementPaladin> protRetRet = { P(1, false, true, true, true), P(2, false), P(3, false) };
    CHECK(Kind(1, protRetRet) == JudgementKind::None);
    CHECK(Kind(2, protRetRet) == JudgementKind::Wisdom);
    CHECK(Kind(3, protRetRet) == JudgementKind::Light);
}

static void TestTankDungeonWisdom()
{
    // Lone Protection tank in a 5-man dungeon with the flag on: owns Wisdom if known.
    CHECK(Kind(1, { P(1, false, true, true, true) }, true) == JudgementKind::Wisdom);
    CHECK(Kind(1, { P(1, false, false, true, true) }, true) == JudgementKind::None);

    // Tank + Retribution (knows Wisdom): Retribution owns Wisdom, tank owns None.
    std::vector<JudgementPaladin> protRet = { P(1, false, true, true, true), P(2, false) };
    CHECK(Kind(1, protRet, true) == JudgementKind::None);
    CHECK(Kind(2, protRet, true) == JudgementKind::Wisdom);

    // Tank + Retribution (lacks Wisdom, has Light): Retribution takes Light, tank backfills Wisdom.
    std::vector<JudgementPaladin> protRetNoWis = { P(1, false, true, true, true), P(2, false, false, true) };
    CHECK(Kind(1, protRetNoWis, true) == JudgementKind::Wisdom);
    CHECK(Kind(2, protRetNoWis, true) == JudgementKind::Light);

    // Tank + Holy (both seals): Holy owns Light, tank backfills Wisdom.
    std::vector<JudgementPaladin> protHoly = { P(1, false, true, true, true), P(2, true) };
    CHECK(Kind(1, protHoly, true) == JudgementKind::Wisdom);
    CHECK(Kind(2, protHoly, true) == JudgementKind::Light);

    // Tank + Holy (Wisdom only): Holy backfills Wisdom, tank owns None.
    std::vector<JudgementPaladin> protHolyWis = { P(1, false, true, true, true), P(2, true, true, false) };
    CHECK(Kind(1, protHolyWis, true) == JudgementKind::None);
    CHECK(Kind(2, protHolyWis, true) == JudgementKind::Wisdom);

    // Tank + Retribution + Holy: non-tanks own both, tank owns None.
    std::vector<JudgementPaladin> trio = { P(1, false, true, true, true), P(2, false), P(3, true) };
    CHECK(Kind(1, trio, true) == JudgementKind::None);
    CHECK(Kind(2, trio, true) == JudgementKind::Wisdom);
    CHECK(Kind(3, trio, true) == JudgementKind::Light);

    // Two tanks: lowest guid tank owns Wisdom, second tank owns None.
    std::vector<JudgementPaladin> twoTanks = { P(5, false, true, true, true), P(3, false, true, true, true) };
    CHECK(Kind(3, twoTanks, true) == JudgementKind::Wisdom);
    CHECK(Kind(5, twoTanks, true) == JudgementKind::None);

    // Tank that prefers SoR on bosses does not refill an empty slot with SoW mid-cycle.
    CHECK(!ShouldCastUpkeepSeal(JudgementKind::Wisdom, false, false, false, false, false));
    CHECK(ShouldCastUpkeepSeal(JudgementKind::Wisdom, false, false, true, true, false));
    CHECK(ShouldCastUpkeepSeal(JudgementKind::Wisdom, true, false, true, true, false));
}

static void TestProtWantedSeal()
{
    // Level < 18 (does not know Seal of Wisdom): always Righteousness.
    CHECK(ProtPaladinWantedSeal(true, true, false, true, false) == ProtPaladinSeal::Righteousness);
    CHECK(ProtPaladinWantedSeal(true, true, false, false, false) == ProtPaladinSeal::Righteousness);

    // Low mana: always Wisdom regardless of zone or boss.
    CHECK(ProtPaladinWantedSeal(true, true, true, true, true) == ProtPaladinSeal::Wisdom);
    CHECK(ProtPaladinWantedSeal(true, false, false, true, true) == ProtPaladinSeal::Wisdom);
    CHECK(ProtPaladinWantedSeal(false, true, false, true, true) == ProtPaladinSeal::Wisdom);

    // 5-man dungeon with flag on: Wisdom on trash, Righteousness on boss.
    CHECK(ProtPaladinWantedSeal(true, true, false, false, true) == ProtPaladinSeal::Wisdom);
    CHECK(ProtPaladinWantedSeal(true, true, true, false, true) == ProtPaladinSeal::Righteousness);

    // Raids / solo (not 5-man) or flag off: Righteousness above medium mana.
    CHECK(ProtPaladinWantedSeal(true, false, false, false, true) == ProtPaladinSeal::Righteousness);
    CHECK(ProtPaladinWantedSeal(false, true, false, false, true) == ProtPaladinSeal::Righteousness);
}

static void TestSealAvailability()
{
    // Seal of Light (baseLevel 30) and Seal of Wisdom (baseLevel 18) are learned at different levels.
    // A Retribution paladin that has not learned Wisdom yet takes Light.
    CHECK(Kind(1, { P(1, false, false, true) }) == JudgementKind::Light);
    // A healer that only has Light owns Light.
    CHECK(Kind(1, { P(1, true, false, true) }) == JudgementKind::Light);
    // A paladin that knows neither Seal: nothing to do.
    CHECK(Kind(1, { P(1, false, false, false) }) == JudgementKind::None);
    CHECK(Kind(1, { P(1, true, false, false) }) == JudgementKind::None);
    // A lone healer that knows Wisdom only (not reachable in vanilla, but defined): it takes the free debuff.
    CHECK(Kind(1, { P(1, true, true, false) }) == JudgementKind::Wisdom);

    // Retribution (Wisdom + Light) and a Holy paladin with Light only:
    // dps Wisdom, healer Light.
    std::vector<JudgementPaladin> g = { P(1, false), P(2, true, false, true) };
    CHECK(Kind(1, g) == JudgementKind::Wisdom);
    CHECK(Kind(2, g) == JudgementKind::Light);

    // Retribution with Light only next to a Holy paladin with both: the
    // healer owns Light, nobody can take Wisdom (the Retribution paladin lacks
    // the Seal), and no paladin ends up owning both.
    std::vector<JudgementPaladin> h = { P(1, false, false, true), P(2, true) };
    CHECK(Kind(2, h) == JudgementKind::Light);
    CHECK(Kind(1, h) == JudgementKind::None);
}

static void TestTarget()
{
    // Never solo.
    CHECK(!IsJudgementUpkeepTarget(false, true, true, 100));
    // Boss / elite: yes.
    CHECK(IsJudgementUpkeepTarget(true, true, false, 100));
    // The tank's mob inside a dungeon or raid: yes, even a non-elite.
    CHECK(IsJudgementUpkeepTarget(true, false, true, 100));
    // Open-world trash that is neither: no.
    CHECK(!IsJudgementUpkeepTarget(true, false, false, 100));
    // Nearly dead: no, boss or not.
    CHECK(!IsJudgementUpkeepTarget(true, true, true, kJudgementMinTargetHealthPct - 1));
    CHECK(IsJudgementUpkeepTarget(true, true, true, kJudgementMinTargetHealthPct));
}

static void TestAffordable()
{
    // Non-healer: medium mana is the line.
    CHECK(!JudgementUpkeepAffordable(false, 39, 40, false));
    CHECK(JudgementUpkeepAffordable(false, 40, 40, false));
    // A non-healer ignores party health (it has no heals to give).
    CHECK(JudgementUpkeepAffordable(false, 80, 40, true));
    // Healer: higher mana line.
    CHECK(!JudgementUpkeepAffordable(true, 49, 40, false));
    CHECK(JudgementUpkeepAffordable(true, 50, 40, false));
    // Healer never judges while someone needs healing.
    CHECK(!JudgementUpkeepAffordable(true, 100, 40, true));
    // A configured medium-mana line above the healer line still wins.
    CHECK(!JudgementUpkeepAffordable(true, 55, 60, false));
}

static void TestRefresh()
{
    CHECK(JudgementNeedsRefresh(false, 0));
    CHECK(JudgementNeedsRefresh(false, 9000)); // absent beats whatever the remaining reads
    CHECK(JudgementNeedsRefresh(true, 0));
    CHECK(JudgementNeedsRefresh(true, kJudgementRefreshWindowMs));
    CHECK(!JudgementNeedsRefresh(true, kJudgementRefreshWindowMs + 1));
    CHECK(!JudgementNeedsRefresh(true, 10000));
}

static void TestSealAndJudge()
{
    const JudgementKind w = JudgementKind::Wisdom;
    // Not an owner: never.
    CHECK(!ShouldCastUpkeepSeal(JudgementKind::None, false, false, true, true));
    CHECK(!ShouldCastUpkeepJudgement(JudgementKind::None, true, true));

    // Empty slot: fill it with the owner's Seal, debuff fresh or not, Judgement ready or not.
    CHECK(ShouldCastUpkeepSeal(w, false, false, false, false));
    CHECK(ShouldCastUpkeepSeal(w, false, false, true, true));
    // The owner's Seal is already up: nothing to cast.
    CHECK(!ShouldCastUpkeepSeal(w, true, true, true, true));
    // A different Seal is up: swap only at refresh time with Judgement ready.
    CHECK(ShouldCastUpkeepSeal(w, true, false, true, true));
    CHECK(!ShouldCastUpkeepSeal(w, true, false, true, false));   // on cooldown: keep swinging
    CHECK(!ShouldCastUpkeepSeal(w, true, false, false, true));   // debuff still fresh: no swap

    // Judge only with the owner's Seal up and a debuff that needs it.
    CHECK(ShouldCastUpkeepJudgement(w, true, true));
    CHECK(!ShouldCastUpkeepJudgement(w, false, true));
    CHECK(!ShouldCastUpkeepJudgement(w, true, false));
}

// One 10 s cycle end to end for a Retribution owner: the Seal swap and the
// Judgement come in that order, once, and the next cycle starts clean.
static void TestCycle()
{
    const JudgementKind kind = JudgementKind::Wisdom;
    bool wantedSealUp = false;   // Command is up
    bool anySealUp = true;
    int remaining = 3000;        // debuff running out
    bool ready = true;           // Judgement off cooldown

    bool needs = JudgementNeedsRefresh(true, remaining);
    CHECK(needs);
    CHECK(!ShouldCastUpkeepJudgement(kind, wantedSealUp, needs));
    CHECK(ShouldCastUpkeepSeal(kind, anySealUp, wantedSealUp, needs, ready));
    wantedSealUp = true;         // Seal of Wisdom cast
    CHECK(!ShouldCastUpkeepSeal(kind, anySealUp, wantedSealUp, needs, ready));
    CHECK(ShouldCastUpkeepJudgement(kind, wantedSealUp, needs));
    // Judged: debuff fresh, Seal spent, Judgement on cooldown.
    remaining = 10000;
    anySealUp = false;
    wantedSealUp = false;
    ready = false;
    needs = JudgementNeedsRefresh(true, remaining);
    CHECK(!needs);
    CHECK(!ShouldCastUpkeepJudgement(kind, wantedSealUp, needs));
    // The empty slot is refilled with the owner's Seal straight away (no wasted swap next cycle).
    CHECK(ShouldCastUpkeepSeal(kind, anySealUp, wantedSealUp, needs, ready));
}

int main()
{
    TestOwnership();
    TestTankNeverOwns();
    TestTankDungeonWisdom();
    TestProtWantedSeal();
    TestSealAvailability();
    TestTarget();
    TestAffordable();
    TestRefresh();
    TestSealAndJudge();
    TestCycle();
    std::printf("judgement upkeep policy: all checks passed\n");
    return 0;
}
