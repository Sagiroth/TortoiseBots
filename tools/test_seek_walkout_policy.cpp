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

static void TestPulledSonMeetsReceiver()
{
    // Walk-53 geometry: the tanks' circle around Hakkar's spawn, the puller returning from the south-east, and the
    // seek spot B where the raid gathers. The old walk back ends 11.3 yd from B: more than the receiver's take reach,
    // so the Son that chased the puller landed on the ranged raid instead of on a tank.
    BossAnchorCircle tank;
    tank.x = -11787.4f;
    tank.y = -1649.34f;
    tank.z = 54.1f;
    tank.radius = 25.0f;
    const float pullerX = -11760.0f;
    const float pullerY = -1690.0f;
    const float receiverX = -11787.4f;
    const float receiverY = -1664.3f;
    float oldX = 0.0f, oldY = 0.0f;
    WalkoutBackPoint(tank, pullerX, pullerY, oldX, oldY);
    const float oldToReceiver = std::sqrt((oldX - receiverX) * (oldX - receiverX) +
                                          (oldY - receiverY) * (oldY - receiverY));
    CHECK(oldToReceiver > kSeekMeetTakeYd);
    // The meet's back point is the receiver's own point (inside the circle, so the clamp is the identity)...
    float meetX = 0.0f, meetY = 0.0f;
    WalkoutMeetBackPoint(true, receiverX, receiverY, tank, pullerX, pullerY, meetX, meetY);
    CHECK(Near(meetX, receiverX) && Near(meetY, receiverY));
    // ... and the arrival is measured against it: arrived 1.9 yd from the receiver, and (walk 54) at the old circle-edge
    // point too, whichever comes first (the receiver keeps moving, so a 2 yd stop timed the walk-out out).
    CHECK(WalkoutMeetArrived(true, meetX, meetY, tank, oldX, oldY));
    CHECK(WalkoutMeetArrived(true, meetX, meetY, tank, receiverX + 1.9f, receiverY));
    // A receiver outside the circle is clamped onto it (30 yd out -> the circle's 20 yd walk-back reach).
    float clampX = 0.0f, clampY = 0.0f;
    WalkoutMeetBackPoint(true, tank.x + 30.0f, tank.y, tank, pullerX, pullerY, clampX, clampY);
    CHECK(Near(clampX, tank.x + 20.0f) && Near(clampY, tank.y));
    // Flag off: both functions are the old ones, for three points.
    const std::pair<float, float> points[] = {{pullerX, pullerY}, {tank.x, tank.y}, {-11820.0f, -1640.0f}};
    for (const std::pair<float, float>& p : points)
    {
        float wantX = 0.0f, wantY = 0.0f, gotX = 0.0f, gotY = 0.0f;
        WalkoutBackPoint(tank, p.first, p.second, wantX, wantY);
        WalkoutMeetBackPoint(false, receiverX, receiverY, tank, p.first, p.second, gotX, gotY);
        CHECK(Near(gotX, wantX) && Near(gotY, wantY));
        CHECK(WalkoutMeetArrived(false, 0.0f, 0.0f, tank, p.first, p.second) ==
              WalkoutBackArrived(tank, p.first, p.second));
    }

    // The receiver pick: the third tank first, then the second off-tank, never the main tank.
    const auto usable = []
    {
        MeetCandidate c;
        c.alive = true;
        c.inside = true;
        return c;
    };
    std::vector<MeetCandidate> tanks;
    tanks = {usable(), usable(), usable()};
    CHECK(PickMeetReceiver(tanks) == 2);
    MeetCandidate dead = usable();
    dead.alive = false;
    tanks = {usable(), usable(), dead};
    CHECK(PickMeetReceiver(tanks) == 1);
    MeetCandidate busy = usable();
    busy.holdsSon = true;
    MeetCandidate victim = usable();
    victim.bossVictim = true;
    tanks = {usable(), busy, victim};
    CHECK(PickMeetReceiver(tanks) == -1);
    tanks = {usable(), dead, dead};
    CHECK(PickMeetReceiver(tanks) == -1);
    tanks = {usable(), usable()};
    CHECK(PickMeetReceiver(tanks) == 1);
    tanks = {usable()};
    CHECK(PickMeetReceiver(tanks) == -1);
    tanks.clear();
    CHECK(PickMeetReceiver(tanks) == -1);

    // Each refusal word alone, and the first failing test's word when several fail.
    CHECK(MeetRefusalWhy(usable()) == nullptr);
    MeetCandidate c = usable();
    c.charmed = true;
    CHECK(std::string(MeetRefusalWhy(c)) == "charmed");
    c = usable();
    c.bossVictim = true;
    CHECK(std::string(MeetRefusalWhy(c)) == "victim");
    c = usable();
    c.holdsSon = true;
    CHECK(std::string(MeetRefusalWhy(c)) == "busy");
    c = usable();
    c.inside = false;
    CHECK(std::string(MeetRefusalWhy(c)) == "outside");
    c = usable();
    c.alive = false;
    c.inside = false;
    CHECK(std::string(MeetRefusalWhy(c)) == "dead");
    c = usable();
    c.bossVictim = true;
    c.holdsSon = true;
    CHECK(std::string(MeetRefusalWhy(c)) == "victim");

    tanks = {usable(), dead, busy};
    CHECK(MeetRefusalCounts(tanks) == "dead:1|busy:1");
    tanks = {usable()};
    CHECK(MeetRefusalCounts(tanks) == "none");

    // The receiver takes the Son within 8 yd or when it is already on it; the meet is over at 20 s.
    CHECK(SeekMeetTakeDue(8.0f, false));
    CHECK(!SeekMeetTakeDue(8.01f, false));
    CHECK(SeekMeetTakeDue(30.0f, true));
    CHECK(!SeekMeetOpen(0, 0, 0));
    CHECK(SeekMeetOpen(7, 1000, 20999));
    CHECK(!SeekMeetOpen(7, 1000, 21000));

    // The meet outlives the task's back, and only WalkoutMeetClear drops it.
    SeekWalkoutState s;
    s.puller = 11;
    s.son = 22;
    s.startMs = 100;
    s.shot = true;
    s.meetReceiver = 33;
    s.meetSon = 22;
    s.meetMs = 500;
    s.meetPuller = 11;
    WalkoutFinish(s, WalkoutEnd::Back, 600);
    CHECK(s.meetReceiver == 33 && s.meetSon == 22 && s.meetMs == 500 && s.meetPuller == 11);
    WalkoutReturnDone(s);
    CHECK(s.meetReceiver == 33 && s.meetSon == 22 && s.meetMs == 500 && s.meetPuller == 11);
    WalkoutMeetClear(s);
    CHECK(s.meetReceiver == 0 && s.meetSon == 0 && s.meetMs == 0 && s.meetPuller == 0);
}

