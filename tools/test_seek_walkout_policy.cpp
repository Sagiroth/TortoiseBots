// Standalone check for ai/playerbot/SeekWalkoutPolicy.h (Hakkar's Son walk-out pull):
//   g++ -std=c++17 -Wall -Wextra tools/test_seek_walkout_policy.cpp -o /tmp/test_seek_walkout_policy && /tmp/test_seek_walkout_policy
#include "../ai/playerbot/SeekWalkoutPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static WalkoutCandidate Cand(std::uint64_t guid, WalkoutRole role, float yd)
{
    WalkoutCandidate c;
    c.guid = guid;
    c.role = role;
    c.alive = true;
    c.yd = yd;
    return c;
}

static void TestRoleOrder()
{
    CHECK(WalkoutRoleOf(true, false, true) == WalkoutRole::Hunter);
    CHECK(WalkoutRoleOf(false, true, true) == WalkoutRole::Caster);
    CHECK(WalkoutRoleOf(false, false, true) == WalkoutRole::Ranged);
    CHECK(WalkoutRoleOf(false, false, false) == WalkoutRole::None);
    // A hunter beats a nearer mage, a mage beats a nearer other ranged damage dealer.
    std::vector<WalkoutCandidate> raid = {Cand(1, WalkoutRole::Ranged, 2.0f), Cand(2, WalkoutRole::Caster, 5.0f),
                                          Cand(3, WalkoutRole::Hunter, 20.0f)};
    CHECK(PickWalkoutPuller(raid) == 2);
    raid.pop_back();
    CHECK(PickWalkoutPuller(raid) == 1);
    // Same role: the nearest, then the lowest guid.
    std::vector<WalkoutCandidate> two = {Cand(9, WalkoutRole::Hunter, 10.0f), Cand(4, WalkoutRole::Hunter, 8.0f)};
    CHECK(PickWalkoutPuller(two) == 1);
    two[1].yd = 10.0f;
    CHECK(PickWalkoutPuller(two) == 1);
    CHECK(PickWalkoutPuller({}) == -1);
}

static void TestExclusions()
{
    const WalkoutCandidate ok = Cand(1, WalkoutRole::Hunter, 5.0f);
    CHECK(WalkoutEligible(ok));
    WalkoutCandidate c = ok;
    c.tank = true;
    CHECK(!WalkoutEligible(c));
    c = ok;
    c.healer = true;
    CHECK(!WalkoutEligible(c));
    c = ok;
    c.bossVictim = true;
    CHECK(!WalkoutEligible(c));
    c = ok;
    c.highThreat = true;
    CHECK(!WalkoutEligible(c));
    c = ok;
    c.charmed = true;
    CHECK(!WalkoutEligible(c));
    c = ok;
    c.insane = true;
    CHECK(!WalkoutEligible(c));
    c = ok;
    c.alive = false;
    CHECK(!WalkoutEligible(c));
    c = ok;
    c.role = WalkoutRole::None;
    CHECK(!WalkoutEligible(c));
    // The excluded hunter gives way to the eligible mage.
    WalkoutCandidate threatHunter = ok;
    threatHunter.highThreat = true;
    std::vector<WalkoutCandidate> raid = {threatHunter, Cand(2, WalkoutRole::Caster, 30.0f)};
    CHECK(PickWalkoutPuller(raid) == 1);
    raid[1].insane = true;
    CHECK(PickWalkoutPuller(raid) == -1);
}

static void TestExemptOnlyWhileActive()
{
    SeekWalkoutState s;
    CHECK(!WalkoutExempts(s, 7, 1000));
    CHECK(WalkoutMayStart(s, 0, 100));
    WalkoutStart(s, 7, 100, 1000, 0);
    CHECK(WalkoutActive(s));
    CHECK(WalkoutExempts(s, 7, 1000));
    CHECK(WalkoutExempts(s, 7, 1000 + kSeekWalkoutTimeoutMs - 1));
    // Only the puller.
    CHECK(!WalkoutExempts(s, 8, 1000));
    // Past the timeout the exemption ends even before anyone closes the task.
    CHECK(!WalkoutExempts(s, 7, 1000 + kSeekWalkoutTimeoutMs));
    WalkoutFinish(s, WalkoutEnd::Back, 2000);
    CHECK(!WalkoutActive(s));
    CHECK(!WalkoutExempts(s, 7, 2000));
}

