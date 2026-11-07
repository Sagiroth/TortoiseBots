// Standalone check for ai/playerbot/SeekWalkoutPolicy.h (Hakkar's Son walk-out pull):
//   g++ -std=c++17 -Wall -Wextra tools/test_seek_walkout_policy.cpp -o /tmp/test_seek_walkout_policy && /tmp/test_seek_walkout_policy
#include "../ai/playerbot/SeekWalkoutPolicy.h"

#include <algorithm>
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

static void TestWalkoutRejectCodes()
{
    const WalkoutCandidate ok = Cand(1, WalkoutRole::Hunter, 5.0f);
    CHECK(WalkoutEligible(ok));
    // One candidate per code, in WalkoutEligible's order.
    WalkoutCandidate c = ok;
    c.alive = false;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Dead);
    c = ok;
    c.role = WalkoutRole::None;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::NoRole);
    c = ok;
    c.tank = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Tank);
    c = ok;
    c.healer = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Healer);
    c = ok;
    c.bossVictim = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Victim);
    c = ok;
    c.highThreat = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Threat);
    c = ok;
    c.charmed = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Cc);
    c = ok;
    c.insane = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Cc);
    c = ok;
    c.canShoot = false;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::NoShot);
    c = ok;
    c.inside = false;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Outside);
    c = ok;
    c.pathOk = false;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::NoPath);
    c = ok;
    c.excluded = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Bench);
    // Precedence: the first failing test wins, as in WalkoutEligible.
    c = ok;
    c.alive = false;
    c.tank = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Dead);
    c = ok;
    c.tank = true;
    c.highThreat = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Tank);
    c = ok;
    c.healer = true;
    c.bossVictim = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Healer);
    c = ok;
    c.bossVictim = true;
    c.highThreat = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Victim);
    c = ok;
    c.highThreat = true;
    c.charmed = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Threat);
    c = ok;
    c.insane = true;
    c.canShoot = false;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::Cc);
    c = ok;
    c.pathOk = false;
    c.excluded = true;
    CHECK(WalkoutRejectOf(c) == WalkoutReject::NoPath);
    // The picker's own codes (off-map, no AI, the hunter pass) and the last one are words, not candidate fields.
    CHECK(std::strcmp(WalkoutRejectWord(WalkoutReject::Away), "away") == 0);
    CHECK(std::strcmp(WalkoutRejectWord(WalkoutReject::NoAi), "no_ai") == 0);
    CHECK(std::strcmp(WalkoutRejectWord(WalkoutReject::HunterOnly), "hunter_only") == 0);
    CHECK(std::strcmp(WalkoutRejectWord(WalkoutReject::NoPath), "no_path") == 0);
}