// Every reading bot holds its own copy of the meet; only the caller whose copy still matches the shared meet clears it
// and logs the terminal row. Two readers on the same snapshot: exactly one close returns true.
static void TestMeetCloseIsWonByOneReader()
{
    SeekWalkoutState shared;
    shared.meetReceiver = 33;
    shared.meetSon = 22;
    shared.meetMs = 500;
    shared.meetPuller = 11;
    const SeekWalkoutState readerA = shared; // both readers copied the meet before either closed it
    const SeekWalkoutState readerB = shared;
    int wins = 0;
    wins += WalkoutMeetCloseIf(shared, readerA.meetReceiver, readerA.meetSon, readerA.meetMs) ? 1 : 0;
    wins += WalkoutMeetCloseIf(shared, readerB.meetReceiver, readerB.meetSon, readerB.meetMs) ? 1 : 0;
    CHECK(wins == 1);
    CHECK(shared.meetReceiver == 0 && shared.meetSon == 0 && shared.meetMs == 0 && shared.meetPuller == 0);
    // A stale copy never closes a later meet: another receiver, another Son or another shot time.
    shared.meetReceiver = 34;
    shared.meetSon = 23;
    shared.meetMs = 900;
    CHECK(!WalkoutMeetCloseIf(shared, 33, 22, 500));
    CHECK(!WalkoutMeetCloseIf(shared, 34, 22, 900)); // same receiver and time, another Son
    CHECK(!WalkoutMeetCloseIf(shared, 34, 23, 500)); // same receiver and Son, another shot time
    CHECK(!WalkoutMeetCloseIf(shared, 0, 23, 900));  // no receiver named
    CHECK(shared.meetReceiver == 34);
    CHECK(WalkoutMeetCloseIf(shared, 34, 23, 900));
}