static void TestEndsAndLimits()
{
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3);
    // One puller at a time.
    CHECK(!WalkoutMayStart(s, 4, 200));
    // Still out: no end.
    CHECK(WalkoutCheck(s, 5000, true, true, false) == WalkoutEnd::None);
    // Back inside before the shot is no end: the pull has not happened.
    CHECK(WalkoutCheck(s, 5000, true, true, true) == WalkoutEnd::None);
    // The Son gone before the shot, or the puller unusable: abort.
    CHECK(WalkoutCheck(s, 5000, true, false, false) == WalkoutEnd::Abort);
    CHECK(WalkoutCheck(s, 5000, false, true, false) == WalkoutEnd::Abort);
    // The shot landed: back once inside, even with the Son dead by then.
    WalkoutNoteShot(s);
    CHECK(WalkoutCheck(s, 6000, true, false, false) == WalkoutEnd::None);
    CHECK(WalkoutCheck(s, 6000, true, false, true) == WalkoutEnd::Back);
    // The 25 s timeout.
    CHECK(WalkoutCheck(s, 1000 + kSeekWalkoutTimeoutMs, true, true, false) == WalkoutEnd::Timeout);
    CHECK(std::string(WalkoutEndName(WalkoutEnd::Back)) == "back");
    CHECK(std::string(WalkoutEndName(WalkoutEnd::Timeout)) == "timeout");
    CHECK(std::string(WalkoutEndName(WalkoutEnd::Abort)) == "abort");
    WalkoutFinish(s, WalkoutEnd::Back, 7000);
    // One pull per seek cycle: cycle 3 is spent, cycle 4 may start.
    CHECK(!WalkoutMayStart(s, 3, 200));
    CHECK(WalkoutMayStart(s, 4, 200));
    // One walk-out per Son: Son 100 is spent whatever the cycle.
    CHECK(!WalkoutMayStart(s, 4, 100));
    WalkoutStart(s, 8, 200, 99000, 4);
    WalkoutFinish(s, WalkoutEnd::Back, 99500);
    CHECK(!WalkoutMayStart(s, 5, 200));
    CHECK(WalkoutMayStart(s, 5, 300));
}

static void TestShotAndBackPoints()
{
    CHECK(Near(WalkoutShotRangeYd(true), 33.0f));
    CHECK(Near(WalkoutShotRangeYd(false), 28.0f));
    float x = 0.0f, y = 0.0f;
    // The walk back goes toward the centre to the tanks' radius minus the inset (25 - 5 = 20 yd), so the Son that
    // follows comes inside the tanks' circle.
    BossAnchorCircle tank;
    tank.x = 100.0f;
    tank.y = 100.0f;
    tank.z = 54.0f;
    tank.radius = 25.0f;
    WalkoutBackPoint(tank, 100.0f - 60.0f, 100.0f, x, y);
    CHECK(Near(x, 80.0f) && Near(y, 100.0f));
    CHECK(WalkoutBackArrived(tank, 81.0f, 100.0f));
    CHECK(!WalkoutBackArrived(tank, 70.0f, 100.0f));
    // A puller at the centre goes nowhere.
    WalkoutBackPoint(tank, 100.0f, 100.0f, x, y);
    CHECK(Near(x, 100.0f) && Near(y, 100.0f));
}

static void TestShotSpellPerClass()
{
    // Hunter: Auto Shot 75; mage, warlock, priest: the wand's Shoot 5019; druid: Wrath 5176; shaman: Lightning Bolt
    // 403. Anyone else has no shot.
    CHECK(std::string(WalkoutShotSpell(3)) == "auto shot");
    CHECK(std::string(WalkoutShotSpell(8)) == "shoot");
    CHECK(std::string(WalkoutShotSpell(9)) == "shoot");
    CHECK(std::string(WalkoutShotSpell(5)) == "shoot");
    CHECK(std::string(WalkoutShotSpell(11)) == "wrath");
    CHECK(std::string(WalkoutShotSpell(7)) == "lightning bolt");
    CHECK(std::string(WalkoutShotSpell(1)).empty());
    CHECK(std::string(WalkoutShotSpell(4)).empty());
    // A bot that cannot fire now (no wand, no ammo, out of mana, silenced, the cast on cooldown) is no puller.
    WalkoutCandidate c = Cand(1, WalkoutRole::Hunter, 5.0f);
    c.canShoot = false;
    CHECK(!WalkoutEligible(c));
    std::vector<WalkoutCandidate> raid = {c, Cand(2, WalkoutRole::Ranged, 40.0f)};
    CHECK(PickWalkoutPuller(raid) == 1);
}

