#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonActions.h"
#include "playerbot/policy/raid/OnyxiaBreathPolicy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "Spells/SpellMgr.h"
#include "ObjectMgr.h"
#include <map>
#include <mutex>

using namespace ai;
namespace
{
struct BreathObservation
{
    uint32 observed=0, instance=0, spell=0;
    ObjectGuid boss;
    std::vector<OnyxiaBreathCircle> circles;
};
std::mutex breathMutex;
std::map<uint64,BreathObservation> breathObservations;

bool BreathHoldOrder(PlayerbotAI* ai)
{
    AiObjectContext* context=ai->GetAiObjectContext();
    PositionMap& positions=AI_VALUE(PositionMap&, "position");
    auto hold=positions.find("pull hold");
    return ai->HasStrategy("stay",BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow",BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack",BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind",BotState::BOT_STATE_COMBAT) ||
        (hold!=positions.end() && hold->second.isSet());
}

bool GetBreathObservation(PlayerbotAI* ai, BreathObservation& result)
{
    Player* bot=ai->GetBot();
    if (bot->GetMapId()!=249 || !ai->HasStrategy("onyxia deep breath",BotState::BOT_STATE_COMBAT))
        return false;
    Unit* boss=GetAirborneOnyxia(ai);
    bool const hold=BreathHoldOrder(ai);
    uint32 const now=WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(breathMutex);
    auto it=breathObservations.find(bot->GetObjectGuid().GetRawValue());
    if (it==breathObservations.end()) return false;
    if (!boss || it->second.boss!=boss->GetObjectGuid() || it->second.instance!=bot->GetInstanceId() ||
        !OnyxiaBreathFresh(now,it->second.observed))
    {
        breathObservations.erase(it);
        return false;
    }
    if (hold || !OnyxiaBreathFloor(bot->GetPositionX(),bot->GetPositionY(),bot->GetPositionZ())) return false;
    result=it->second;
    return true;
}

bool LoadBreathCircles(uint32 spell,Unit* boss,std::vector<OnyxiaBreathCircle>& circles)
{
    uint32 duration=0;
    std::vector<uint32> seen;
    for (unsigned count=0;spell && count<32;++count)
    {
        if (std::find(seen.begin(),seen.end(),spell)!=seen.end()) return false;
        seen.push_back(spell);
        auto* entry=sSpellTemplate.LookupEntry<SpellEntry>(spell);
        auto* position=sSpellMgr.GetSpellTargetPosition(spell);
        auto* radius=entry ? sSpellRadiusStore.LookupEntry(entry->EffectRadiusIndex[EFFECT_INDEX_0]) : nullptr;
        if (!entry || !position || position->mapId!=249 || !radius || radius->Radius<=0 ||
            entry->Effect[EFFECT_INDEX_0]!=SPELL_EFFECT_SCHOOL_DAMAGE)
            return false;
        duration+=entry->GetCastTime(boss);
        if (duration>14000) return false; // bounded hold must cover the complete chain
        circles.push_back({position->x,position->y,radius->Radius});
        spell=entry->EffectTriggerSpell[EFFECT_INDEX_1];
    }
    return !spell && !circles.empty();
}
}

void ai::ObserveOnyxiaBreathPacket(PlayerbotAI* ai,WorldPacket const& packet)
{
    Player* bot=ai->GetBot();
    if (bot->GetMapId()!=249 || !bot->IsInWorld() || !bot->IsAlive() ||
        !ai->HasStrategy("onyxia deep breath",BotState::BOT_STATE_COMBAT) ||
        packet.getOpcode()!=SMSG_MESSAGECHAT || packet.size()<5)
        return;
    uint8 const type=packet.contents()[0];
    if (type!=CHAT_MSG_MONSTER_EMOTE && type!=CHAT_MSG_RAID_BOSS_EMOTE) return;
    std::string name,message;
    if (!ReadOnyxiaBossEmote(packet.contents(),packet.size(),name,message)) return;
    Unit* boss=GetAirborneOnyxia(ai);
    if (!boss) return;
    int32 const locale=bot->GetSession()->GetSessionDbLocaleIndex();
    if (name!=boss->GetNameForLocaleIdx(locale) || message!=sObjectMgr.GetMangosString(-1249004,locale))
        return;
    BreathObservation observation;
    observation.spell=OnyxiaBreathDirection(boss->GetPositionX(),boss->GetPositionY());
    if (!observation.spell || !LoadBreathCircles(observation.spell,boss,observation.circles)) return;
    observation.observed=WorldTimer::getMSTime();
    observation.instance=bot->GetInstanceId();
    observation.boss=boss->GetObjectGuid();
    std::lock_guard<std::mutex> lock(breathMutex);
    for (auto it=breathObservations.begin();it!=breathObservations.end();)
        if (!OnyxiaBreathFresh(observation.observed,it->second.observed)) it=breathObservations.erase(it);
        else ++it;
    auto it=breathObservations.find(bot->GetObjectGuid().GetRawValue());
    if (it!=breathObservations.end() && it->second.boss==observation.boss &&
        observation.observed-it->second.observed<1000) return;
    breathObservations[bot->GetObjectGuid().GetRawValue()]=observation;
}

bool ai::OnyxiaBreathActive(PlayerbotAI* ai)
{
    BreathObservation observation;
    return GetBreathObservation(ai,observation);
}

bool OnyxiaBreathEscapeAction::isUseful()
{
    BreathObservation observation;
    return MovementAction::isUseful() && GetBreathObservation(ai,observation) &&
        OnyxiaBreathClearance(bot->GetPositionX(),bot->GetPositionY(),observation.circles)<3.0f;
}

bool OnyxiaBreathEscapeAction::Execute(Event& event)
{
    BreathObservation observation;
    if (!GetBreathObservation(ai,observation)) return false;
    WorldPosition const start(bot);
    float const startClearance=OnyxiaBreathClearance(start.getX(),start.getY(),observation.circles);
    if (startClearance>=3.0f) return false;
    ai->StopMoving(); // also stop a pre-existing chase if no route can be accepted
    if (!OnyxiaBreathFloor(start.getX(),start.getY(),start.getZ())) return false;
    std::vector<WorldPosition> candidates;
    // A bounded grid supplies points inside the perimeter, not fixed guesses
    // about a universally safe flank. Every point is checked against the full chain.
    for (float x=-64;x<=22;x+=2)
        for (float y=-250;y<=-180;y+=2)
            if (OnyxiaBreathFloor(x,y,start.getZ()) &&
                OnyxiaBreathClearance(x,y,observation.circles)>=3.0f)
                candidates.emplace_back(249,x,y,start.getZ());
    std::sort(candidates.begin(),candidates.end(),[&](WorldPosition const& a,WorldPosition const& b) {
        return start.sqDistance(a)<start.sqDistance(b);
    });
    unsigned attempted=0;
    std::vector<WorldPosition> attemptedPoints;
    for (WorldPosition point:candidates)
    {
        bool nearbyAttempt=false;
        for (auto const& tried:attemptedPoints)
            if (tried.distance(point)<4) nearbyAttempt=true;
        if (nearbyAttempt) continue;
        if (++attempted>24) break;
        attemptedPoints.push_back(point);
        point.setZ(point.GetHeight());
        if (!OnyxiaBreathFloor(point.getX(),point.getY(),point.getZ()) ||
            fabs(point.getZ()-start.getZ())>5 || IsHazardNearPosition(point) ||
            !bot->IsWithinLOS(point.getX(),point.getY(),point.getZ()+bot->GetCollisionHeight())) continue;
        auto const path=start.GetPathTo(point,bot);
        if (!point.isPathTo(path)) continue;
        bool safe=true;
        float length=0;
        WorldPosition previous=start;
        for (WorldPosition const& next:path)
        {
            float distance=previous.distance(next);
            length+=distance;
            if (!std::isfinite(distance) || length>50) { safe=false; break; }
            unsigned const steps=std::max(1u,unsigned(std::ceil(distance)));
            for (unsigned i=1;i<=steps;++i)
            {
                float t=float(i)/steps;
                WorldPosition sample(249,previous.getX()+(next.getX()-previous.getX())*t,
                    previous.getY()+(next.getY()-previous.getY())*t,
                    previous.getZ()+(next.getZ()-previous.getZ())*t);
                if (!OnyxiaBreathFloor(sample.getX(),sample.getY(),sample.getZ()) ||
                    IsHazardNearPosition(sample) ||
                    OnyxiaBreathClearance(sample.getX(),sample.getY(),observation.circles)<startClearance-0.5f)
                    safe=false;
            }
            previous=next;
        }
        if (!safe || length>50) continue;
        bool unpulled=false;
        for (ObjectGuid const& guid:AI_VALUE(std::list<ObjectGuid>,"possible targets"))
        {
            Unit* unit=ai->GetUnit(guid);
            if (unit && unit->IsAlive() && !unit->IsInCombat() &&
                sServerFacade.getDistance2d(unit,point.getX(),point.getY())<sPlayerbotAIConfig.aggroDistance)
                unpulled=true;
        }
        if (!unpulled && MoveTo(249,point.getX(),point.getY(),point.getZ(),false,IsReaction(),false,true))
        {
            if (IsReaction()) WaitForReach(length);
            return true;
        }
    }
    return false;
}