static void TestWalkoutRejectText()
{
    WalkoutRejectTally t;
    t.members = 20;
    t.Add(WalkoutReject::Dead);
    t.Add(WalkoutReject::Dead);
    t.Add(WalkoutReject::Threat);
    t.Add(WalkoutReject::Threat);
    t.Add(WalkoutReject::Threat);
    t.Add(WalkoutReject::NoPath);
    t.top = 4200.0f;
    t.low = 3100.0f;
    CHECK(WalkoutRejectText(t) == "reason=dead:2|threat:3|no_path:1,members=20 top=4200 low=3100");
    // Enum order, not the order the counts were added in.
    t = WalkoutRejectTally();
    t.members = 3;
    t.Add(WalkoutReject::NoPath);
    t.Add(WalkoutReject::Away);
    t.Add(WalkoutReject::Dead);
    CHECK(WalkoutRejectText(t) == "reason=away:1|dead:1|no_path:1,members=3 top=-1 low=-1");
    // Every member passed: no code, and the count says how many were looked at.
    CHECK(WalkoutRejectText(WalkoutRejectTally()) == "reason=none,members=0 top=-1 low=-1");
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
    // The shot cannot be cast at the stand with 59 s to the siphon: abort, and the Son is free again for the next
    // cycle; the cycle's one walk-out stays spent (max 1 per cycle).
    WalkoutAbortUnspent(s, 2000);
    CHECK(!WalkoutActive(s));
    CHECK(WalkoutReturning(s, 2000));
    WalkoutReturnDone(s);
    CHECK(!WalkoutMayStart(s, 3, 100));
    CHECK(WalkoutMayStart(s, 4, 100));
    // A cycle spent before the task stays spent, and an abort frees only its own Son: the timed-out cycle 3's Son 100
    // stays walked.
    SeekWalkoutState t;
    WalkoutStart(t, 7, 100, 1000, 3);
    WalkoutFinish(t, WalkoutEnd::Timeout, 2000);
    WalkoutReturnDone(t);
    WalkoutStart(t, 7, 300, 3000, 4, 60000);
    WalkoutAbortUnspent(t, 4000);
    WalkoutReturnDone(t);
    CHECK(WalkoutMayStart(t, 3, 300)); // the aborted Son is free again for a cycle that is open
    CHECK(!WalkoutMayStart(t, 4, 300)); // cycle 4's one walk-out is spent
    CHECK(!WalkoutMayStart(t, 5, 100)); // Son 100 was walked to by the timed-out task
    CHECK(WalkoutMayStart(t, 5, 300));
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
    // Two refused moves abort: no pull happened, so the Son is free again for the next cycle (max 1 per cycle), and the
    // puller is left out of the picks for 60 s so another bot takes the next one.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 60000);
    WalkoutNoteMove(s, false);
    WalkoutNoteMove(s, false);
    CHECK(WalkoutNoteMove(s, false));
    WalkoutAbortUnspent(s, 2000);
    WalkoutExclude(s, 7, 2000);
    WalkoutReturnDone(s);
    CHECK(!WalkoutMayStart(s, 3, 100));
    CHECK(WalkoutMayStart(s, 4, 100));
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
    // failed cycle stays spent and the next one's start check under the lock refuses the excluded bot; another bot
    // may start.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 89000); // due at 90000: plenty of time left in the cycle
    WalkoutNoteMove(s, false);
    WalkoutNoteMove(s, false);
    WalkoutAbortUnspent(s, 1500);
    WalkoutExclude(s, 7, 1500);
    WalkoutReturnDone(s);
    CHECK(!WalkoutMayStart(s, 3, 100));
    CHECK(!WalkoutMayStartFor(s, 3, 100, 7, 1600, 0));
    CHECK(!WalkoutMayStartFor(s, 4, 100, 7, 1600, 0));
    CHECK(WalkoutMayStartFor(s, 4, 100, 8, 1600, 0));
    CHECK(WalkoutMayStartFor(s, 4, 100, 7, 1500 + kSeekWalkoutExcludeMs, 0));
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

static void TestWalkoutWaitsOnlyForACastOnTheSon()
{
    // At the stand the puller waits only for a cast aimed at the walk-out's Son (the pull shot itself). Its Auto Shot
    // or a wand's Shoot still on the boss is not the pull: it is interrupted and the shot goes out now.
    CHECK(WalkoutShotWaits(true, true));
    CHECK(!WalkoutShotWaits(false, true));  // a cast or autorepeat on another target
    CHECK(!WalkoutShotWaits(false, false)); // nothing running
    CHECK(!WalkoutShotWaits(true, false));  // a stale flag alone never waits
}

static void TestOneWalkoutPerCycle()
{
    // kSeekWalkoutMaxPerCycle 1: a second start in the same cycle is refused however much of the window is left; the
    // cycle's own walk-out is never retried, and the next cycle takes its own.
    CHECK(kSeekWalkoutMaxPerCycle == 1);
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 89000); // the siphon is due at 90000
    CHECK(!WalkoutCycleOpen(s, 3));
    CHECK(!WalkoutMayStart(s, 3, 200));
    CHECK(!WalkoutMayStartFor(s, 3, 200, 8, 2000, 88000));
    // A timeout with 60 s left reopens nothing, and the retry's time rule is not what refuses it.
    CHECK(!WalkoutFinish(s, WalkoutEnd::Timeout, 26000));
    WalkoutReturnDone(s);
    CHECK(WalkoutRetryStartInTime(s, 3, 26000));
    CHECK(!WalkoutRetryAllowed(s, 26000));
    CHECK(!WalkoutCycleOpen(s, 3));
    CHECK(!WalkoutMayStart(s, 3, 200));
    CHECK(!WalkoutMayStartFor(s, 3, 200, 8, 27000, 63000));
    // The next cycle starts its own count.
    CHECK(WalkoutCycleOpen(s, 4));
    CHECK(WalkoutMayStart(s, 4, 200));
}