static void TestPullerUsable()
{
    CHECK(WalkoutPullerUsable(true, false, false, false));
    CHECK(!WalkoutPullerUsable(false, false, false, false));
    CHECK(!WalkoutPullerUsable(true, true, false, false));
    CHECK(!WalkoutPullerUsable(true, false, true, false));
    // Becoming his victim is the danger; threat alone no longer aborts a running task.
    CHECK(!WalkoutPullerUsable(true, false, false, true));
}

static void TestCastFailAbortUnspends()
{
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 60000);
    CHECK(!WalkoutMayStart(s, 3, 200));
    // The shot cannot be cast at the stand with 59 s to the siphon: abort, and neither the Son nor the cycle counts
    // as spent.
    WalkoutAbortUnspent(s, 2000);
    CHECK(!WalkoutActive(s));
    CHECK(WalkoutReturning(s, 2000));
    WalkoutReturnDone(s);
    CHECK(WalkoutMayStart(s, 3, 100));
    // A cycle spent before the task stays spent (the unspend only undoes this task's own marks).
    SeekWalkoutState t;
    WalkoutStart(t, 7, 100, 1000, 3);
    WalkoutFinish(t, WalkoutEnd::Timeout, 2000);
    WalkoutReturnDone(t);
    WalkoutStart(t, 7, 300, 3000, 4, 60000);
    WalkoutAbortUnspent(t, 4000);
    WalkoutReturnDone(t);
    CHECK(!WalkoutMayStart(t, 3, 300)); // cycle 3 was the timed-out task's
    CHECK(WalkoutMayStart(t, 4, 300));
    CHECK(!WalkoutMayStart(t, 4, 100)); // Son 100 was walked to by the timed-out task
}

static void TestWalkBackAfterAnyEnd()
{
    // Timeout and abort leave the puller outside: it stays exempt for a walk back of at most 15 s, then the clamp
    // holds it again. A Back end is already inside: no walk back.
    for (WalkoutEnd end : {WalkoutEnd::Timeout, WalkoutEnd::Abort})
    {
        SeekWalkoutState s;
        WalkoutStart(s, 7, 100, 1000, 0);
        WalkoutFinish(s, end, 26000);
        CHECK(!WalkoutActive(s));
        CHECK(WalkoutBusy(s));
        CHECK(WalkoutReturning(s, 26000));
        CHECK(WalkoutExempts(s, 7, 26000));
        CHECK(WalkoutExempts(s, 7, 26000 + kSeekWalkoutReturnMs - 1));
        CHECK(!WalkoutExempts(s, 8, 26000));
        CHECK(!WalkoutExempts(s, 7, 26000 + kSeekWalkoutReturnMs));
        CHECK(!WalkoutReturning(s, 26000 + kSeekWalkoutReturnMs));
        // No new task while the walk back runs.
        CHECK(!WalkoutMayStart(s, 1, 200));
        WalkoutReturnDone(s);
        CHECK(!WalkoutBusy(s));
        CHECK(!WalkoutExempts(s, 7, 27000));
        CHECK(WalkoutMayStart(s, 1, 200));
    }
    SeekWalkoutState back;
    WalkoutStart(back, 7, 100, 1000, 0);
    WalkoutNoteShot(back);
    WalkoutFinish(back, WalkoutEnd::Back, 9000);
    CHECK(!WalkoutBusy(back));
    CHECK(!WalkoutExempts(back, 7, 9000));
}

static void TestPetStanceSavedOnce()
{
    // The stance is the pet's react state AND its command (stay/follow), saved once per puller, handed back once.
    std::unordered_map<std::uint64_t, WalkoutPetStance> held;
    WalkoutSavePetStance(held, 7, 70, 1, 0, 1000);   // pet 70: defensive, stay
    WalkoutSavePetStance(held, 7, 70, 0, 1, 2000);   // already held passive/follow: the first stance is kept
    CHECK(held.size() == 1);
    WalkoutPetStance stance;
    CHECK(WalkoutTakePetStance(held, 7, stance));
    CHECK(stance.react == 1 && stance.command == 0 && stance.petGuid == 70 && stance.savedMs == 1000);
    CHECK(!WalkoutTakePetStance(held, 7, stance));
    CHECK(held.empty());
}

