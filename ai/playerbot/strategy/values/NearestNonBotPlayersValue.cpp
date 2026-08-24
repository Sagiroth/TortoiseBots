
#include "playerbot/playerbot.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "NearestNonBotPlayersValue.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;
using namespace MaNGOS;

void NearestNonBotPlayersValue::FindUnits(std::list<Unit*> &targets)
{
    AnyUnitInObjectRangeCheck u_check(bot, range);
    UnitListSearcher<AnyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestNonBotPlayersValue::AcceptUnit(Unit* unit)
{
    ObjectGuid guid = unit->getObjectGuid();
#ifdef MANGOS
    return guid.IsPlayer() && !PlayerbotAIStorage::Instance().GetAI((Player*)unit) && (!((Player*)unit)->IsGameMaster() || ((Player*)unit)->IsGMVisible());
#endif
#ifdef CMANGOS
    return guid.IsPlayer() && !PlayerbotAIStorage::Instance().GetAI((Player*)unit) && (!((Player*)unit)->IsGameMaster() || ((Player*)unit)->IsGMVisible());
#endif
}