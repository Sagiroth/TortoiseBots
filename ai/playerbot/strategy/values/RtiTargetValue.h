#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Value.h"
#include "Group/Group.h"
#include "TargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "FocusMarkPolicy.h"

namespace ai
{
    class RtiTargetValue : public TargetValue
    {
    public:
        RtiTargetValue(PlayerbotAI* ai, std::string type = "rti", std::string name = "rti target") : type(type), TargetValue(ai,name)
        {}

    public:
        static int GetRtiIndex(std::string rti)
        {
            int index = -1;
            if(rti == "star") index = 0;
            else if(rti == "circle") index = 1;
            else if(rti == "diamond") index = 2;
            else if(rti == "triangle") index = 3;
            else if(rti == "moon") index = 4;
            else if(rti == "square") index = 5;
            else if(rti == "cross") index = 6;
            else if(rti == "skull") index = 7;
            return index;
        }

        Unit *Calculate() override
        {
            return CalculateMark(AI_VALUE(std::string, type));
        }

        // The bot's own focus mark, or Skull when the own mark is on no
        // reachable mob. Only the damage and tank picks use the fallback; the
        // CC mark (RtiCcTargetValue) never does.
        Unit* CalculateFocus()
        {
            std::string const own = AI_VALUE(std::string, type);
            if (Unit* unit = CalculateMark(own))
                return unit;
            std::string const fallback = FocusFallbackMark(own, false);
            return fallback.empty() ? nullptr : CalculateMark(fallback);
        }

    protected:
        // The mark word on this unit in the bot's group, or "" when unmarked.
        std::string MarkOn(Unit* unit)
        {
            static char const* const words[] = { "star", "circle", "diamond", "triangle", "moon", "square", "cross", "skull" };
            Group* group = bot->GetGroup();
            if (!group || !unit)
                return std::string();
            for (uint8 i = 0; i < 8; ++i)
                if (group->GetTargetIcon(i) == unit->GetObjectGuid())
                    return words[i];
            return std::string();
        }

        Unit* CalculateMark(std::string const& rti)
        {
            Group *group = bot->GetGroup();
            if(!group)
                return NULL;

            int index = GetRtiIndex(rti);

            if (index == -1)
                return NULL;

            ObjectGuid guid = group->GetTargetIcon(static_cast<uint8>(index));
            if (!guid)
                return NULL;

            // Pre-pull marks are not in "possible targets" yet (that value only
            // holds units already fighting the bot): like the donor
            // (RtiTargetValue::Calculate, attackers gate deleted), accept a
            // marked unit on legality + range + LOS instead of combat state.
            Unit* unit = ai->GetUnit(ObjectGuid(guid));
            if (!unit || sServerFacade.UnitIsDead(unit))
                return NULL;

            if (!PossibleAttackTargetsValue::IsPossibleTarget(unit, bot, sPlayerbotAIConfig.sightDistance, false))
                return NULL;

            if (!bot->IsWithinLOSInMap(unit))
                return NULL;

            return unit;
        }

    private:
        std::string type;
    };

    class RtiCcTargetValue : public RtiTargetValue
    {
    public:
        RtiCcTargetValue(PlayerbotAI* ai, std::string name = "rti cc target") : RtiTargetValue(ai, "rti cc", name) {}
    };
}