// The receiver waits only while the Son's victim is the walk-out puller, the returner or nobody; a Son on anyone else
// (a DPS or a healer peeled it) ends the wait and the meet closes as `peeled`.
// Walk 54, fix D: the meet's arrival is within kSeekMeetTakeYd (8 yd) of the receiver's clamped point OR at the old
// circle-edge point.
static void TestMeetArrivalNearReceiverOrOldPoint()
{
    BossAnchorCircle tank;
    tank.x = -11787.4f;
    tank.y = -1649.34f;
    tank.z = 54.1f;
    tank.radius = 25.0f;
    // The Eleen case: the receiver stands 24.7 yd out (clamped to the 20 yd walk-back reach), the puller 6.8 yd from
    // that point and beyond the old arrival radius (20 + 2 yd).
    const float recvX = tank.x + 24.7f;
    const float recvY = tank.y;
    float meetX = 0.0f, meetY = 0.0f;
    WalkoutMeetBackPoint(true, recvX, recvY, tank, tank.x + 40.0f, tank.y, meetX, meetY);
    CHECK(Near(meetX, tank.x + 20.0f));
    const float nearX = meetX + 6.8f;
    CHECK(!WalkoutBackArrived(tank, nearX, tank.y));          // outside the old arrival radius
    CHECK(WalkoutMeetArrived(true, meetX, meetY, tank, nearX, tank.y));      // 6.8 yd from the receiver: arrived
    CHECK(WalkoutMeetArrived(true, meetX, meetY, tank, meetX + 8.0f, tank.y)); // the take reach itself
    CHECK(!WalkoutMeetArrived(true, meetX, meetY, tank, meetX + 8.5f, tank.y)); // just past it: not yet
    // The old point: at the circle edge point (20 yd) while the receiver walked far off the other way.
    CHECK(WalkoutMeetArrived(true, tank.x - 40.0f, tank.y, tank, tank.x + 20.0f, tank.y));
    // 30 yd out and 30 yd from the receiver: not arrived.
    CHECK(!WalkoutMeetArrived(true, meetX, meetY, tank, tank.x + 50.0f, tank.y));
    // No meet: the old rule, unchanged.
    CHECK(WalkoutMeetArrived(false, 0.0f, 0.0f, tank, tank.x + 20.0f, tank.y) ==
          WalkoutBackArrived(tank, tank.x + 20.0f, tank.y));
    CHECK(!WalkoutMeetArrived(false, 0.0f, 0.0f, tank, nearX, tank.y));
}