static void TestWalkoutTimeoutLateSpendsCycle()
{
    // A timeout spends the cycle at any time left (max one walk-out): an abort (puller dead, the boss's victim) or a
    // Back end neither reopens it.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 45000); // due at 46000: 20 s left
    CHECK(!WalkoutFinish(s, WalkoutEnd::Timeout, 26000));
    WalkoutReturnDone(s);
    CHECK(!WalkoutMayStart(s, 3, 200));
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

static void TestWalkoutUnspentNeverRetries()
{
    // A failed cast or "no stand" frees the Son but not the cycle: one walk-out per cycle, however much time is left.
    SeekWalkoutState late;
    WalkoutStart(late, 7, 100, 1000, 3, 21000); // due at 22000
    WalkoutAbortUnspent(late, 2000);
    WalkoutReturnDone(late);
    CHECK(!WalkoutMayStart(late, 3, 100));
    CHECK(WalkoutMayStart(late, 4, 100)); // the Son itself is free again
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 89000);
    WalkoutAbortUnspent(s, 2000); // 87 s left: still no second walk-out
    WalkoutReturnDone(s);
    CHECK(!WalkoutMayStart(s, 3, 200));
    CHECK(WalkoutMayStart(s, 4, 200));
    CHECK(kSeekWalkoutMaxPerCycle == 1);
    CHECK(kSeekWalkoutRetryMinMs == 25000);
}

static void TestWalkoutForgetsDeadSons()
{
    // A walked Son that died is forgotten: once it respawns (49033/49034 come back after 60 s) it is the next cycle's
    // Son.
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

static void TestWalkoutRetryNeverStarts()
{
    // The retry's own checks are still there but cannot pass the cap: a walk-out that timed out with 40 s left is the
    // cycle's only one. The first start of the next cycle keeps the plain siphon lead.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 65000); // the siphon is due at 66000
    CHECK(!WalkoutFinish(s, WalkoutEnd::Timeout, 26000)); // 40 s left
    WalkoutReturnDone(s);
    CHECK(WalkoutRetryStartInTime(s, 3, 26000));
    CHECK(!WalkoutMayStartFor(s, 3, 200, 8, 26000, 40000));
    CHECK(WalkoutMayStartFor(s, 4, 200, 8, 70000, 20000));
    // An unspent abort's cycle is just as spent.
    SeekWalkoutState u;
    WalkoutStart(u, 7, 100, 1000, 3, 65000);
    WalkoutAbortUnspent(u, 2000);
    WalkoutReturnDone(u);
    CHECK(!WalkoutMayStartFor(u, 3, 100, 8, 2000, 64000));
    CHECK(WalkoutMayStartFor(u, 4, 100, 8, 2000, 64000));
}

static void TestCastFailExcludesPuller()
{
    // A cast that fails at the stand frees the Son, and the puller sits out the picks for 60 s like a move fail: the
    // next cycle's walk-out goes to another bot instead of the same puller, Son and stand.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 65000);
    WalkoutCastFailed(s, 7, 2000);
    WalkoutReturnDone(s);
    CHECK(WalkoutExcluded(s, 7, 2000));
    CHECK(!WalkoutMayStartFor(s, 3, 100, 8, 2500, 63500));
    CHECK(!WalkoutMayStartFor(s, 4, 100, 7, 2500, 63500));
    CHECK(WalkoutMayStartFor(s, 4, 100, 8, 2500, 63500));
    CHECK(!WalkoutExcluded(s, 7, 2000 + kSeekWalkoutExcludeMs));
}

// seek_pull (walk 51): a Son walked in an earlier cycle is free again, and the running task's own Son never is.
static void TestPerCycleWalkedFreesSons()
{
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 1, 72000);
    WalkoutFinish(s, WalkoutEnd::Abort, 3000); // a threat abort through the end path
    WalkoutReturnDone(s);
    WalkoutStart(s, 7, 200, 5000, 2, 72000);
    WalkoutFinish(s, WalkoutEnd::Timeout, 30000);
    WalkoutReturnDone(s);
    CHECK(s.walkedSons.size() == 2 && s.walkedAt.size() == 2);
    // The default rules: both Sons stay walked for life, so cycles 3 and later never walk out (walk 51's silent
    // cycles).
    CHECK(!WalkoutMayStart(s, 3, 100));
    CHECK(!WalkoutMayStart(s, 3, 200));
    // The row's rules forget the earlier cycles first.
    WalkoutForgetEarlierCycles(s, 3);
    CHECK(s.walkedSons.empty() && s.walkedAt.empty());
    CHECK(WalkoutMayStart(s, 3, 100));
    CHECK(WalkoutMayStart(s, 3, 200));
    // A running task's Son is never dropped, whatever the cycle asked for.
    WalkoutStart(s, 8, 200, 40000, 3, 72000);
    WalkoutForgetEarlierCycles(s, 4);
    CHECK(std::find(s.walkedSons.begin(), s.walkedSons.end(), 200) != s.walkedSons.end());
    CHECK(s.walkedAt.size() == 1 && s.walkedAt[0].first == 200 && s.walkedAt[0].second == 3);
    // A Son of the cycle itself stays, an older one goes.
    SeekWalkoutState t;
    WalkoutStart(t, 7, 100, 1000, 1);
    WalkoutNoteShot(t);
    WalkoutFinish(t, WalkoutEnd::Back, 2000);
    WalkoutStart(t, 7, 200, 3000, 2);
    WalkoutNoteShot(t);
    WalkoutFinish(t, WalkoutEnd::Back, 4000);
    WalkoutStart(t, 7, 300, 5000, 3);
    WalkoutForgetEarlierCycles(t, 3);
    CHECK(t.walkedSons.size() == 1 && t.walkedSons[0] == 300);
    CHECK(t.walkedAt.size() == 1 && t.walkedAt[0].second == 3);
}