static void TestPetStanceOnlyForTheHeldPet()
{
    // The stance is applied only to the pet it was taken from: a pet dismissed and summoned again, or a relog's new
    // pet, has another guid and the entry is dropped unapplied.
    WalkoutPetStance stance;
    stance.petGuid = 70;
    CHECK(WalkoutPetStanceApplies(stance, 70));
    CHECK(!WalkoutPetStanceApplies(stance, 71));
    CHECK(!WalkoutPetStanceApplies(stance, 0));
}

static void TestPetHoldsAgeOut()
{
    // A puller that logs out or is dismissed mid-hold never runs its own release: any bot's next release drops every
    // entry older than kSeekWalkoutPetHoldMs, so the hold count goes back to 0.
    std::unordered_map<std::uint64_t, WalkoutPetStance> held;
    WalkoutSavePetStance(held, 7, 70, 1, 0, 1000);
    WalkoutSavePetStance(held, 8, 80, 1, 1, 50000);
    CHECK(WalkoutSweepPetHolds(held, 1000 + kSeekWalkoutPetHoldMs - 1) == 0);
    CHECK(held.size() == 2);
    CHECK(WalkoutSweepPetHolds(held, 1000 + kSeekWalkoutPetHoldMs) == 1);
    CHECK(held.size() == 1 && held.count(8) == 1);
    CHECK(WalkoutSweepPetHolds(held, 50000 + kSeekWalkoutPetHoldMs) == 1);
    CHECK(held.empty());
    // A hold lasts at most the task and its walk back, well inside the age-out.
    CHECK(kSeekWalkoutTimeoutMs + kSeekWalkoutReturnMs < kSeekWalkoutPetHoldMs);
}

static void TestPetReleasedOnEveryEnd()
{
    // The pet is held while its bot runs the task or its walk back, and released on the bot's next update after any
    // end: a Back end, the walk back done, or the record dropped (the boss died or left the fight, a stale sweep).
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 0);
    CHECK(!WalkoutPetRelease(&s, 7));
    CHECK(WalkoutPetRelease(&s, 8));
    // Boss death mid-task: the record is dropped, nothing holds the pet any more.
    CHECK(WalkoutPetRelease(nullptr, 7));
    // A timeout: held through the walk back, released once it is done.
    WalkoutFinish(s, WalkoutEnd::Timeout, 26000);
    CHECK(!WalkoutPetRelease(&s, 7));
    WalkoutReturnDone(s);
    CHECK(WalkoutPetRelease(&s, 7));
    // A Back end: released at once.
    WalkoutStart(s, 7, 200, 30000, 1);
    WalkoutNoteShot(s);
    WalkoutFinish(s, WalkoutEnd::Back, 35000);
    CHECK(WalkoutPetRelease(&s, 7));
}

static void TestStandSearchStaysOnThePlatform()
{
    // Candidates run from the anchor's edge toward the Son in 2 yd steps.
    BossAnchorCircle c;
    c.x = 0.0f;
    c.y = 0.0f;
    c.z = 54.0f;
    c.radius = 30.0f;
    const std::vector<std::pair<float, float>> pts = WalkoutStandCandidates(c, -78.0f, 0.0f);
    CHECK(!pts.empty());
    CHECK(Near(pts.front().first, -30.0f) && Near(pts.front().second, 0.0f));
    CHECK(Near(pts[1].first, -32.0f));
    CHECK(pts.back().first > -78.0f);
    // A stand must have a floor at or above 46.5 (the evade line 45.8 plus margin), be within range and see the Son.
    CHECK(Near(kSeekWalkoutMinStandZ, 46.5f));
    CHECK(WalkoutStandOk(true, 47.0f, 27.0f, 28.0f, true));
    CHECK(!WalkoutStandOk(true, 37.0f, 10.0f, 28.0f, true));   // down on the slope: the walk-out pullers died there
    CHECK(!WalkoutStandOk(true, 46.4f, 10.0f, 28.0f, true));
    CHECK(!WalkoutStandOk(false, 50.0f, 10.0f, 28.0f, true));
    CHECK(!WalkoutStandOk(true, 50.0f, 29.0f, 28.0f, true));
    CHECK(!WalkoutStandOk(true, 50.0f, 10.0f, 28.0f, false));
    // The first candidate that passes is the stand; none passing is no start.
    int index = -1;
    const std::vector<float> floors = {54.0f, 52.0f, 49.0f, 47.0f, 44.0f};
    auto const ok = [&](std::size_t i) { return i < floors.size() && WalkoutStandOk(true, floors[i], 40.0f - 4.0f * i, 28.0f, true); };
    for (std::size_t i = 0; i < floors.size(); ++i)
        if (ok(i)) { index = static_cast<int>(i); break; }
    CHECK(index == 3);  // 40 - 12 = 28 yd at z 47
    // A Son already within the circle has no candidates: the anchored shot is its pull.
    CHECK(WalkoutStandCandidates(c, 10.0f, 0.0f).empty());
}

