#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include "../ai/playerbot/policy/rpg/CampCommandPolicy.h"

using namespace TortoiseBots;

namespace
{

CampAddValidationInput MakeValidInput()
{
    CampAddValidationInput in;
    in.tent = {true, false, true, 10.0f};
    in.cast = {true, false, true, 10.5f};
    in.fire = {true, false, true, 11.2f};
    in.seat = {true, false, true, 9.8f};
    return in;
}

}  // namespace

int main()
{
    // 4-point spread and level check
    assert(std::fabs(ComputeCamp4PointSpread(10.0f, 10.5f, 11.2f, 9.8f) - 1.4f) < 1e-4f);
    assert(IsCamp4PointTerrainLevel(10.0f, 10.5f, 11.2f, 9.8f));
    assert(IsCamp4PointTerrainLevel(10.0f, 13.0f, 11.0f, 10.0f));
    assert(!IsCamp4PointTerrainLevel(10.0f, 13.1f, 11.0f, 10.0f));

    // Valid spot passes
    {
        CampAddValidationResult const res = EvaluateCampAddSpot(MakeValidInput());
        assert(res.failure == CampAddCheckFailure::None);
        assert(res.failedPoint == nullptr);
        assert(std::fabs(res.spreadYd - 1.4f) < 1e-4f);
    }

    // Nearby spot fails first
    {
        CampAddValidationInput in = MakeValidInput();
        in.nearbySpotExists = true;
        in.nearestSpotDistYd = 18.0f;
        CampAddValidationResult const res = EvaluateCampAddSpot(in);
        assert(res.failure == CampAddCheckFailure::TooCloseToExistingSpot);
    }

    // Indoors fails and identifies point
    {
        CampAddValidationInput in = MakeValidInput();
        in.fire.outdoors = false;
        CampAddValidationResult const res = EvaluateCampAddSpot(in);
        assert(res.failure == CampAddCheckFailure::NotOutdoors);
        assert(std::string(res.failedPoint) == "fire");
    }

    // Liquid fails and identifies point
    {
        CampAddValidationInput in = MakeValidInput();
        in.seat.inLiquid = true;
        CampAddValidationResult const res = EvaluateCampAddSpot(in);
        assert(res.failure == CampAddCheckFailure::InLiquid);
        assert(std::string(res.failedPoint) == "seat");
    }

    // Off-navmesh / unreachable fails and identifies point
    {
        CampAddValidationInput in = MakeValidInput();
        in.cast.onNavmeshAndReachable = false;
        CampAddValidationResult const res = EvaluateCampAddSpot(in);
        assert(res.failure == CampAddCheckFailure::NotOnNavmeshOrUnreachable);
        assert(std::string(res.failedPoint) == "cast");
    }

    // Steep terrain fails with spread reported
    {
        CampAddValidationInput in = MakeValidInput();
        in.tent.z = 10.0f;
        in.cast.z = 14.5f;
        CampAddValidationResult const res = EvaluateCampAddSpot(in);
        assert(res.failure == CampAddCheckFailure::TerrainTooSteep);
        assert(std::fabs(res.spreadYd - 4.7f) < 1e-4f);
    }

    // Nearest spot pick with distance
    {
        std::vector<CampSpotCoord> spots = {
            {1, 1, 0.0f, 0.0f, 0.0f},     // wrong map
            {2, 0, 50.0f, 0.0f, 0.0f},    // 50 yd away
            {3, 0, 3.0f, 4.0f, 0.0f},     // 5 yd away
            {4, 0, 12.0f, 0.0f, 0.0f},    // 12 yd away
        };
        NearestCampPick const within60 = PickNearestCampSpot(0, 0.0f, 0.0f, 0.0f, 60.0f, spots);
        assert(within60.index == 2);
        assert(std::fabs(within60.distanceYd - 5.0f) < 1e-4f);

        NearestCampPick const within4 = PickNearestCampSpot(0, 0.0f, 0.0f, 0.0f, 4.0f, spots);
        assert(within4.index == -1);
    }

    // Default spot name formatting
    assert(FormatDefaultCampSpotName("Elwynn Forest") == "Elwynn Forest camp");
    assert(FormatDefaultCampSpotName("") == "Camp");
    assert(FormatDefaultCampSpotName(std::string(120, 'a')).size() == CAMP_SPOT_NAME_MAX_LEN);

    return 0;
}