static void TestRetryRulesFromTheRow()
{
    const WalkoutCycleRules rules = WalkoutRulesFromRow(3, 60.0f);
    CHECK(rules.maxPerCycle == 3);
    CHECK(rules.retryMinMs == 60000u);
    CHECK(rules.firstStartMinMs == 60000u);
    CHECK(rules.perCycleWalked && rules.reopenOnAbort && rules.shotStall && rules.pickTopTankThreat &&
          rules.standFan && rules.hunterReachScan);
    // A threat abort with 72 s left reopens the cycle: three starts fit, the fourth does not, and every start needs
    // 60 s or more to the siphon.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 72000); // due at 73000
    CHECK(WalkoutFinish(s, WalkoutEnd::Abort, 3000, rules));
    WalkoutReturnDone(s);
    CHECK(WalkoutMayStartFor(s, 3, 200, 8, 4000, 69000, rules));
    WalkoutStart(s, 8, 200, 4000, 3, 69000);
    CHECK(WalkoutFinish(s, WalkoutEnd::Abort, 5000, rules));
    WalkoutReturnDone(s);
    CHECK(WalkoutMayStartFor(s, 3, 300, 9, 6000, 67000, rules));
    WalkoutStart(s, 9, 300, 6000, 3, 67000);
    CHECK(!WalkoutFinish(s, WalkoutEnd::Timeout, 7000, rules)); // three starts: no fourth
    WalkoutReturnDone(s);
    CHECK(!WalkoutMayStartFor(s, 3, 400, 10, 8000, 65000, rules));
    // A retry with only 59 s left is refused, and so is a first start.
    SeekWalkoutState r;
    WalkoutStart(r, 7, 100, 1000, 3, 72000);
    CHECK(WalkoutFinish(r, WalkoutEnd::Timeout, 3000, rules));
    WalkoutReturnDone(r);
    CHECK(!WalkoutMayStartFor(r, 3, 200, 8, 14000, 59000, rules));
    SeekWalkoutState f;
    CHECK(!WalkoutMayStartFor(f, 3, 100, 8, 1000, 59000, rules));
    // The default rules keep the old one walk-out per cycle.
    SeekWalkoutState old;
    WalkoutStart(old, 7, 100, 1000, 3, 89000);
    CHECK(!WalkoutFinish(old, WalkoutEnd::Timeout, 26000));
    WalkoutReturnDone(old);
    CHECK(!WalkoutMayStart(old, 3, 200));
    CHECK(!WalkoutMayStartFor(old, 3, 200, 8, 27000, 62000));
}

static void TestWalkoutEndFreesSon()
{
    // Puller-side ends free the Son for the retry; Son-side ends keep it walked for this cycle.
    for (const char* reason : {"threat", "victim", "dead", "charmed", "insanity", "gone", "cast failed",
                               "move failed", "no shot"})
        CHECK(WalkoutEndFreesSon(reason));
    for (const char* reason : {"no stand", "son gone", "timeout"})
        CHECK(!WalkoutEndFreesSon(reason));
}