static void TestThreatRule()
{
    // Only the victim, or a threat at 90 % of the main tank's or more: a damage dealer is nearly always in the top two
    // when the off-tanks hold little threat.
    CHECK(!WalkoutThreatTooHigh(600.0f, 1000.0f));
    CHECK(WalkoutThreatTooHigh(700.0f, 1000.0f));
    CHECK(WalkoutThreatTooHigh(1200.0f, 1000.0f));
    CHECK(!WalkoutThreatTooHigh(500.0f, 0.0f));   // no main-tank reference: the victim rule alone
    CHECK(WalkoutPullerProblem(true, false, false, false) == nullptr);
    CHECK(std::string(WalkoutPullerProblem(false, false, false, false)) == "dead");
    CHECK(std::string(WalkoutPullerProblem(true, true, false, false)) == "charmed");
    CHECK(std::string(WalkoutPullerProblem(true, false, true, false)) == "insanity");
    CHECK(std::string(WalkoutPullerProblem(true, false, false, true)) == "victim");
}

// t1105h review (Codex HIGH): a running task aborts on threat again, at 1.0 x the live top tank's threat (the highest
// tank on the boss's list this tick, never a cached main tank). The 0.7 pick margin and the victim abort stay.
static void TestRunningThreatAbort()
{
    // Growth during the task: below the top tank goes on, level with it aborts with reason `threat`.
    std::vector<float> tanks{1000.0f, 400.0f};
    CHECK(Near(WalkoutTopTankThreat(tanks), 1000.0f));
    CHECK(!WalkoutRunningThreatTooHigh(800.0f, WalkoutTopTankThreat(tanks)));
    CHECK(WalkoutPullerProblem(true, false, false, false, WalkoutRunningThreatTooHigh(800.0f, 1000.0f)) == nullptr);
    CHECK(WalkoutRunningThreatTooHigh(1000.0f, WalkoutTopTankThreat(tanks)));
    CHECK(std::string(WalkoutPullerProblem(true, false, false, false,
        WalkoutRunningThreatTooHigh(1000.0f, 1000.0f))) == "threat");
    // The pick margin is still 0.7: 800 of 1000 is never picked, though it runs on.
    CHECK(WalkoutThreatTooHigh(800.0f, 1000.0f));
    // A main-tank swap: the old main tank (index 0) dropped to 500 and the off-tank holds 1200. The bot at 1000 compares
    // against the new top tank and goes on; against the old main tank it would have aborted.
    std::vector<float> swapped{500.0f, 1200.0f};
    CHECK(Near(WalkoutTopTankThreat(swapped), 1200.0f));
    CHECK(!WalkoutRunningThreatTooHigh(1000.0f, WalkoutTopTankThreat(swapped)));
    CHECK(WalkoutRunningThreatTooHigh(1000.0f, swapped[0]));
    // No living tank on the list: only the victim rule speaks.
    CHECK(Near(WalkoutTopTankThreat({}), 0.0f));
    CHECK(!WalkoutRunningThreatTooHigh(5000.0f, 0.0f));
    // The other reasons come first: dead, charmed, insanity and victim keep their words.
    CHECK(std::string(WalkoutPullerProblem(true, false, false, true, true)) == "victim");
    CHECK(std::string(WalkoutPullerProblem(false, false, false, false, true)) == "dead");
}

