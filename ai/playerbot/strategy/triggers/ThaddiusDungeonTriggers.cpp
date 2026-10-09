#include "playerbot/playerbot.h"
#include "ThaddiusDungeonTriggers.h"
#include "playerbot/ThaddiusPolarityPolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    Unit* FindEntry(PlayerbotAI* ai, Player* bot, uint32 entry)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == entry)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, entry, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive())
                return unit;
        }
        return nullptr;
    }

    bool PetActive(Unit* pet)
    {
        return pet && IsThaddiusPetActive(pet->IsAlive(),
            pet->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE));
    }

    void FindAdds(PlayerbotAI* ai, Player* bot, Unit*& stalagg, Unit*& feugen)
    {
        stalagg = FindEntry(ai, bot, 15929);
        feugen = FindEntry(ai, bot, 15930);
    }
}

bool ThaddiusPhasePetTrigger::IsActive()
{
    Unit* stalagg;
    Unit* feugen;
    FindAdds(ai, bot, stalagg, feugen);
    return IsThaddiusPhasePet(PetActive(feugen), PetActive(stalagg));
}

bool ThaddiusPhaseTransitionTrigger::IsActive()
{
    Unit* stalagg;
    Unit* feugen;
    FindAdds(ai, bot, stalagg, feugen);
    if (IsThaddiusPhasePet(PetActive(feugen), PetActive(stalagg)))
        return false;
    Unit* thaddius = FindEntry(ai, bot, 15928);
    return thaddius && IsThaddiusPhaseTransition(false,
        thaddius->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE));
}

bool ThaddiusPhaseThaddiusTrigger::IsActive()
{
    Unit* stalagg;
    Unit* feugen;
    FindAdds(ai, bot, stalagg, feugen);
    if (IsThaddiusPhasePet(PetActive(feugen), PetActive(stalagg)))
        return false;
    Unit* thaddius = FindEntry(ai, bot, 15928);
    return thaddius && !thaddius->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
}
