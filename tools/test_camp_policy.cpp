#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include "ai/playerbot/policy/rpg/CampPolicy.h"

using namespace TortoiseBots;

int main()
{
    // Capital zones blocked
    assert(IsCampBlockedZone(1519));
    assert(IsCampBlockedZone(1637));
    assert(IsCampBlockedZone(1497));
    assert(IsCampBlockedZone(1537));
    assert(IsCampBlockedZone(1657));
    assert(IsCampBlockedZone(1638));
    assert(!IsCampBlockedZone(40));   // Westfall
    assert(!IsCampBlockedZone(33));   // Stranglethorn Vale

    // Tent GO entries
    assert(IsTentGameObjectEntry(1000001));
    assert(IsTentGameObjectEntry(1000236));
    assert(IsTentGameObjectEntry(1100000));
    assert(IsTentGameObjectEntry(1100001));
    assert(!IsTentGameObjectEntry(29784));

    // Time of day
    assert(IsCampNightHour(21));
    assert(IsCampNightHour(23));
    assert(IsCampNightHour(0));
    assert(IsCampNightHour(5));
    assert(!IsCampNightHour(6));
    assert(!IsCampNightHour(12));
    assert(!IsCampNightHour(20));
    assert(EffectiveCampChancePct(15, 30, false) == 15);
    assert(EffectiveCampChancePct(15, 30, true) == 30);

    // Eligibility: only autonomous free bots >= minLevel, outdoors/non-BG, no real player master/group/hire
    assert(IsBotEligibleForCamp(true, true, false, false, false, 10, 10, false, false));
    assert(!IsBotEligibleForCamp(false, true, false, false, false, 20, 10, false, false));
    assert(!IsBotEligibleForCamp(true, false, false, false, false, 20, 10, false, false));
    assert(!IsBotEligibleForCamp(true, true, true, false, false, 20, 10, false, false));
    assert(!IsBotEligibleForCamp(true, true, false, true, false, 20, 10, false, false));
    assert(!IsBotEligibleForCamp(true, true, false, false, true, 20, 10, false, false));
    assert(!IsBotEligibleForCamp(true, true, false, false, false, 9, 10, false, false));
    assert(!IsBotEligibleForCamp(true, true, false, false, false, 20, 10, true, false));
    assert(!IsBotEligibleForCamp(true, true, false, false, false, 20, 10, false, true));

    // Kit grant mirrors eligibility without dungeon/BG gate
    assert(BotWantsCampKit(true, true, false, false, false, 10, 10));
    assert(!BotWantsCampKit(true, true, false, false, false, 9, 10));
    assert(!BotWantsCampKit(true, true, true, false, false, 30, 10));

    // Start decision
    assert(ShouldStartCampAtSpot(true, false, true, true, false, true, false, 15, 0));
    assert(ShouldStartCampAtSpot(true, false, true, true, false, true, false, 15, 14));
    assert(!ShouldStartCampAtSpot(true, false, true, true, false, true, false, 15, 15));
    assert(!ShouldStartCampAtSpot(true, true, true, true, false, true, false, 100, 0));   // tent nearby
    assert(!ShouldStartCampAtSpot(true, false, false, true, false, true, false, 100, 0)); // spell on cd
    assert(!ShouldStartCampAtSpot(true, false, true, false, false, true, false, 100, 0)); // no item
    assert(!ShouldStartCampAtSpot(true, false, true, true, true, true, false, 100, 0));   // in water
    assert(!ShouldStartCampAtSpot(true, false, true, true, false, false, false, 100, 0)); // indoors
    assert(!ShouldStartCampAtSpot(true, false, true, true, false, true, true, 100, 0));   // capital
    assert(!ShouldStartCampAtSpot(true, false, true, true, false, true, false, 0, 0));    // 0% chance

    // Joining existing camp
    assert(ShouldJoinExistingCamp(true, true, false, true, false, 10, 9));
    assert(!ShouldJoinExistingCamp(true, true, false, true, false, 10, 10));
    assert(!ShouldJoinExistingCamp(true, false, false, true, false, 100, 0));
    assert(!ShouldJoinExistingCamp(false, true, false, true, false, 100, 0));
    assert(!ShouldJoinExistingCamp(true, true, true, true, false, 100, 0));
    assert(ComputeJoinSitDurationMs(60, 180, 0) == 60000);
    assert(ComputeJoinSitDurationMs(60, 180, 120) == 180000);
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    ComputeJoinSeatOffset(10.0f, 20.0f, 5.0f, 0.0f, 3.0f, ox, oy, oz);
    assert(std::fabs(ox - 13.0f) < 0.01f && std::fabs(oy - 20.0f) < 0.01f && std::fabs(oz - 5.0f) < 0.01f);

    // Abandonment decision
    assert(!ShouldAbandonCamp(false, true, true, true, 1000, 1200000, true, true));
    assert(ShouldAbandonCamp(true, true, true, true, 1000, 1200000, true, true));    // combat
    assert(ShouldAbandonCamp(false, false, true, true, 1000, 1200000, true, true));  // dead
    assert(ShouldAbandonCamp(false, true, false, true, 1000, 1200000, true, true));  // no longer eligible
    assert(ShouldAbandonCamp(false, true, true, false, 1000, 1200000, true, true));  // map changed
    assert(ShouldAbandonCamp(false, true, true, true, 1200000, 1200000, true, true));// sit cap reached
    assert(!ShouldAbandonCamp(false, true, true, true, 1000, 1200000, false, false));// before spawn grace
    assert(ShouldAbandonCamp(false, true, true, true, 6000, 1200000, false, true));  // tent gone after grace

    // Emote rotation (day vs night)
    assert(NextCampEmoteStep(CampEmoteStep::SitEat, true, false) == CampEmoteStep::Cook);
    assert(NextCampEmoteStep(CampEmoteStep::Cook, true, false) == CampEmoteStep::ClassFlavour);
    assert(NextCampEmoteStep(CampEmoteStep::ClassFlavour, true, false) == CampEmoteStep::Dance);
    assert(NextCampEmoteStep(CampEmoteStep::Dance, true, false) == CampEmoteStep::SitEat);
    assert(NextCampEmoteStep(CampEmoteStep::SitEat, false, false) == CampEmoteStep::ClassFlavour);

    assert(NextCampEmoteStep(CampEmoteStep::SitEat, true, true) == CampEmoteStep::Sleep);
    assert(NextCampEmoteStep(CampEmoteStep::Sleep, true, true) == CampEmoteStep::Cook);
    assert(NextCampEmoteStep(CampEmoteStep::Sleep, false, true) == CampEmoteStep::ClassFlavour);
    assert(NextCampEmoteStep(CampEmoteStep::ClassFlavour, false, true) == CampEmoteStep::SitEat);

    // Class flavour
    assert(PickCampFlavourAction(8, 70, false, 0).spellId == 746);   // hurt -> bandage
    assert(PickCampFlavourAction(3, 100, true, 0).petRest == true);  // hunter with pet -> feed + pet rest
    assert(PickCampFlavourAction(8, 100, false, 0).spellId == 5504); // mage -> conjure water
    assert(PickCampFlavourAction(8, 100, false, 1).spellId == 587);  // mage -> conjure food
    assert(PickCampFlavourAction(9, 100, false, 0).spellId == 6201); // warlock -> healthstone

    // Player greeting rate limits
    assert(ShouldGreetPlayerAtCamp(10000, 0, 0, 120000));
    assert(!ShouldGreetPlayerAtCamp(50000, 10000, 0, 120000));      // bot on cooldown
    assert(!ShouldGreetPlayerAtCamp(50000, 0, 10000, 120000));      // player on cooldown
    assert(ShouldGreetPlayerAtCamp(130000, 10000, 10000, 120000));
    assert(ShouldSayCampLine(15, 14));
    assert(!ShouldSayCampLine(15, 15));
    assert(std::strlen(PickCampGreetingLine(0)) > 0);

    // Rested XP restore
    assert(ShouldRestoreRestBonus(false, true));
    assert(!ShouldRestoreRestBonus(true, true));
    assert(!ShouldRestoreRestBonus(false, false));

    // Nearest spot picking
    std::vector<CampSpotCoord> spots = {
        {1, 1, 100.0f, 100.0f, 10.0f},  // wrong map
        {2, 0, 50.0f, 0.0f, 0.0f},      // 50 yd away (out of 40 yd range)
        {3, 0, 30.0f, 0.0f, 0.0f},      // 30 yd away
        {4, 0, 12.0f, 0.0f, 0.0f},      // 12 yd away (closest)
    };
    assert(PickNearestCampSpotIndex(0, 0.0f, 0.0f, 0.0f, 40.0f, spots) == 3);
    assert(PickNearestCampSpotIndex(0, 0.0f, 0.0f, 0.0f, 10.0f, spots) == -1);
    assert(PickNearestCampSpotIndex(1, 95.0f, 100.0f, 10.0f, 40.0f, spots) == 0);

    return 0;
}
