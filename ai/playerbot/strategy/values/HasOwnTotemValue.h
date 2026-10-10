#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "TargetValue.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/ServerFacade.h"

namespace ai
{
    // Owner-scoped twin of HasTotemValue: true only for the bot's OWN totems
    // in spell range. HasTotemValue is group-aware (same-subgroup members'
    // totems count) so totem-placing triggers avoid duplicates — but recall
    // can only destroy our own totems, so the recall trigger must not fire
    // on (or spare) other shamans' totems.
    class HasOwnTotemValue : public BoolCalculatedValue, public Qualified
	{
	public:
        HasOwnTotemValue(PlayerbotAI* ai, std::string name = "has own totem") : BoolCalculatedValue(ai, name), Qualified() {}

        bool Calculate() override
        {
            std::list<ObjectGuid> units = *context->GetValue<std::list<ObjectGuid> >("nearest npcs");
            for (std::list<ObjectGuid>::iterator i = units.begin(); i != units.end(); i++)
            {
                Unit* unit = ai->GetUnit(*i);
                if (!unit)
                    continue;

                Creature* totem = dynamic_cast<Creature*>(unit);
                if (!totem || !totem->IsTotem())
                    continue;

                if (!strstri(totem->GetName(), qualifier.c_str()))
                    continue;

                if (sServerFacade.GetDistance2d(bot, totem) > ai->GetRange("spell"))
                    continue;

                if (totem->GetOwner() == bot)
                    return true;
            }

            return false;
        }
    };

    // Owner-scoped twin of HaveAnyTotemValue: true only when at least one of
    // the bot's OWN totems is down. Totemic Recall refunds only our own
    // totems, so standing near another shaman's totems must not trigger it.
    class HaveAnyOwnTotemValue : public BoolCalculatedValue, public Qualified
    {
	public:
        HaveAnyOwnTotemValue(PlayerbotAI* ai, std::string name = "have any own totem") : BoolCalculatedValue(ai, name), Qualified() {}

        bool Calculate() override
        {
            std::list<ObjectGuid> units = *context->GetValue<std::list<ObjectGuid> >("nearest npcs");
            for (std::list<ObjectGuid>::iterator i = units.begin(); i != units.end(); i++)
            {
                Unit* unit = ai->GetUnit(*i);
                if (!unit)
                    continue;

                Creature* totem = dynamic_cast<Creature*>(unit);
                if (!totem || !totem->IsTotem())
                    continue;

                if (totem->GetOwner() == bot)
                    return true;
            }

            return false;
        }
    };
}