// Walk 54, fix C: the take edge. The Son stands outside the raid circle; its victim stands on the circle's edge.
static void TestTakeEdgeRefusal()
{
    BossAnchorCircle raid;
    raid.x = -11787.4f;
    raid.y = -1649.34f;
    raid.z = 54.1f;
    raid.radius = 30.0f;
    // The pure verdict: only a Son outside the circle is ever refused, and then unless the victim is inside, the z is
    // ok, the Son is near the circle AND the Son is on its victim; only a distance failing alone is `Far`.
    CHECK(SeekTakeEdgeVerdict(true, false, false, false, false) == SeekTakeEdge::Allowed);
    CHECK(SeekTakeEdgeVerdict(true, true, true, true, true) == SeekTakeEdge::Allowed);
    CHECK(SeekTakeEdgeVerdict(false, false, true, true, true) == SeekTakeEdge::Refused);
    CHECK(SeekTakeEdgeVerdict(false, true, false, true, true) == SeekTakeEdge::Refused);
    CHECK(SeekTakeEdgeVerdict(false, false, true, false, false) == SeekTakeEdge::Refused); // not only a distance
    CHECK(SeekTakeEdgeVerdict(false, true, false, false, true) == SeekTakeEdge::Refused);
    CHECK(SeekTakeEdgeVerdict(false, true, true, true, true) == SeekTakeEdge::Allowed);
    CHECK(SeekTakeEdgeVerdict(false, true, true, false, true) == SeekTakeEdge::Far);
    CHECK(SeekTakeEdgeVerdict(false, true, true, true, false) == SeekTakeEdge::Far);
    CHECK(SeekTakeEdgeVerdict(false, true, true, false, false) == SeekTakeEdge::Far);
    // Named failing case from walk 54: the victim at 30.0 yd, the Son at 34 yd (outside) -> taken.
    CHECK(!SeekTakeEdgeRefuses(raid, raid.x + 34.0f, raid.y, 54.0f, true, raid.x + 30.0f, raid.y));
    // The victim 31.9 yd out is inside the 2 yd slack; 32.5 yd is not.
    CHECK(!SeekTakeEdgeRefuses(raid, raid.x + 36.0f, raid.y, 54.0f, true, raid.x + 31.9f, raid.y));
    CHECK(SeekTakeEdgeRefuses(raid, raid.x + 36.0f, raid.y, 54.0f, true, raid.x + 32.5f, raid.y));
    // Victim at 40 yd: refused (a chase would leave the platform's raid).
    CHECK(SeekTakeEdgeRefuses(raid, raid.x + 45.0f, raid.y, 54.0f, true, raid.x + 40.0f, raid.y));
    // The Son's z 45 is below Hakkar's evade line (and the 46.5 floor): refused although the victim is inside.
    CHECK(SeekTakeEdgeRefuses(raid, raid.x + 34.0f, raid.y, 45.0f, true, raid.x + 30.0f, raid.y));
    CHECK(!SeekTakeEdgeRefuses(raid, raid.x + 34.0f, raid.y, kSeekWalkoutMinStandZ, true, raid.x + 30.0f,
                                         raid.y));
    // No victim: refused like today (nobody inside to protect).
    CHECK(SeekTakeEdgeRefuses(raid, raid.x + 34.0f, raid.y, 54.0f, false, 0.0f, 0.0f));
    // A Son inside the circle is never refused, whatever its victim or z do (today's rule).
    CHECK(!SeekTakeEdgeRefuses(raid, raid.x + 25.0f, raid.y, 45.0f, true, raid.x + 40.0f, raid.y));
    CHECK(!SeekTakeEdgeRefuses(raid, raid.x + 25.0f, raid.y, 54.0f, false, 0.0f, 0.0f));
    // Codex round 1: the exemption is bounded. A Son at 50 yd with its victim inside is refused (`Far`): it is past the
    // circle + 8 yd, and also 20 yd from the victim.
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 50.0f, raid.y, 54.0f, true, raid.x + 30.0f, raid.y) == SeekTakeEdge::Far);
    // Near the victim but too far from the circle: a victim on the edge (31.9 yd) and the Son 9.9 yd behind it at
    // 41.8 yd is past radius + 8 (38) although within 10 yd of the victim.
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 41.8f, raid.y, 54.0f, true, raid.x + 31.9f, raid.y) == SeekTakeEdge::Far);
    // A Son at 36 yd but 15 yd from its inside victim is refused (not really on the victim).
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 36.0f, raid.y, 54.0f, true, raid.x + 21.0f, raid.y) == SeekTakeEdge::Far);
    CHECK(SeekTakeEdgeRefuses(raid, raid.x + 36.0f, raid.y, 54.0f, true, raid.x + 21.0f, raid.y));
    // A Son at 34 yd and 4 yd from its inside victim is taken.
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 34.0f, raid.y, 54.0f, true, raid.x + 30.0f, raid.y) == SeekTakeEdge::Allowed);
    // The boundary at radius + 8 (38 yd): 37.9 is taken, 38.1 is not.
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 37.9f, raid.y, 54.0f, true, raid.x + 31.0f, raid.y) == SeekTakeEdge::Allowed);
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 38.1f, raid.y, 54.0f, true, raid.x + 31.0f, raid.y) == SeekTakeEdge::Far);
    // The boundary at 10 yd from the victim: 9.9 is taken, 10.1 is not (the Son 36 yd out, the victim inside).
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 36.0f, raid.y, 54.0f, true, raid.x + 26.1f, raid.y) == SeekTakeEdge::Allowed);
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 36.0f, raid.y, 54.0f, true, raid.x + 25.9f, raid.y) == SeekTakeEdge::Far);
    // The distance basis is 2D from the circle's centre, whatever the bearing.
    CHECK(SeekTakeEdgeCheck(raid, raid.x, raid.y - 37.9f, 54.0f, true, raid.x, raid.y - 31.0f) == SeekTakeEdge::Allowed);
    CHECK(SeekTakeEdgeCheck(raid, raid.x, raid.y - 38.1f, 54.0f, true, raid.x, raid.y - 31.0f) == SeekTakeEdge::Far);
    // A refusal that is not only a distance stays `Refused` (z 45, victim outside): the log word stays chase_refused.
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 50.0f, raid.y, 45.0f, true, raid.x + 30.0f, raid.y) == SeekTakeEdge::Refused);
    CHECK(SeekTakeEdgeCheck(raid, raid.x + 50.0f, raid.y, 54.0f, true, raid.x + 45.0f, raid.y) == SeekTakeEdge::Refused);
    // The victim's position is what counts, never the Son's: a Son far outside next to an inside victim is taken, a
    // Son just outside next to an outside victim is refused.
    CHECK(!SeekTakeEdgeRefuses(raid, raid.x + 33.0f, raid.y, 54.0f, true, raid.x + 29.0f, raid.y));
    CHECK(SeekTakeEdgeRefuses(raid, raid.x + 31.0f, raid.y, 54.0f, true, raid.x + 38.0f, raid.y));
}

