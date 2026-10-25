#include "playerbot/playerbot.h"
#include "RuinsOfAhnQirajActions.h"
#include "playerbot/policy/raid/OssirianCrystalPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <mutex>

using namespace ai;
namespace
{
bool ClickerAllowed(PlayerbotAI* ai)
{
    Player* bot=ai->GetBot();
    if (bot->GetMapId()!=509 || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        !bot->IsInCombat() || !bot->IsSelfMover() || bot->IsMoving() || ai->IsTank(bot) || ai->IsHeal(bot) ||
        bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) ||
        !ai->HasStrategy("ossirian crystal",BotState::BOT_STATE_COMBAT)) return false;
    AiObjectContext* context=ai->GetAiObjectContext();
    Unit* order=ai->GetUnit(AI_VALUE(ObjectGuid,"explicit attack target"));
    if ((order && PossibleTargetsValue::IsValid(order,bot,true)) || AI_VALUE(Unit*,"rti target")) return false;
    PositionMap& positions=AI_VALUE(PositionMap&,"position");
    auto hold=positions.find("pull hold");
    return !ai->HasStrategy("stay",BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("follow",BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("wait for attack",BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("grind",BotState::BOT_STATE_COMBAT) &&
        (hold==positions.end() || !hold->second.isSet());
}
Unit* ActiveOssirian(PlayerbotAI* ai)
{
    AiObjectContext* context=ai->GetAiObjectContext();
    for (ObjectGuid const& guid:AI_VALUE(std::list<ObjectGuid>,"attackers"))
    {
        Unit* boss=ai->GetUnit(guid);
        if (boss && boss->GetEntry()==15339 && boss->IsAlive() && boss->IsInCombat() && boss->IsInMap(ai->GetBot()))
            return boss;
    }
    return nullptr;
}
bool WeaknessDue(PlayerbotAI* ai,Unit* boss)
{
    bool has=false;
    int32 remaining=0;
    for (uint32 id:{25177u,25178u,25180u,25183u,25181u})
        if (Aura* aura=ai->GetAura(id,boss))
        {
            has=true;
            int32 duration=aura->GetAuraDuration();
            if (duration<0) return false;
            remaining=std::max(remaining,duration);
        }
    return OssirianWeaknessDue(boss->HasAura(25176),has,remaining);
}
GameObject* ReadyCrystal(PlayerbotAI* ai,Unit* boss)
{
    if (!ClickerAllowed(ai)) return nullptr;
    AiObjectContext* context=ai->GetAiObjectContext();
    Player* bot=ai->GetBot();
    GameObject* best=nullptr;
    for (ObjectGuid const& guid:AI_VALUE(std::list<ObjectGuid>,"nearest game objects"))
    {
        GameObject* go=ai->GetGameObject(guid);
        if (!go || go->GetEntry()!=180619 || !go->IsInMap(bot) || go->IsDeleted() || !go->isSpawned() ||
            go->getLootState()==GO_ACTIVATED || go->GetGoState()!=GO_STATE_READY ||
            go->HasFlag(GAMEOBJECT_FLAGS,GO_FLAG_NO_INTERACT | GO_FLAG_IN_USE) ||
            !go->IsAtInteractDistance(bot) || !bot->IsWithinLOSInMap(go) || !go->PlayerCanUse(bot)) continue;
        float const dx=go->GetPositionX()-boss->GetPositionX();
        float const dy=go->GetPositionY()-boss->GetPositionY();
        float const dz=go->GetPositionZ()-boss->GetPositionZ();
        if (!OssirianCrystalInRange(std::sqrt(dx*dx+dy*dy+dz*dz))) continue;
        if (!best || go->GetObjectGuid()<best->GetObjectGuid()) best=go;
    }
    return best;
}
struct ClickRecord { OssirianClickWindow window; uint32 lastSeen=0; };
std::mutex clickMutex;
std::map<std::pair<uint32,uint64>,ClickRecord> clicks;
GameObject* CrystalRequest(PlayerbotAI* ai,bool claim)
{
    if (!ClickerAllowed(ai)) return nullptr;
    Unit* boss=ActiveOssirian(ai);
    // A moving boss may leave the30yd area during the5sec crystal cast.
    // Human tank positioning is a prerequisite; never route or freeze the tank.
    if (!boss || boss->IsMoving() || !WeaknessDue(ai,boss)) return nullptr;
    Player* chosen=nullptr;
    GameObject* crystal=nullptr;
    for (Player* member:LiveGroupMembers(ai->GetBot()->GetGroup()))
    {
        if (!member || !member->IsInMap(boss)) continue;
        PlayerbotAI* candidate=PlayerbotAIStorage::Instance().GetAI(member);
        GameObject* go=candidate ? ReadyCrystal(candidate,boss) : nullptr;
        if (go && (!chosen || member->GetObjectGuid()<chosen->GetObjectGuid()))
        {
            chosen=member; crystal=go;
        }
    }
    if (chosen!=ai->GetBot() || !crystal) return nullptr;
    uint32 const now=WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(clickMutex);
    for (auto it=clicks.begin();it!=clicks.end();)
        if (now-it->second.lastSeen>90000) it=clicks.erase(it);
        else ++it;
    auto& record=clicks[{boss->GetInstanceId(),boss->GetObjectGuid().GetRawValue()}];
    record.lastSeen=now;
    uint64 const guid=crystal->GetObjectGuid().GetRawValue();
    bool const ready=claim ? record.window.Claim(guid,ai->GetBot()->GetObjectGuid().GetRawValue(),
        chosen->GetObjectGuid().GetRawValue(),now) : record.window.Ready(guid,now);
    return ready ? crystal : nullptr;
}
}

bool OssirianCrystalAction::isUseful() { return CrystalRequest(ai,false)!=nullptr; }
bool OssirianCrystalAction::Execute(Event& event)
{
    GameObject* crystal=CrystalRequest(ai,true);
    if (!crystal) return false;
    // Normal opcode handling rechecks use eligibility at execution time.
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
    *packet << crystal->GetObjectGuid();
    bot->GetSession()->QueuePacket(packet.release());
    return true; // request queued, not proof of a successful weakness application
}
