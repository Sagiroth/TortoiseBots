#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/actions/GenericActions.h"

class Map;

namespace ai
{
    // Raid/dungeon follow wedge (AiPlayerbot.RaidFollowWedge, RaidFollowWedgePolicy.h):
    // a group inside an instance follows in the near formation behind the leader.
    bool UsesRaidFollowWedge(PlayerbotAI* ai);

    // Shared ground/LOS guard for every formation's follow spot
    // (FollowSpotSafetyPolicy.h): false when the floor below the spot is a
    // drop the leader is not standing on. `from*` is the formation center the
    // line of sight is tested from. `refuseVoid`: a formation that always
    // refused a spot with no floor under it (the circle, the line rows, the
    // arrow) keeps refusing it.
    bool FinalizeFollowSpot(Player* bot, Map* map, float fromX, float fromY, float fromZ,
        float x, float y, float& z, bool refuseVoid = false);

    class Formation : public AiNamedObject
    {
    public:
        Formation(PlayerbotAI* ai, std::string name) : AiNamedObject (ai, name), serial(++lastSerial) {}

    public:
        virtual std::string GetTargetName() { return ""; }
        virtual WorldLocation GetLocation() { return NullLocation; }
        virtual float GetAngle();
        virtual float GetOffset();
        virtual float getOffset() { return GetOffset(); }
        virtual float GetMaxDistance();
        static WorldLocation NullLocation;
		static bool IsNullLocation(WorldLocation const& loc);
        // Per object, not per name: .bot formation replaces the object on every
        // set, even with the same name, so the station trigger keys on this.
        unsigned int GetSerial() const { return serial; }

    protected:
        float GetFollowAngle();
        // The bot's place in the follower roster built by GetFollowAngle:
        // index 1..total-1, total = roster size + 1 (the leader fills 1).
        void GetFollowSlot(int& index, int& total);

    private:
        static unsigned int lastSerial;
        unsigned int const serial;
    };

    class FollowFormation : public Formation
    {
    public:
        FollowFormation(PlayerbotAI* ai, std::string name) : Formation(ai, name) {}
        virtual WorldLocation GetLocation();
    };

    class MoveFormation : public Formation
    {
    public:
        MoveFormation(PlayerbotAI* ai, std::string name) : Formation(ai, name) {}

    protected:
        WorldLocation MoveLine(std::vector<Player*> line, float diff, float cx, float cy, float cz, float orientation, float range);
        WorldLocation MoveSingleLine(std::vector<Player*> line, float diff, float cx, float cy, float cz, float orientation, float range);
    };

    class MoveAheadFormation : public MoveFormation
    {
    public:
        MoveAheadFormation(PlayerbotAI* ai, std::string name) : MoveFormation(ai, name) {}
        virtual WorldLocation GetLocation() override;
        virtual WorldLocation GetLocationInternal() { return NullLocation; }
    };

    class FormationValue : public ManualSetValue<Formation*>
	{
	public:
        FormationValue(PlayerbotAI* ai);
        ~FormationValue() override { if (value) { delete value; value = NULL; } if (raidValue) delete raidValue; }
        virtual Formation* Get() override;
        virtual void Reset() override;
        virtual std::string Save() override;
        virtual bool Load(std::string value) override;
        // An explicit player choice (`.bot formation`): it overrides the raid
        // wedge for this bot until the formation is reset.
        bool LoadUserChoice(std::string value);

    private:
        // The near formation the raid/dungeon wedge uses, built on first use.
        Formation* raidValue = NULL;
        bool userChosen = false;
	};

    class FormationPositionValue : public CalculatedValue<WorldPosition>
    {
    public:
        FormationPositionValue(PlayerbotAI* ai, std::string name = "formation position") : CalculatedValue(ai, name) {}

        virtual WorldPosition Calculate() override {
            Formation* formation = AI_VALUE(Formation*, "formation");

            WorldLocation loc = formation->GetLocation();
            if (Formation::IsNullLocation(loc) || loc.mapId == -1)
                return WorldLocation();

            return loc;
        }
    };

    class SetFormationAction : public ChatCommandAction
    {
    public:
        SetFormationAction(PlayerbotAI* ai) : ChatCommandAction(ai, "set formation") {}
        virtual bool Execute(Event& event) override;
    };
};