// Fix A: the meet's walkers (puller, returner) are no non-tank victim.
static void TestMeetWalkerHasSon()
{
    CHECK(SeekMeetWalkerHasSon(7, 7, 0));
    CHECK(SeekMeetWalkerHasSon(8, 0, 8));
    CHECK(SeekMeetWalkerHasSon(8, 7, 8));
    CHECK(!SeekMeetWalkerHasSon(9, 7, 8));    // a damage dealer or healer: counts on the clock
    CHECK(!SeekMeetWalkerHasSon(0, 0, 0));    // nobody
    CHECK(!SeekMeetWalkerHasSon(0, 7, 8));
    CHECK(!SeekMeetWalkerHasSon(5, 0, 0));    // no walker named
}

static void TestMeetReceiverWaitsOnlyForTheWalker()
{
    const std::uint64_t puller = 11, returner = 12, dps = 77, healer = 88;
    CHECK(MeetReceiverShouldWait(puller, puller, 0));    // the puller has it: wait
    CHECK(MeetReceiverShouldWait(returner, 0, returner)); // the returner has it: wait
    CHECK(MeetReceiverShouldWait(0, puller, returner));   // nobody has it yet: wait
    CHECK(MeetReceiverShouldWait(0, 0, 0));
    CHECK(!MeetReceiverShouldWait(dps, puller, 0));       // another DPS peeled it: no wait
    CHECK(!MeetReceiverShouldWait(healer, puller, returner));
    CHECK(!MeetReceiverShouldWait(dps, 0, 0));            // no walker named: a victim is nobody's walker
}