static void TestThreatPickUsesTheRunningReference()
{
    // Cause Insanity zeroes the main tank's threat at ~17 s: the pick compares with the live top tank, like the abort.
    CHECK(!WalkoutPickThreatTooHigh(0.0f, 0.0f));
    CHECK(WalkoutPickThreatTooHigh(50.0f, 0.0f));
    CHECK(!WalkoutPickThreatTooHigh(699.0f, 1000.0f));
    CHECK(WalkoutPickThreatTooHigh(700.0f, 1000.0f));
    CHECK(WalkoutPickThreatTooHigh(703.0f, 1000.0f));
}

static void TestShotStall()
{
    SeekWalkoutState s;
    CHECK(!WalkoutShotStalled(s, 1000));
    WalkoutStart(s, 7, 100, 1000, 1);
    CHECK(!WalkoutShotStalled(s, 2000)); // on the way, not at the stand
    WalkoutNoteAtStand(s, 2000);
    CHECK(s.atStandSinceMs == 2000);
    WalkoutNoteAtStand(s, 2500); // the stand clock keeps its first tick
    CHECK(s.atStandSinceMs == 2000);
    CHECK(!WalkoutShotStalled(s, 2000 + kSeekWalkoutShotStallMs - 1));
    CHECK(WalkoutShotStalled(s, 2000 + kSeekWalkoutShotStallMs));
    WalkoutNoteShot(s);
    CHECK(s.atStandSinceMs == 0);
    CHECK(!WalkoutShotStalled(s, 2000 + kSeekWalkoutShotStallMs + 1));
}

static void TestStandMoveRestartsTheStallClock()
{
    // The stand follows the patrolling Son: a re-plan that moves it more than the arrival distance starts the stall
    // clock afresh at the new spot, so time spent at the old stand cannot abort the walk.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 1);
    WalkoutNoteStand(s, true, 10.0f, 10.0f, 50.0f, 1000);
    WalkoutNoteAtStand(s, 2000);
    CHECK(s.atStandSinceMs == 2000);
    // A re-plan 2 yd away is the same stand: the clock keeps running.
    WalkoutNoteStand(s, true, 12.0f, 10.0f, 50.0f, 3000);
    CHECK(s.atStandSinceMs == 2000);
    // 8 yd away it is a new stand: the clock restarts, and 6 s later there is no abort.
    WalkoutNoteStand(s, true, 20.0f, 10.0f, 50.0f, 8000);
    CHECK(s.atStandSinceMs == 0);
    CHECK(!WalkoutShotStalled(s, 8000 + kSeekWalkoutShotStallMs));
    // 6 s at the new stand: abort.
    WalkoutNoteAtStand(s, 14000);
    CHECK(WalkoutShotStalled(s, 14000 + kSeekWalkoutShotStallMs));
}

static void TestAnAbortAfterTheShotKeepsTheCycleClosed()
{
    // The shot landed (the Son joined the fight), so the walk-out did happen: a later abort (the puller dying, say)
    // neither reopens the cycle nor frees the Son for a retry.
    const WalkoutCycleRules rules = WalkoutRulesFromRow(3, 60.0f);
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 3, 72000);
    WalkoutNoteShot(s);
    CHECK(!WalkoutFinish(s, WalkoutEnd::Abort, 3000, rules));
    CHECK(s.walkedSons.size() == 1 && s.walkedSons[0] == 100);
    CHECK(s.cycleSpent);
    CHECK(s.returner == 7);
}