static void TestPullerInsideWithAPath()
{
    // Only a bot inside the anchor with a navmesh path to the stand (the hunter on the 51 yd ledge never moved).
    WalkoutCandidate c = Cand(1, WalkoutRole::Hunter, 5.0f);
    c.inside = false;
    CHECK(!WalkoutEligible(c));
    c.inside = true;
    c.pathOk = false;
    CHECK(!WalkoutEligible(c));
    // The ranking hands the eligible in order, so the path is asked of the best first and the next one takes over.
    std::vector<WalkoutCandidate> raid = {Cand(1, WalkoutRole::Ranged, 2.0f), Cand(2, WalkoutRole::Hunter, 9.0f),
                                          Cand(3, WalkoutRole::Caster, 4.0f)};
    const std::vector<int> order = RankWalkoutPullers(raid);
    CHECK(order.size() == 3 && order[0] == 1 && order[1] == 2 && order[2] == 0);
}

static void TestMoveFailsAbort()
{
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 0);
    CHECK(!WalkoutNoteMove(s, false));
    CHECK(!WalkoutNoteMove(s, true));   // a good move resets the count
    CHECK(!WalkoutNoteMove(s, false));
    CHECK(!WalkoutNoteMove(s, false));
    CHECK(WalkoutNoteMove(s, false));   // three failed moves in a row: abort ("move failed")
}

static void TestMoveFailAbortUnspendsAndExcludes()
{
    // Two refused moves abort, unspent like a cast fail: no pull happened, so the Son and the cycle stay free, and the
    // puller is left out of the picks for 60 s so another bot takes the next one.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 60000);
    WalkoutNoteMove(s, false);
    WalkoutNoteMove(s, false);
    CHECK(WalkoutNoteMove(s, false));
    WalkoutAbortUnspent(s, 2000);
    WalkoutExclude(s, 7, 2000);
    WalkoutReturnDone(s);
    CHECK(WalkoutMayStart(s, 3, 100));
    CHECK(WalkoutExcluded(s, 7, 2000));
    CHECK(WalkoutExcluded(s, 7, 2000 + kSeekWalkoutExcludeMs - 1));
    CHECK(!WalkoutExcluded(s, 7, 2000 + kSeekWalkoutExcludeMs));
    CHECK(!WalkoutExcluded(s, 8, 2000));
    WalkoutCandidate c = Cand(7, WalkoutRole::Hunter, 5.0f);
    c.excluded = true;
    CHECK(!WalkoutEligible(c));
    std::vector<WalkoutCandidate> raid = {c, Cand(8, WalkoutRole::Caster, 20.0f)};
    CHECK(PickWalkoutPuller(raid) == 1);
}

static void TestSonsNearestFirstWithAStand()
{
    // The nearest Son without a stand does not block the others: the first, nearest first, that has one is taken.
    const std::vector<float> yd = {60.0f, 40.0f, 80.0f};
    std::vector<int> asked;
    auto const standFor = [&](int i) { asked.push_back(i); return i == 0; };
    CHECK(PickWalkoutSon(yd, standFor) == 0);
    CHECK(asked.size() == 2 && asked[0] == 1 && asked[1] == 0);
    CHECK(PickWalkoutSon(yd, [](int) { return false; }) == -1);
    CHECK(PickWalkoutSon({}, [](int) { return true; }) == -1);
}

static void TestExcludedPullerNeverRestarts()
{
    // Two quick move failures, an immediate return inside, and a start while the scan still names the same bot: the
    // start check under the lock refuses the excluded bot; another bot may start.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 60000);
    WalkoutNoteMove(s, false);
    WalkoutNoteMove(s, false);
    WalkoutAbortUnspent(s, 1500);
    WalkoutExclude(s, 7, 1500);
    WalkoutReturnDone(s);
    CHECK(WalkoutMayStart(s, 3, 100));
    CHECK(!WalkoutMayStartFor(s, 3, 100, 7, 1600, 0));
    CHECK(WalkoutMayStartFor(s, 3, 100, 8, 1600, 0));
    CHECK(WalkoutMayStartFor(s, 3, 100, 7, 1500 + kSeekWalkoutExcludeMs, 0));
}