// The receiver is revalidated every update: charmed, Hakkar's victim, or already holding another Son ends the meet.
static void TestMeetReceiverLostWhy()
{
    CHECK(MeetReceiverLostWhy(false, false, false) == nullptr);
    CHECK(std::string(MeetReceiverLostWhy(true, false, false)) == "receiver_charmed");
    CHECK(std::string(MeetReceiverLostWhy(false, true, false)) == "receiver_victim");
    CHECK(std::string(MeetReceiverLostWhy(false, false, true)) == "receiver_busy");
    // The first failing test wins: charmed, then victim, then busy.
    CHECK(std::string(MeetReceiverLostWhy(true, true, true)) == "receiver_charmed");
    CHECK(std::string(MeetReceiverLostWhy(false, true, true)) == "receiver_victim");
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

static void TestWalkoutFocusHoldsOtherActions()
{
    // Walk 55 (failing case before the focus): the live puller, no shot yet, wins "follow" -> held. Without the
    // focus nothing held it and the puller stood in range and sight in "follow" until the 25 s timeout.
    CHECK(WalkoutFocusBlocks(true, true, false, "follow"));
    for (const char* other : {"hunter step out", "wait for attack", "reach melee", "reach spell", "feign death",
                              "flee", "auto shot", "boss tactics seek"})
        CHECK(WalkoutFocusBlocks(true, true, false, other));
    // The walk-out itself and the pet's dismissal still run.
    CHECK(!WalkoutFocusBlocks(true, true, false, "boss tactics seek tank"));
    CHECK(!WalkoutFocusBlocks(true, true, false, "boss tactics seek pet"));
    // The shot landed: the block lifts for the walk back (mutation: drop `!shot` and this fails).
    CHECK(!WalkoutFocusBlocks(true, true, true, "follow"));
    // Flag off and a bot that is not the puller: nothing held.
    CHECK(!WalkoutFocusBlocks(false, true, false, "follow"));
    CHECK(!WalkoutFocusBlocks(true, false, false, "follow"));
    // The row is one per walk-out: the same start logs once, the next walk-out logs again.
    CHECK(WalkoutPreemptRowDue(0, 1000));
    CHECK(!WalkoutPreemptRowDue(1000, 1000));
    CHECK(WalkoutPreemptRowDue(1000, 52000));
}

static void TestWalkoutPullShotSpell()
{
    // A hunter opens with Arcane Shot when castable, else Auto Shot; never a DoT.
    CHECK(std::string(WalkoutPullShotSpell(3, true)) == "arcane shot");
    CHECK(std::string(WalkoutPullShotSpell(3, false)) == "auto shot");
    CHECK(std::string(WalkoutPullShotSpell(3, true)) != "serpent sting");
    // Every other class keeps its shot whatever `arcaneReady` says.
    for (bool ready : {true, false})
    {
        CHECK(std::string(WalkoutPullShotSpell(8, ready)) == "shoot");
        CHECK(std::string(WalkoutPullShotSpell(9, ready)) == "shoot");
        CHECK(std::string(WalkoutPullShotSpell(5, ready)) == "shoot");
        CHECK(std::string(WalkoutPullShotSpell(11, ready)) == "wrath");
        CHECK(std::string(WalkoutPullShotSpell(7, ready)) == "lightning bolt");
        // A warrior or paladin has no walk-out shot, so its pick and its pull path are unchanged.
        CHECK(std::string(WalkoutPullShotSpell(1, ready)).empty());
        CHECK(std::string(WalkoutPullShotSpell(2, ready)).empty());
    }
}

static void TestWalkoutFaceFirst()
{
    // Not facing the Son at the stand: turn this tick, cast next tick.
    CHECK(WalkoutFaceFirst(true, true, false));
    // Already facing: cast now.
    CHECK(!WalkoutFaceFirst(true, true, true));
    // Flag off: today's turn-and-cast in one tick.
    CHECK(!WalkoutFaceFirst(false, true, false));
    // No seek_pull rules: no facing at all, as today.
    CHECK(!WalkoutFaceFirst(true, false, false));
}

static void TestWalkoutFocusPullerLive()
{
    CHECK(WalkoutFocusPullerLive(true, true, false, 0));
    CHECK(WalkoutFocusPullerLive(true, true, false, kSeekWalkoutTimeoutMs - 1));
    // At exactly 25 s the anchor's exemption has lapsed (WalkoutExempts uses `<`), and so has the focus.
    CHECK(!WalkoutFocusPullerLive(true, true, false, kSeekWalkoutTimeoutMs));
    // A puller that died or was charmed mid-walk-out is held by nothing, even before the abort clears the record.
    CHECK(!WalkoutFocusPullerLive(true, false, false, 1000));
    CHECK(!WalkoutFocusPullerLive(true, true, true, 1000));
    CHECK(!WalkoutFocusBlocks(true, WalkoutFocusPullerLive(true, false, false, 1000), false, "release spirit"));
    // A stale record past the task's 25 s holds nothing.
    CHECK(!WalkoutFocusPullerLive(true, true, false, kSeekWalkoutTimeoutMs + 1));
    // Not this bot's walk-out.
    CHECK(!WalkoutFocusPullerLive(false, true, false, 1000));
}

static WalkoutFocusInput FocusIn(float hpPct = 80.0f)
{
    WalkoutFocusInput in;
    in.flagOn = true;
    in.recordedPuller = true;
    in.alive = true;
    in.charmed = false;
    in.sinceStartMs = 5000;
    in.shot = false;
    in.hpPct = hpPct;
    return in;
}

static void TestWalkoutFocusHolds()
{
    // The multiplier's whole decision. The live puller at 80 %, no shot, action follow -> held.
    CHECK(WalkoutFocusHolds(FocusIn(80.0f), "follow"));
    // Failing case before the survival floor: the puller at 35 %, no shot, a healing potion -> not held (the hold
    // used to keep potions, healthstones and Mend Pet for the whole unshot task).
    CHECK(!WalkoutFocusHolds(FocusIn(35.0f), "healing potion"));
    CHECK(!WalkoutFocusHolds(FocusIn(35.0f), "healthstone"));
    CHECK(!WalkoutFocusHolds(FocusIn(35.0f), "mend pet"));
    // The floor's edge: 40 % still holds, just under it lets go.
    CHECK(WalkoutFocusHolds(FocusIn(kWalkoutFocusFloorPct), "healing potion"));
    CHECK(!WalkoutFocusHolds(FocusIn(kWalkoutFocusFloorPct - 0.1f), "healing potion"));
    // The walk-out and the pet's dismissal always run.
    CHECK(!WalkoutFocusHolds(FocusIn(), "boss tactics seek tank"));
    CHECK(!WalkoutFocusHolds(FocusIn(), "boss tactics seek pet"));
    // Dead (mutation: swap alive and charmed inside WalkoutFocusPullerLive and this or the next fails).
    WalkoutFocusInput dead = FocusIn();
    dead.alive = false;
    CHECK(!WalkoutFocusHolds(dead, "release spirit"));
    WalkoutFocusInput charmed = FocusIn();
    charmed.charmed = true;
    CHECK(!WalkoutFocusHolds(charmed, "follow"));
    // The shot landed: the walk back is not held.
    WalkoutFocusInput shot = FocusIn();
    shot.shot = true;
    CHECK(!WalkoutFocusHolds(shot, "follow"));
    // A stale record, a record naming another puller (or a walk back: no running task names this bot), the flag off.
    WalkoutFocusInput stale = FocusIn();
    stale.sinceStartMs = kSeekWalkoutTimeoutMs;
    CHECK(!WalkoutFocusHolds(stale, "follow"));
    WalkoutFocusInput other = FocusIn();
    other.recordedPuller = false;
    CHECK(!WalkoutFocusHolds(other, "follow"));
    WalkoutFocusInput off = FocusIn();
    off.flagOn = false;
    CHECK(!WalkoutFocusHolds(off, "follow"));
}

static void TestWalkoutPullShotInFlight()
{
    // Failing case before the in-flight wait: the pull shot cast 1 s ago, the Son not in combat yet -> wait (no second
    // cast, no "no shot" abort).
    CHECK(WalkoutPullShotWaits(10000, 11000, false));
    CHECK(!WalkoutNoShotAbortDue(true, 10000, 11000));
    // The Son is in combat: the shot landed, nothing to wait for.
    CHECK(!WalkoutPullShotWaits(10000, 11000, true));
    // 2 s and later: the shot missed; the stall aborts as before.
    CHECK(!WalkoutPullShotWaits(10000, 12000, false));
    CHECK(WalkoutNoShotAbortDue(true, 10000, 12000));
    // No pull cast this task (the flag off records none): today's stall rule.
    CHECK(!WalkoutPullShotWaits(0, 1000, false));
    CHECK(WalkoutNoShotAbortDue(true, 0, 1000));
    CHECK(!WalkoutNoShotAbortDue(false, 0, 1000));
    // A new task clears the cast time.
    SeekWalkoutState s;
    s.pullCastMs = 5000;
    WalkoutStart(s, 1, 2, 20000, 1);
    CHECK(s.pullCastMs == 0);
    s.pullCastMs = 21000;
    WalkoutFinish(s, WalkoutEnd::Back, 22000, WalkoutCycleRules());
    CHECK(s.pullCastMs == 0);
}

static void TestWalkoutRunningCount()
{
    // The focus's fast path counts running tasks only; a walk back is busy but not running.
    SeekWalkoutState s;
    WalkoutBusyMarks m = WalkoutMarksOf(s);
    CHECK(!m.busy && !m.running);
    WalkoutStart(s, 1, 2, 20000, 1);
    m = WalkoutMarksOf(s);
    CHECK(m.busy && m.running);
    WalkoutFinish(s, WalkoutEnd::Timeout, 45000, WalkoutCycleRules());
    m = WalkoutMarksOf(s);
    CHECK(m.busy && !m.running);
    CHECK(WalkoutCountStep(false, true) == 1);
    CHECK(WalkoutCountStep(true, false) == -1);
    CHECK(WalkoutCountStep(true, true) == 0);
    CHECK(WalkoutCountStep(false, false) == 0);
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


static void TestWalkoutHoldsCombat()
{
    // DungeonRunSeekWalkoutHoldCombat: `combat end` is held only for the live, unshot puller.
    // Named failing case: a live unshot puller 1 s into its task holds combat.
    CHECK(WalkoutHoldsCombat(true, true, true, false, 1000, false) == true);
    CHECK(!WalkoutHoldsCombat(false, true, true, false, 1000, false)); // flag off
    CHECK(!WalkoutHoldsCombat(true, true, true, false, 1000, true));   // shot: the walk back is not held
    CHECK(WalkoutHoldsCombat(true, true, true, false, kSeekWalkoutTimeoutMs - 1, false));
    CHECK(!WalkoutHoldsCombat(true, true, true, false, kSeekWalkoutTimeoutMs, false)); // the same `<` edge as the focus
    CHECK(!WalkoutHoldsCombat(true, true, false, false, 1000, false)); // dead
    CHECK(!WalkoutHoldsCombat(true, true, true, true, 1000, false));   // charmed
    CHECK(!WalkoutHoldsCombat(true, false, true, false, 1000, false)); // not the recorded puller
    // A puller that dies mid-task: held at 4 s, released the read it is dead (the record still names it).
    CHECK(WalkoutHoldsCombat(true, true, true, false, 4000, false));
    CHECK(!WalkoutHoldsCombat(true, true, false, false, 4100, false));
    // The walk-out ends while the trigger is held: the next read finds no task naming the bot, and the hold is gone.
    SeekWalkoutState s;
    WalkoutStart(s, 7, 100, 1000, 1);
    bool recorded = WalkoutActive(s) && s.puller == 7;
    CHECK(WalkoutHoldsCombat(true, recorded, true, false, 2000, s.shot));
    WalkoutFinish(s, WalkoutEnd::Timeout, 26000);
    recorded = WalkoutActive(s) && s.puller == 7;
    CHECK(!WalkoutHoldsCombat(true, recorded, true, false, 2100, s.shot));
}

static void TestWalkoutEligibleNeedsCombatEngine()
{
    WalkoutCandidate ok = Cand(1, WalkoutRole::Hunter, 5.0f);
    CHECK(ok.combatEngine); // the default: with the flag off the field stays true
    CHECK(WalkoutEligible(ok));
    WalkoutCandidate c = ok;
    c.combatEngine = false;
    CHECK(!WalkoutEligible(c));
    CHECK(WalkoutRejectOf(c) == WalkoutReject::NoCombat);
    CHECK(std::strcmp(WalkoutRejectWord(WalkoutReject::NoCombat), "no_combat") == 0);
    WalkoutRejectTally t;
    t.Add(WalkoutReject::NoCombat);
    CHECK(WalkoutRejectText(t).find("no_combat:1") != std::string::npos);
    // The bot's own engine and meal decide; a hunter whose pet fights while its own AI is on the non-combat engine is
    // not ready (the pet's combat is not an input).
    CHECK(WalkoutCombatEngineReady(true, false));
    CHECK(!WalkoutCombatEngineReady(false, false));
    CHECK(!WalkoutCombatEngineReady(true, true)); // eating or drinking
}


static void TestWalkoutFaceThenCast()
{
    // Named failing case: FaceCast on, a stopped puller facing away turns and casts in the same tick (no return).
    CHECK(WalkoutFaceThenCast(true, true, true, false).returnAfterFace == false);
    WalkoutFaceStep on = WalkoutFaceThenCast(true, true, true, false);
    CHECK(on.face && on.force && on.stop);
    on = WalkoutFaceThenCast(true, true, true, true); // already facing: no turn, still stopped, cast now
    CHECK(!on.face && on.stop && !on.returnAfterFace);
    on = WalkoutFaceThenCast(true, false, false, false); // FaceCast alone, without the focus or the rules
    CHECK(on.face && on.force && !on.returnAfterFace);
    // Off: today's t1106k values, exactly.
    for (int f = 0; f < 2; ++f)
        for (int r = 0; r < 2; ++r)
            for (int i = 0; i < 2; ++i)
            {
                const WalkoutFaceStep off = WalkoutFaceThenCast(false, f != 0, r != 0, i != 0);
                CHECK(off.returnAfterFace == WalkoutFaceFirst(f != 0, r != 0, i != 0));
                CHECK(off.stop == WalkoutFaceFirst(f != 0, r != 0, i != 0));
                CHECK(off.face == (r != 0 && i == 0));
                CHECK(!off.force);
            }
    CHECK(WalkoutFaceThenCast(false, true, true, false).returnAfterFace); // face-first still returns with the flag off
}

static void TestWalkoutFaceTargetYields()
{
    CHECK(!WalkoutFaceTargetYields(false, true)); // flag off: as before
    CHECK(WalkoutFaceTargetYields(true, true));   // the live, unshot puller keeps facing its Son
    CHECK(!WalkoutFaceTargetYields(true, false)); // not the puller: turned as before
    // The shot landed: the live-puller read (WalkoutHoldsCombat) is false, so the facing update runs again.
    CHECK(!WalkoutFaceTargetYields(true, WalkoutHoldsCombat(true, true, true, false, 3000, true)));
    CHECK(WalkoutFaceTargetYields(true, WalkoutHoldsCombat(true, true, true, false, 3000, false)));
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
    TestPulledSonMeetsReceiver();
    TestMeetArrivalNearReceiverOrOldPoint();
    TestTakeEdgeRefusal();
    TestMeetWalkerHasSon();
    TestMeetReceiverWaitsOnlyForTheWalker();
    TestMeetReceiverLostWhy();
    TestMeetCloseIsWonByOneReader();
    TestShotSpellPerClass();
    TestWalkoutFocusHoldsOtherActions();
    TestWalkoutPullShotSpell();
    TestWalkoutFaceFirst();
    TestWalkoutFocusPullerLive();
    TestWalkoutHoldsCombat();
    TestWalkoutEligibleNeedsCombatEngine();
    TestWalkoutFaceThenCast();
    TestWalkoutFaceTargetYields();
    TestWalkoutFocusHolds();
    TestWalkoutPullShotInFlight();
    TestWalkoutRunningCount();
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
