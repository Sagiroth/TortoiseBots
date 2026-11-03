// Standalone check for ai/playerbot/PullAggroEstimatePolicy.h (how many creatures a pull brings):
//   g++ -std=c++17 -Wall -Wextra tools/test_pull_aggro_estimate_policy.cpp -o /tmp/test_pull_aggro_estimate_policy && /tmp/test_pull_aggro_estimate_policy
#include "../ai/playerbot/PullAggroEstimatePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::AggroEstimate;
using ai::AggroEstimateParams;
using ai::AggroLink;
using ai::AggroMob;
using ai::AggroRadius;
using ai::EstimateAggro;
using ai::JoinersOverCeiling;

namespace
{
    // A level 60 creature of faction 14 with the default 20 yd detection.
    AggroMob Mob(float x, float y, float z = 0.0f)
    {
        AggroMob m;
        m.x = x;
        m.y = y;
        m.z = z;
        m.level = 60;
        m.faction = 14;
        return m;
    }

    // The party stands at the origin, level 60, 3 yd of drift, the core's
    // default rate and assist radius.
    AggroEstimateParams Params()
    {
        return AggroEstimateParams();
    }

    // The target sits far from the party (100 yd), so only its pack and the
    // assist hop reach what stands next to it; index 0 is the target.
    AggroMob Target()
    {
        return Mob(100.0f, 0.0f);
    }

    std::uint32_t Bodies(std::vector<AggroMob> const& mobs, AggroEstimateParams const& p = Params())
    {
        return EstimateAggro(mobs, 0, p).bodies;
    }
}