static void TestWalkoutReplansStandWhenSonMoves()
{
    // The stand follows the Son's live position: re-planned every second, the newest one walked to; only 5 s straight
    // without any stand aborts ("no stand").
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 0);
    s.standX = 1.0f;
    s.standY = 2.0f;
    s.standZ = 50.0f;
    s.standMs = 1000;
    CHECK(!WalkoutReplanDue(s, 1999));
    CHECK(WalkoutReplanDue(s, 2000));
    CHECK(!WalkoutNoteStand(s, true, 5.0f, 6.0f, 51.0f, 2000));
    CHECK(Near(s.standX, 5.0f) && Near(s.standY, 6.0f) && Near(s.standZ, 51.0f));
    CHECK(!WalkoutReplanDue(s, 2500));
    // The Son walked out of every stand's reach: the old stand is kept a while.
    CHECK(!WalkoutNoteStand(s, false, 0.0f, 0.0f, 0.0f, 3000));
    CHECK(Near(s.standX, 5.0f));
    CHECK(!WalkoutNoteStand(s, false, 0.0f, 0.0f, 0.0f, 7999));
    // It came back: the clock restarts.
    CHECK(!WalkoutNoteStand(s, true, 8.0f, 9.0f, 52.0f, 8000));
    CHECK(!WalkoutNoteStand(s, false, 0.0f, 0.0f, 0.0f, 9000));
    CHECK(!WalkoutNoteStand(s, false, 0.0f, 0.0f, 0.0f, 13999));
    CHECK(WalkoutNoteStand(s, false, 0.0f, 0.0f, 0.0f, 14000));
}

static void TestWalkoutPickMarginBelowAbort()
{
    // Picked only below 0.7 of the main tank's threat; there is no threat abort any more (only the victim one).
    CHECK(Near(kSeekWalkoutThreatShare, 0.7f));
    WalkoutCandidate c = Cand(1, WalkoutRole::Hunter, 5.0f);
    c.highThreat = WalkoutThreatTooHigh(0.69f * 1000.0f, 1000.0f);
    CHECK(WalkoutEligible(c));
    c.highThreat = WalkoutThreatTooHigh(0.71f * 1000.0f, 1000.0f);
    CHECK(!WalkoutEligible(c));
    CHECK(WalkoutPullerUsable(true, false, false, false));
}

static void TestWalkoutWaitsForTheSiphon()
{
    // A walk-out starts once the next Blood Siphon is due in 75 s or less: as soon as the dose window opens, so the
    // Son is back and burned well before the siphon (the 12 s non-tank burn keeps it off the healers).
    SeekWalkoutState s;
    CHECK(Near(static_cast<float>(kSeekWalkoutSiphonLeadMs), 75000.0f));
    CHECK(!WalkoutMayStartFor(s, 0, 100, 7, 1000, 75001));
    CHECK(WalkoutMayStartFor(s, 0, 100, 7, 1000, 75000));
    CHECK(WalkoutMayStartFor(s, 0, 100, 7, 1000, 70000));
    CHECK(WalkoutMayStartFor(s, 0, 100, 7, 1000, 0));
}

static void TestWalkoutTimeoutLeavesCycleOpen()
{
    // A timeout with 40 s left to the siphon: the cycle stays open for ONE more walk-out, to another Son (the timed-out
    // one stays walked).
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 65000); // the siphon is due at 66000
    CHECK(WalkoutFinish(s, WalkoutEnd::Timeout, 26000));
    WalkoutReturnDone(s);
    CHECK(WalkoutCycleOpen(s, 3));
    CHECK(WalkoutMayStart(s, 3, 200));
    CHECK(!WalkoutMayStart(s, 3, 100));
    // The second one times out too, with plenty left: no third walk-out in this cycle, ever.
    SeekWalkoutState t;
    WalkoutStart(t, 7, 100, 1000, 3, 89000);
    CHECK(WalkoutFinish(t, WalkoutEnd::Timeout, 26000));
    WalkoutReturnDone(t);
    WalkoutStart(t, 8, 200, 27000, 3, 63000);
    CHECK(!WalkoutFinish(t, WalkoutEnd::Timeout, 28000));
    WalkoutReturnDone(t);
    CHECK(!WalkoutCycleOpen(t, 3));
    CHECK(!WalkoutMayStart(t, 3, 300));
    // The next cycle starts its own count.
    CHECK(WalkoutCycleOpen(t, 4));
    CHECK(WalkoutMayStart(t, 4, 300));
}

