// Standalone check for ai/playerbot/SeekWalkoutPolicy.h (Hakkar's Son walk-out pull):
//   g++ -std=c++17 -Wall -Wextra tools/test_seek_walkout_policy.cpp -o /tmp/test_seek_walkout_policy && /tmp/test_seek_walkout_policy
#include "../ai/playerbot/SeekWalkoutPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
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
    c.topThreat = true;
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
    threatHunter.topThreat = true;
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
    // A Son 78 yd west of the bot: the stand is 28 yd from the Son, on the line toward the bot.
    float x = 0.0f, y = 0.0f;
    WalkoutShotPoint(-78.0f, 0.0f, 0.0f, 0.0f, 28.0f, x, y);
    CHECK(Near(x, -50.0f) && Near(y, 0.0f));
    // Already within range: the bot shoots from where it stands.
    WalkoutShotPoint(-20.0f, 0.0f, 0.0f, 0.0f, 28.0f, x, y);
    CHECK(Near(x, 0.0f) && Near(y, 0.0f));
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
    CHECK(WalkoutPullerUsable(true, false, false, false, false));
    CHECK(!WalkoutPullerUsable(false, false, false, false, false));
    CHECK(!WalkoutPullerUsable(true, true, false, false, false));
    CHECK(!WalkoutPullerUsable(true, false, true, false, false));
    CHECK(!WalkoutPullerUsable(true, false, false, true, false));
    // Entering the boss's top two threats aborts the task at once, not only becoming his victim.
    CHECK(!WalkoutPullerUsable(true, false, false, false, true));
}

static void TestCastFailAbortUnspends()
{
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3);
    CHECK(!WalkoutMayStart(s, 3, 200));
    // The shot cannot be cast at the stand: abort, and neither the Son nor the cycle counts as spent.
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
    WalkoutStart(t, 7, 300, 3000, 4);
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
    WalkoutSavePetStance(held, 7, 1, 0);   // defensive, stay
    WalkoutSavePetStance(held, 7, 0, 1);   // already held passive/follow: the first stance is kept
    CHECK(held.size() == 1);
    WalkoutPetStance stance;
    CHECK(WalkoutTakePetStance(held, 7, stance));
    CHECK(stance.react == 1 && stance.command == 0);
    CHECK(!WalkoutTakePetStance(held, 7, stance));
    CHECK(held.empty());
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

int main()
{
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
    std::cout << "seek walk-out policy: all checks passed\n";
    return 0;
}