int main()
{
    std::cout << "Starting TortoiseBots pull aggro estimate tests...\n";

    // -------------------------------------------------------------
    // The aggro radius: Creature::GetAttackDistance.
    CHECK(AggroRadius(Mob(0, 0), 60, 1.0f) == 20.0f);          // same level
    CHECK(AggroRadius(Mob(0, 0), 55, 1.0f) == 25.0f);          // a lower party sees a wider radius
    CHECK(AggroRadius(Mob(0, 0), 65, 1.0f) == 15.0f);          // a higher party a narrower one
    CHECK(AggroRadius(Mob(0, 0), 30, 1.0f) == 45.0f);          // the difference stops at -25
    {
        AggroMob low = Mob(0, 0);
        low.level = 30;
        CHECK(AggroRadius(low, 70, 1.0f) == 5.0f);             // never under 5 yd
        low.detectionYd = 3.0f;
        CHECK(AggroRadius(low, 70, 1.0f) == 3.0f);             // ... or under its own detection
        low.detectionYd = 0.0f;
        CHECK(AggroRadius(low, 30, 1.0f) == 0.0f);             // no detection: never by proximity
        low.detectionYd = 0.5f;
        CHECK(AggroRadius(low, 30, 1.0f) == 0.0f);             // under 1 yd counts as none
        AggroMob quiet = Mob(0, 0);
        quiet.noAggro = true;
        CHECK(AggroRadius(quiet, 60, 1.0f) == 0.0f);
    }
    CHECK(AggroRadius(Mob(0, 0), 60, 2.0f) == 40.0f);           // Rate.Creature.Aggro
    CHECK(AggroRadius(Mob(0, 0), 60, 0.0f) == 0.0f);

    // -------------------------------------------------------------
    // A lone target: one body, no joiners; a bad index is all zero.
    {
        std::vector<AggroMob> mobs = {Target()};
        AggroEstimate e = EstimateAggro(mobs, 0, Params());
        CHECK(e.bodies == 1 && e.joiners == 0 && e.bodiesIfPatrolsPass == 1);
        e = EstimateAggro(mobs, 5, Params());
        CHECK(e.bodies == 0 && e.joiners == 0 && e.bodiesIfPatrolsPass == 0);
        CHECK(EstimateAggro(std::vector<AggroMob>(), 0, Params()).bodies == 0);
    }

    // -------------------------------------------------------------
    // The target's pack comes at any distance; another group does not.
    {
        std::vector<AggroMob> mobs = {Target(), Mob(400.0f, 400.0f), Mob(-300.0f, 0.0f), Mob(-300.0f, 5.0f)};
        mobs[0].groupId = 7;
        mobs[1].groupId = 7;
        mobs[2].groupId = 8;
        mobs[3].groupId = 8;
        CHECK(Bodies(mobs) == 2);
        // Group id 0 is "no group": two such creatures are not a pack.
        mobs[1].groupId = 0;
        mobs[0].groupId = 0;
        CHECK(Bodies(mobs) == 1);
    }

    // -------------------------------------------------------------
    // creature_linking edges are directed and chain.
    {
        std::vector<AggroMob> mobs = {Target(), Mob(500.0f, 0.0f), Mob(-500.0f, 0.0f)};
        AggroEstimateParams p = Params();
        // The target is a master whose aggro wakes a slave, and that slave wakes another.
        p.links = {AggroLink{0, 1}};
        CHECK(Bodies(mobs, p) == 2);
        p.links = {AggroLink{0, 1}, AggroLink{1, 2}};
        CHECK(Bodies(mobs, p) == 3);
        // ... whatever order the rows come in.
        p.links = {AggroLink{1, 2}, AggroLink{0, 1}};
        CHECK(Bodies(mobs, p) == 3);
        // The reverse edge (slave 1 wakes the target) does not wake 1 from the target.
        p.links = {AggroLink{1, 0}};
        CHECK(Bodies(mobs, p) == 1);
        // An edge pointing at an index outside the list is ignored.
        p.links = {AggroLink{0, 9}, AggroLink{9, 1}};
        CHECK(Bodies(mobs, p) == 1);
    }

    // -------------------------------------------------------------
    // Proximity: inside the aggro radius plus the drift of the party.
    {
        // Same level: 20 yd, plus 3 yd of drift.
        std::vector<AggroMob> mobs = {Target(), Mob(22.0f, 0.0f)};
        CHECK(Bodies(mobs) == 2);
        mobs[1].x = 23.5f;
        CHECK(Bodies(mobs) == 1);
        AggroEstimateParams p = Params();
        p.spreadYd = 0.0f;
        mobs[1].x = 20.5f;
        CHECK(Bodies(mobs, p) == 1);
        mobs[1].x = 19.5f;
        CHECK(Bodies(mobs, p) == 2);
        mobs[1].x = 20.0f; // exactly on the radius wakes
        CHECK(Bodies(mobs, p) == 2);

        // A mob measures from the engage point, not from the origin.
        AggroEstimateParams away = Params();
        away.engageX = 80.0f;
        mobs[1].x = 22.0f;
        CHECK(Bodies(mobs, away) == 1);
        away.engageX = 40.0f;
        CHECK(Bodies(mobs, away) == 2);

        // The lowest party level gives the wider radius (25 yd + 3 at level 55).
        mobs[1].x = 27.0f;
        CHECK(Bodies(mobs) == 1);
        AggroEstimateParams low = Params();
        low.lowestPartyLevel = 55;
        CHECK(Bodies(mobs, low) == 2);
        // A level far under the creature stops at 25 levels: 45 + 3 yd, not more.
        low.lowestPartyLevel = 30;
        mobs[1].x = 47.0f;
        CHECK(Bodies(mobs, low) == 2);
        mobs[1].x = 49.0f;
        CHECK(Bodies(mobs, low) == 1);
        // A party far over the creature still gets the floor of 5 yd + drift.
        AggroEstimateParams high = Params();
        high.lowestPartyLevel = 70;
        mobs[1].level = 30;
        mobs[1].x = 7.0f;
        CHECK(Bodies(mobs, high) == 2);
        mobs[1].x = 9.0f;
        CHECK(Bodies(mobs, high) == 1);

        // The distance is 3D: a mob a floor above does not wake.
        mobs[1] = Mob(0.0f, 0.0f, 25.0f);
        CHECK(Bodies(mobs) == 1);
        mobs[1].z = 15.0f;
        CHECK(Bodies(mobs) == 2);

        // The aggro rate scales the radius.
        mobs[1] = Mob(30.0f, 0.0f);
        CHECK(Bodies(mobs) == 1);
        AggroEstimateParams rate = Params();
        rate.aggroRate = 2.0f;
        CHECK(Bodies(mobs, rate) == 2);
        rate.aggroRate = 0.0f;
        mobs[1].x = 1.0f;
        CHECK(Bodies(mobs, rate) == 1);

        // No line of sight, no aggro flag, no detection: stays asleep.
        mobs[1] = Mob(5.0f, 0.0f);
        CHECK(Bodies(mobs) == 2);
        mobs[1].partySees = false; // no sight between it and the engage point
        CHECK(Bodies(mobs) == 1);
        mobs[1].partySees = true;
        mobs[1].seen = false; // the pulled creature not seeing it does not keep it asleep
        CHECK(Bodies(mobs) == 2);
        mobs[1].seen = true;
        mobs[1].noAggro = true;
        CHECK(Bodies(mobs) == 1);
        mobs[1].noAggro = false;
        mobs[1].detectionYd = 0.0f;
        CHECK(Bodies(mobs) == 1);
    }

    // -------------------------------------------------------------
    // The assist hop: same faction within 20 yd of a caller. The observed cross-pack wakes are spawn pairs 11.0-19.5
    // yd apart, so the server's 10 yd assistance radius is never the whole ring.
    {
        std::vector<AggroMob> mobs = {Target(), Mob(118.0f, 0.0f)};
        CHECK(Bodies(mobs) == 2); // 18 yd: inside the ring
        mobs[1].x = 120.0f; // exactly on the ring assists
        CHECK(Bodies(mobs) == 2);
        mobs[1].x = 121.0f;
        CHECK(Bodies(mobs) == 1);
        // Another faction template does not assist.
        mobs[1].x = 118.0f;
        mobs[1].faction = 15;
        CHECK(Bodies(mobs) == 1);
        mobs[1].faction = 14;
        // "No assist" and "no aggro" never join.
        mobs[1].noAssist = true;
        CHECK(Bodies(mobs) == 1);
        mobs[1].noAssist = false;
        mobs[1].noAggro = true;
        CHECK(Bodies(mobs) == 1);
        mobs[1].noAggro = false;
        // Out of sight of the pulled creature.
        mobs[1].seen = false;
        CHECK(Bodies(mobs) == 1);
        mobs[1].seen = true;
        // The party's sight of it does not matter for an assist.
        mobs[1].partySees = false;
        CHECK(Bodies(mobs) == 2);
        mobs[1].partySees = true;
        // A configured radius wider than the observed ring is read.
        mobs[1].x = 125.0f; // exactly on the wider ring
        AggroEstimateParams wide = Params();
        wide.familyAssistYd = 25.0f;
        CHECK(Bodies(mobs, wide) == 2);
        mobs[1].x = 126.0f;
        CHECK(Bodies(mobs, wide) == 1);
        mobs[1].x = 118.0f;
        CHECK(Bodies(mobs) == 2);
    }

    // The hop does not chain: the one assisted does not call.
    {
        // The second one stands 16 yd from the assisted one (24 yd from the target, outside the ring): only a chain
        // could take it.
        std::vector<AggroMob> mobs = {Target(), Mob(108.0f, 0.0f), Mob(124.0f, 0.0f)};
        CHECK(Bodies(mobs) == 2);
    }

    // A pack member dragged in by the pack calls too: the assist comes from every member of the pull's pack, not only
    // the pulled creature. A waker does as well, and so does a pack member that stands in reach itself.
    {
        std::vector<AggroMob> mobs = {Target(), Mob(100.0f, 30.0f), Mob(100.0f, 48.0f)};
        mobs[0].groupId = 3;
        mobs[1].groupId = 3;
        CHECK(Bodies(mobs) == 3); // 18 yd from the pack mate, 48 yd from the target
        // The proximity waker at 20 yd calls its neighbour 8 yd on (28 yd from
        // the party: out of its own reach).
        std::vector<AggroMob> waker = {Target(), Mob(20.0f, 0.0f), Mob(28.0f, 0.0f)};
        CHECK(Bodies(waker) == 3);
        // A pack member in reach of the party calls, too.
        mobs[1] = Mob(20.0f, 0.0f);
        mobs[1].groupId = 3;
        mobs[2] = Mob(28.0f, 0.0f);
        CHECK(Bodies(mobs) == 3);
    }

    // The ring starts from every pack member's spawn: the ZG terrace's cross-pack wakes are spawn pairs 11.0-19.5 yd
    // apart, and the woken creature was never near the pulled one itself.
    {
        std::vector<AggroMob> mobs = {Target(), Mob(100.0f, 30.0f), Mob(100.0f, 49.5f)};
        mobs[0].groupId = 3;
        mobs[1].groupId = 3;
        CHECK(Bodies(mobs) == 3); // 19.5 yd from the pack mate, 49.5 yd from the target
        mobs[2].y = 50.5f; // 20.5 yd: outside the ring
        CHECK(Bodies(mobs) == 2);
        // The assisted neighbour drags its own pack in.
        mobs[2].y = 49.5f;
        mobs[2].groupId = 4;
        mobs.push_back(Mob(500.0f, 500.0f));
        mobs[3].groupId = 4;
        CHECK(Bodies(mobs) == 4);
    }

    // An assisted creature drags its pack in, and that pack does not call.
    {
        std::vector<AggroMob> mobs = {Target(), Mob(108.0f, 0.0f), Mob(-400.0f, 0.0f), Mob(-408.0f, 0.0f)};
        mobs[1].groupId = 4;
        mobs[2].groupId = 4;
        CHECK(Bodies(mobs) == 3);
        // The same through a link.
        mobs[1].groupId = 0;
        mobs[2].groupId = 0;
        AggroEstimateParams p = Params();
        p.links = {AggroLink{1, 2}};
        CHECK(Bodies(mobs, p) == 3);
    }

    // -------------------------------------------------------------
    // Patrols: a lone patroller is left out in the second count.
    {
        std::vector<AggroMob> mobs = {Target(), Mob(10.0f, 0.0f), Mob(12.0f, 0.0f)};
        mobs[1].patroller = true;
        AggroEstimate e = EstimateAggro(mobs, 0, Params());
        CHECK(e.bodies == 3 && e.joiners == 2);
        CHECK(e.bodiesIfPatrolsPass == 2);
        // A patroller in a pack is not waited out.
        mobs[1].groupId = 9;
        mobs[2] = Mob(500.0f, 500.0f);
        mobs[2].groupId = 9;
        e = EstimateAggro(mobs, 0, Params());
        CHECK(e.bodies == 3 && e.bodiesIfPatrolsPass == 3);
        // The target itself is never left out.
        std::vector<AggroMob> self = {Target(), Mob(500.0f, 0.0f)};
        self[0].patroller = true;
        CHECK(EstimateAggro(self, 0, Params()).bodiesIfPatrolsPass == 1);
        // ... nor are its links dropped.
        AggroEstimateParams linked = Params();
        linked.links = {AggroLink{0, 1}};
        e = EstimateAggro(self, 0, linked);
        CHECK(e.bodies == 2 && e.bodiesIfPatrolsPass == 2);
        // A lone patroller does not carry a link or an assist while it is gone.
        std::vector<AggroMob> chain = {Target(), Mob(500.0f, 0.0f), Mob(-500.0f, 0.0f)};
        chain[1].patroller = true;
        AggroEstimateParams p = Params();
        p.links = {AggroLink{0, 1}, AggroLink{1, 2}};
        e = EstimateAggro(chain, 0, p);
        CHECK(e.bodies == 3 && e.bodiesIfPatrolsPass == 1);
        // A patroller that would assist is gone too.
        std::vector<AggroMob> near = {Target(), Mob(104.0f, 0.0f)};
        near[1].patroller = true;
        e = EstimateAggro(near, 0, Params());
        CHECK(e.bodies == 2 && e.bodiesIfPatrolsPass == 1);
    }

    // -------------------------------------------------------------
    // The ceiling.
    {
        AggroEstimate e;
        e.joiners = 7;
        e.bodiesIfPatrolsPass = 9;
        CHECK(JoinersOverCeiling(e, 6));
        CHECK(!e.patrolIsTheCause);                 // 8 joiners without the patrol is still over
        e.joiners = 7;
        e.bodiesIfPatrolsPass = 7;
        CHECK(JoinersOverCeiling(e, 6));
        CHECK(e.patrolIsTheCause);                  // 6 joiners once it is gone
        e.joiners = 6;
        CHECK(!JoinersOverCeiling(e, 6));           // exactly the ceiling is not over
        CHECK(!e.patrolIsTheCause);
        e.joiners = 50;
        CHECK(!JoinersOverCeiling(e, 0));           // 0 is off
    }

    std::cout << "pull aggro estimate policy: ok\n";
    return 0;
}