static void TestWalkoutTimeoutLateSpendsCycle()
{
    // A timeout with 20 s left: a second Son could not be brought and burned before the siphon, so the cycle is spent.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 45000); // due at 46000
    CHECK(!WalkoutFinish(s, WalkoutEnd::Timeout, 26000));
    WalkoutReturnDone(s);
    CHECK(!WalkoutMayStart(s, 3, 200));
    // Exactly 25 s left is not more than 25 s.
    SeekWalkoutState e;
    WalkoutStart(e, 7, 100, 1000, 3, 50000); // due at 51000
    CHECK(!WalkoutFinish(e, WalkoutEnd::Timeout, 26000));
    // A Back end is the cycle's Son: never reopened. An abort (puller dead, the boss's victim) neither.
    SeekWalkoutState b;
    WalkoutStart(b, 7, 100, 1000, 3, 89000);
    WalkoutNoteShot(b);
    CHECK(!WalkoutFinish(b, WalkoutEnd::Back, 20000));
    CHECK(!WalkoutMayStart(b, 3, 200));
    SeekWalkoutState a;
    WalkoutStart(a, 7, 100, 1000, 3, 89000);
    CHECK(!WalkoutFinish(a, WalkoutEnd::Abort, 20000));
    WalkoutReturnDone(a);
    CHECK(!WalkoutMayStart(a, 3, 200));
}

static void TestWalkoutUnspentRetryNeedsTimeAndCap()
{
    // A failed cast or "no stand" frees the Son; the cycle reopens only with more than 25 s left and fewer than two
    // walk-outs started in it.
    SeekWalkoutState late;
    WalkoutStart(late, 7, 100, 1000, 3, 21000); // due at 22000
    WalkoutAbortUnspent(late, 2000);
    WalkoutReturnDone(late);
    CHECK(!WalkoutMayStart(late, 3, 100));
    CHECK(WalkoutMayStart(late, 4, 100)); // the Son itself is free again
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 89000);
    CHECK(WalkoutFinish(s, WalkoutEnd::Timeout, 26000));
    WalkoutReturnDone(s);
    WalkoutStart(s, 8, 200, 27000, 3, 63000);
    WalkoutAbortUnspent(s, 28000); // 62 s left, but the second walk-out of the cycle: a third never starts
    WalkoutReturnDone(s);
    CHECK(!WalkoutMayStart(s, 3, 200));
    CHECK(kSeekWalkoutMaxPerCycle == 2);
    CHECK(kSeekWalkoutRetryMinMs == 25000);
}

static void TestWalkoutForgetsDeadSons()
{
    // A walked Son that died is forgotten: once it respawns (49033/49034 come back after 60 s) it is the retry's Son.
    // A Son still alive stays walked; the running task's own Son is never forgotten.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 1, 70000);
    WalkoutNoteShot(s);
    WalkoutFinish(s, WalkoutEnd::Back, 15000);
    WalkoutStart(s, 7, 200, 100000, 2, 70000);
    WalkoutForgetGoneSons(s, [](std::uint64_t son) { return son == 100 || son == 200; });
    CHECK(s.walkedSons.size() == 1 && s.walkedSons[0] == 200);
    WalkoutNoteShot(s);
    WalkoutFinish(s, WalkoutEnd::Back, 110000);
    CHECK(WalkoutMayStart(s, 3, 100));
    WalkoutForgetGoneSons(s, [](std::uint64_t) { return false; });
    CHECK(!WalkoutMayStart(s, 3, 200));
}

int main()
{
    TestWalkoutTimeoutLeavesCycleOpen();
    TestWalkoutTimeoutLateSpendsCycle();
    TestWalkoutUnspentRetryNeedsTimeAndCap();
    TestWalkoutForgetsDeadSons();
    TestRoleOrder();
    TestExclusions();
    TestExemptOnlyWhileActive();
    TestEndsAndLimits();
    TestShotAndBackPoints();
    TestShotSpellPerClass();
    TestPullerUsable();
    TestCastFailAbortUnspends();
    TestWalkBackAfterAnyEnd();
    TestPetStanceSavedOnce();
    TestPetReleasedOnEveryEnd();
    TestPetStanceOnlyForTheHeldPet();
    TestPetHoldsAgeOut();
    TestStandSearchStaysOnThePlatform();
    TestThreatRule();
    TestRunningThreatAbort();
    TestPullerInsideWithAPath();
    TestMoveFailsAbort();
    TestMoveFailAbortUnspendsAndExcludes();
    TestSonsNearestFirstWithAStand();
    TestExcludedPullerNeverRestarts();
    TestWalkoutReplansStandWhenSonMoves();
    TestWalkoutPickMarginBelowAbort();
    TestWalkoutWaitsForTheSiphon();
    std::cout << "seek walk-out policy: all checks passed\n";
    return 0;
}