static void TestPlatformPickAndCastLater()
{
    // Walk 52 cycle-4 shape: two hunters dead, three living casters 35-48 yd from the centre (outside the 30 yd
    // anchor) mid-cast, one mage benched and one warlock under Cause Insanity. With the defaults nobody is picked.
    BossAnchorCircle c;
    c.x = 0.0f;
    c.y = 0.0f;
    c.z = 54.0f;
    c.radius = 30.0f;
    std::vector<WalkoutCandidate> raid = {
        Cand(1, WalkoutRole::Hunter, 20.0f),
        Cand(2, WalkoutRole::Hunter, 22.0f),
        Cand(3, WalkoutRole::Caster, 35.0f),
        Cand(4, WalkoutRole::Caster, 41.0f),
        Cand(5, WalkoutRole::Caster, 48.0f),
        Cand(6, WalkoutRole::Caster, 44.0f),
        Cand(7, WalkoutRole::Caster, 46.0f),
    };
    raid[0].alive = false;
    raid[1].alive = false;
    for (std::size_t i = 2; i < raid.size(); ++i)
    {
        raid[i].inside = InsideAnchor(c, raid[i].yd, 0.0f);
        raid[i].canShoot = false; // a cast in progress refuses the check
    }
    raid[5].excluded = true; // the benched mage
    raid[6].insane = true;   // the warlock under Cause Insanity
    CHECK(PickWalkoutPuller(raid) == -1);
    for (std::size_t i = 2; i < raid.size(); ++i)
    {
        raid[i].inside = WalkoutOnPlatform(raid[i].yd, 52.9f);
        raid[i].canShoot = WalkoutShotReadyLater(false, true, false);
    }
    CHECK(PickWalkoutPuller(raid) == 2); // the nearest living caster
}

static void TestWalkoutOnPlatform()
{
    CHECK(Near(kSeekWalkoutPlatformYd, 60.0f));
    CHECK(WalkoutOnPlatform(59.9f, 52.9f));
    CHECK(!WalkoutOnPlatform(60.1f, 52.9f));
    CHECK(!WalkoutOnPlatform(40.0f, 46.4f));
    CHECK(WalkoutOnPlatform(40.0f, 46.5f));
}

static void TestWalkoutShotReadyLater()
{
    CHECK(WalkoutShotReadyLater(true, false, false));
    CHECK(WalkoutShotReadyLater(false, true, false));
    CHECK(WalkoutShotReadyLater(false, false, true));
    CHECK(!WalkoutShotReadyLater(false, false, false));
}

static void TestWalkoutFightOldEnough()
{
    CHECK(WalkoutFightOldEnough(WalkoutCycleRules(), 0));
    const WalkoutCycleRules rules = WalkoutRulesFromRow(3, 60.0f);
    CHECK(rules.firstFightStartMs == 19000u);
    CHECK(!WalkoutFightOldEnough(rules, 18999));
    CHECK(WalkoutFightOldEnough(rules, 19000));
}

static void TestWalkoutStartLead()
{
    const WalkoutCycleRules eighty = WalkoutRulesFromRow(3, 60.0f, 80.0f);
    CHECK(eighty.startLeadMs == 80000u);
    SeekWalkoutState s;
    CHECK(WalkoutMayStartFor(s, 0, 100, 7, 1000, 80000, eighty));
    CHECK(!WalkoutMayStartFor(s, 0, 100, 7, 1000, 80001, eighty));
    CHECK(WalkoutMayStartFor(s, 0, 100, 7, 1000, 60000, eighty));
    CHECK(!WalkoutMayStartFor(s, 0, 100, 7, 1000, 59999, eighty));
    // The old line without the seek window's own lead.
    const WalkoutCycleRules old = WalkoutRulesFromRow(3, 60.0f);
    CHECK(old.startLeadMs == kSeekWalkoutSiphonLeadMs);
    CHECK(!WalkoutMayStartFor(s, 0, 100, 7, 1000, 75001, old));
    CHECK(WalkoutMayStartFor(s, 0, 100, 7, 1000, 75000, old));
    // The lead is clamped to the walk-out's own bounds.
    CHECK(WalkoutRulesFromRow(3, 60.0f, 90.0f).startLeadMs == 85000u);
    CHECK(WalkoutRulesFromRow(3, 60.0f, 50.0f).startLeadMs == 75000u);
}

static void TestStandMoveKeepsTheStallClockInShot()
{
    // In shot position the stand keeps moving with the patrolling Son: the clock keeps running instead of restarting
    // with every re-plan.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 1);
    WalkoutNoteStand(s, true, 10.0f, 10.0f, 50.0f, 1000);
    WalkoutNoteAtStand(s, 2000);
    CHECK(s.atStandSinceMs == 2000);
    WalkoutNoteStand(s, true, 18.0f, 10.0f, 50.0f, 8000, true);
    CHECK(s.atStandSinceMs == 2000);
    CHECK(WalkoutShotStalled(s, 8000));
}

