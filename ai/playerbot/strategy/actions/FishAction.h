#pragma once
#include "playerbot/PlayerbotAI.h"
#include "CastCustomSpellAction.h"
#include "UseItemAction.h"


namespace ai
{
    // A fishing spot with a hostile creature spawn near the bot's level within `radius` (a
    // murloc camp on the shore, scouts by the river) is no place to stand still and fish:
    // 18 % of all deaths happened while fishing. Static spawn data, no grid needed.
    bool IsFishingSpotGuarded(Player* bot, WorldPosition const& spot, float radius = 35.0f);
    // TravelMgr::GetFishSpot, but up to eight candidates are tried until one is not guarded;
    // the last candidate is returned when every one of them is.
    WorldPosition* GetSafeFishSpot(Player* bot, bool onlyNearestGrid = false);
    // Open-water search (issue #402, ported from mod-playerbots `FindWaterRadial` /
    // `FindFishingHole`): fishable water within `searchRadius` of `from`, else an
    // invalid position. The pool-bot-only direct-fishing fallback when the travel
    // fish table is empty; bounded (fixed rings x directions) and cheap, and it
    // reads already-loaded terrain like any path query - no world scan.
    WorldPosition FindNearbyWater(Player* bot, WorldPosition const& from, float searchRadius);
    // Nearest visible fishing hole (school) within `searchRadius`, else invalid.
    // Fishing prefers the hole at cast time; the search walks to it only when
    // the hole itself is out of casting range from dry land.
    WorldPosition FindNearbyFishingHole(PlayerbotAI* ai, Player* bot, float searchRadius);
    // Cast point on dry land `from` facing `water`: ground at the bot, water
    // 10-20 yd out with line of sight, invalid when the bot must move first.
    WorldPosition GetShoreCastSpot(Player* bot, WorldPosition const& from, WorldPosition const& water);
    // Nearest dry stand toward `water` that can cast at it (sampled along the
    // bot-to-water ray inside `searchRadius`), invalid when none is. Lets an
    // idle pool bot walk a short step to the shore instead of only fishing
    // when it happens to stand in casting range already.
    WorldPosition StepTowardFishableWater(Player* bot, WorldPosition const& from, WorldPosition const& water, float searchRadius);

    class MoveToFishAction : public MovementAction, public Qualified
    {
    public:
        MoveToFishAction(PlayerbotAI* ai) : MovementAction(ai, "move to fish"), Qualified() {}
        virtual bool isUseful() override;
        virtual bool Execute(Event& event) override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "move to fish"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot move to a fishing location.\n"
                   "It identifies suitable spots for fishing and navigates there.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif
    };

    class FishAction : public CastCustomSpellAction
    {
    public:
        FishAction(PlayerbotAI* ai) : CastCustomSpellAction(ai, "fish") {}
        virtual bool isUseful() override;
        virtual bool Execute(Event& event) override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "fish"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot perform fishing.\n"
                   "It casts the fishing spell to catch fish at the current location.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif
    };

    class UseFishingBobberAction : public UseAction
    {
    public:
        UseFishingBobberAction(PlayerbotAI* ai) : UseAction(ai, "use fishing bobber") {}

        bool Execute(Event& event) override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "use fishing bobber"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot use a fishing bobber.\n"
                   "It interacts with the bobber to complete the fishing process.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif
    };
}