static void TestWalkoutRejectTankAndHealer()
{
    // The picker classifies with WalkoutRoleOf over `rangedDps && !healer`, so a melee tank and a ranged healer both
    // read WalkoutRole::None and the role test alone would file them as `no_role`.
    WalkoutCandidate tank = Cand(1, WalkoutRoleOf(false, false, false), 5.0f);
    tank.tank = true;
    CHECK(tank.role == WalkoutRole::None);
    CHECK(!WalkoutEligible(tank));
    CHECK(WalkoutRejectOf(tank) == WalkoutReject::Tank);

    WalkoutCandidate healer = Cand(2, WalkoutRoleOf(false, false, false), 12.0f);
    healer.healer = true;
    CHECK(healer.role == WalkoutRole::None);
    CHECK(!WalkoutEligible(healer));
    CHECK(WalkoutRejectOf(healer) == WalkoutReject::Healer);

    // The post-loop pass's counts: tank and healer by name, not one `no_role`.
    WalkoutRejectTally t;
    t.members = 5;
    if (!WalkoutEligible(tank))
        t.Add(WalkoutRejectOf(tank));
    if (!WalkoutEligible(healer))
        t.Add(WalkoutRejectOf(healer));
    CHECK(WalkoutRejectText(t) == "reason=tank:1|healer:1,members=5 top=-1 low=-1");
}

static void TestStandCandidatesFan()
{
    BossAnchorCircle c;
    c.x = 0.0f;
    c.y = 0.0f;
    c.z = 54.0f;
    c.radius = 30.0f;
    const std::vector<std::pair<float, float>> base = WalkoutStandCandidates(c, -78.0f, 0.0f);
    const std::vector<std::pair<float, float>> zero = WalkoutStandCandidatesFan(c, -78.0f, 0.0f, {0.0f});
    CHECK(zero.size() == base.size());
    for (std::size_t i = 0; i < base.size(); ++i)
    {
        CHECK(zero[i].first == base[i].first);
        CHECK(zero[i].second == base[i].second);
    }
    const std::vector<std::pair<float, float>> fan =
        WalkoutStandCandidatesFan(c, -78.0f, 0.0f, {0.0f, 10.0f, -10.0f, 20.0f, -20.0f});
    CHECK(fan.size() == base.size() * 5);
    // The rays come in the offsets' order, and every point stays between the circle's edge and the Son.
    for (std::size_t i = 0; i < fan.size(); ++i)
    {
        float const r = std::sqrt(fan[i].first * fan[i].first + fan[i].second * fan[i].second);
        CHECK(r >= c.radius - 0.01f && r < 78.0f);
    }
    const float cos10 = std::cos(10.0f * 3.14159265358979323846f / 180.0f);
    const float sin10 = std::sin(10.0f * 3.14159265358979323846f / 180.0f);
    CHECK(Near(fan[base.size()].first, -30.0f * cos10));
    CHECK(Near(fan[base.size()].second, -30.0f * sin10));
    CHECK(Near(fan[base.size() * 2].first, -30.0f * cos10));
    CHECK(Near(fan[base.size() * 2].second, 30.0f * sin10));
    // A Son inside the circle has no ray: the anchored shot is its pull.
    CHECK(WalkoutStandCandidatesFan(c, 10.0f, 0.0f, {0.0f, 10.0f}).empty());
}

int main()
{
    TestWalkoutRejectCodes();
    TestWalkoutRejectText();
    TestPerCycleWalkedFreesSons();
    TestRetryRulesFromTheRow();
    TestWalkoutEndFreesSon();
    TestThreatPickUsesTheRunningReference();
    TestShotStall();
    TestStandMoveRestartsTheStallClock();
    TestAnAbortAfterTheShotKeepsTheCycleClosed();
    TestPlatformPickAndCastLater();
    TestWalkoutOnPlatform();
    TestWalkoutShotReadyLater();
    TestWalkoutFightOldEnough();
    TestWalkoutStartLead();
    TestStandMoveKeepsTheStallClockInShot();
    TestWalkoutRejectTankAndHealer();
    TestStandCandidatesFan();
    TestWalkoutRetryNeverStarts();
    TestCastFailExcludesPuller();
    TestOneWalkoutPerCycle();
    TestWalkoutWaitsOnlyForACastOnTheSon();
    TestWalkoutTimeoutLateSpendsCycle();
    TestWalkoutUnspentNeverRetries();
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
