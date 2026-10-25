#!/usr/bin/env python3
"""Exercise production warning handling with core DoMovement/packet encoding.
Usage: python3 tools/test_onyxia_breath_diagnostics.py CORE_CHECKOUT
Snapshot: Turtle live directional chain rows, 2026-10-06; radius10=30 yards.
Requires a C++17 compiler. No database, server, or network access.
"""
from pathlib import Path
import json, subprocess, os, sys, tempfile
r = Path(__file__).resolve().parents[1]
core = Path(sys.argv[1])
workspace = tempfile.TemporaryDirectory(prefix="onyxia-diagnostics-")
w = Path(workspace.name)
def function(text,signature):
 a=text.index(signature);b=text.index('{',a)+1;depth=1
 while depth:
  depth+=(text[b]=='{')-(text[b]=='}');b+=1
 return text[a:b]
policy=(r/'ai/playerbot/policy/raid/OnyxiaBreathPolicy.h').read_text().replace('#pragma once','')
source=(r/'ai/playerbot/strategy/actions/OnyxiaBreathActions.cpp').read_text()
chat=(core/'src/game/Chat/Chat.cpp').read_text();boss=(core/'src/scripts/dungeons/onyxias_lair/boss_onyxia.cpp').read_text()
fixture=policy+r'''
#include <cassert>
#include <map>
#include <list>
#include <mutex>
#include <type_traits>
#include <iostream>
#include <sstream>
using uint8=uint8_t; using uint16=uint16_t; using uint32=uint32_t; using uint64=uint64_t; using int32=int32_t;
using namespace ai;
struct ObjectGuid {
 uint64 raw=0; ObjectGuid()=default; ObjectGuid(uint64 v):raw(v){} uint64 GetRawValue()const{return raw;}
 bool operator==(ObjectGuid b)const{return raw==b.raw;} bool operator!=(ObjectGuid b)const{return raw!=b.raw;}
};
enum ChatMsg {CHAT_MSG_MONSTER_WHISPER=26,CHAT_MSG_RAID_BOSS_WHISPER=89,CHAT_MSG_RAID_BOSS_EMOTE=90,
 CHAT_MSG_MONSTER_EMOTE=13,CHAT_MSG_SAY=0,CHAT_MSG_PARTY=1,CHAT_MSG_YELL=5,
 CHAT_MSG_MONSTER_SAY=12,CHAT_MSG_MONSTER_YELL=14,CHAT_MSG_CHANNEL=17};
enum Language {LANG_UNIVERSAL=0,LANG_ADDON=0xFFFFFFFF};
constexpr unsigned CHAT_TAG_NONE=0,CHAT_TAG_GM=4,SMSG_MESSAGECHAT=1;
#define MANGOS_ASSERT assert
struct WorldPacket {
 unsigned opcode=0;std::vector<uint8> bytes;
 void Initialize(unsigned v){opcode=v;bytes.clear();}
 unsigned getOpcode()const{return opcode;} size_t size()const{return bytes.size();}
 uint8 const* contents()const{return bytes.data();}
 template<class T> WorldPacket& operator<<(T v){for(size_t i=0;i<sizeof(T);++i)bytes.push_back((uint64(v)>>(i*8))&255);return *this;}
 WorldPacket& operator<<(ObjectGuid v){return *this<<v.raw;}
 WorldPacket& operator<<(std::string const& s){bytes.insert(bytes.end(),s.begin(),s.end());bytes.push_back(0);return *this;}
 WorldPacket& operator<<(char const* s){return *this<<std::string(s);}
};
struct ChatHandler {static void BuildChatPacket(WorldPacket&,ChatMsg,std::string const&,Language,uint32,ObjectGuid const&,char const*,ObjectGuid const&,char const*,char const*,uint8);};
struct Session {int32 GetSessionDbLocaleIndex(){return -1;}};
struct Motion {void MovePoint(unsigned,float,float,float,unsigned){}};
struct Unit {
 unsigned map=249,instance=1,entry=10184;bool alive=true,combat=true,hover=true,flying=true;
 float x=0,y=0,z=-65; ObjectGuid guid{99}; Motion motion;uint32 cast=0;
 bool IsAlive(){return alive;}bool IsInCombat(){return combat;}
 bool HasAura(unsigned v){return v==17131 && hover;}bool IsFlying(){return flying;}
 unsigned GetEntry(){return entry;}bool IsInMap(Unit* b){return map==b->map&&instance==b->instance;}
 float GetPositionX(){return x;}float GetPositionY(){return y;}float GetPositionZ(){return z;}
 ObjectGuid GetObjectGuid(){return guid;}std::string GetNameForLocaleIdx(int){return "Onyxia";}
 void InterruptNonMeleeSpells(bool){}Motion* GetMotionMaster(){return &motion;}
 void CastSpell(Unit*,uint32 id,bool triggered){assert(triggered);cast=id;}
 void SetFacingTo(float){}float GetAngle(float,float){return 0;}void SetTargetGuid(ObjectGuid){}
 void SetSpeedRate(unsigned,float){}
};
struct Player:Unit {
 Session session;bool world=true,teleported=false;
 unsigned GetMapId(){return map;}unsigned GetInstanceId(){return instance;}
 bool IsInWorld(){return world;}bool IsBeingTeleported(){return teleported;}Session* GetSession(){return &session;}
};
struct Position {bool isSet(){return false;}};using PositionMap=std::map<std::string,Position>;
struct AiObjectContext {PositionMap positions;std::list<ObjectGuid> attackers{{99}};
 template<class T> T Value(const char*){
  if constexpr(std::is_same_v<T,PositionMap&>)return positions;else return attackers;
 }
};
#define AI_VALUE(type,key) context->Value<type>(key)
enum class BotState {BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT};
struct PlayerbotAI {
 Player bot;Unit boss;AiObjectContext context;bool enabled=true,hold=false,debug=true;
 PlayerbotAI(){bot.guid=ObjectGuid(1);bot.x=-25;bot.y=-215;bot.z=-89;}
 Player* GetBot(){return &bot;}AiObjectContext* GetAiObjectContext(){return &context;}
 Unit* GetUnit(ObjectGuid g){return g.raw==99?&boss:nullptr;}
 bool HasStrategy(std::string s,BotState){return s=="onyxia deep breath"?enabled:s=="debug"?debug:s=="stay"&&hold;}
};
struct Config {
 std::vector<std::string> stages;
 void logEvent(PlayerbotAI*,std::string event,std::string stage,std::string){assert(event=="OnyxiaBreath");stages.push_back(stage);}
} sPlayerbotAIConfig;
uint32 nowMs=100;
struct WorldTimer {static uint32 getMSTime(){return nowMs;}};
struct ObjectMgr {const char* GetMangosString(int32 id,int32){assert(id==-1249004);return "%s takes in a deep breath...";}} sObjectMgr;
constexpr unsigned EFFECT_INDEX_0=0,EFFECT_INDEX_1=1,SPELL_EFFECT_SCHOOL_DAMAGE=2;
struct SpellEntry {unsigned Effect[3]={2,64,0},EffectRadiusIndex[3]={10,0,0},EffectTriggerSpell[3]={0,0,0},time=0;uint32 GetCastTime(Unit*){return time;}};
struct TargetPosition {unsigned mapId=249;float x=0,y=0;};
std::map<uint32,SpellEntry> spells;std::map<uint32,TargetPosition> positions;
struct SpellTemplate {template<class T>T* LookupEntry(uint32 id){auto i=spells.find(id);return i==spells.end()?nullptr:&i->second;}}sSpellTemplate;
struct SpellMgr {TargetPosition* GetSpellTargetPosition(uint32 id){auto i=positions.find(id);return i==positions.end()?nullptr:&i->second;}}sSpellMgr;
struct Radius {float Radius=30;};
struct RadiusStore {struct Row{float Radius=30;} row;Row* LookupEntry(unsigned id){return id==10?&row:nullptr;}}sSpellRadiusStore;
namespace ai {Unit* GetAirborneOnyxia(PlayerbotAI*);void ObserveOnyxiaBreathPacket(PlayerbotAI*,WorldPacket const&);}
'''
fixture=fixture.replace('struct Radius {float Radius=30;};\n','')
fixture+=(r/'ai/playerbot/OnyxiaAirPolicy.h').read_text().split('namespace ai')[1].split('inline bool OnyxiaWhelpRole')[0].replace('{','namespace ai {',1)+'}\n'
# The short policy prefix above contains just the actual airborne predicate.
fixture+=function((r/'ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp').read_text(),'Unit* ai::GetAirborneOnyxia(')
a=source.index('namespace\n{');b=source.index('bool ai::OnyxiaBreathActive(')
fixture+=source[a:b]
fixture+=function(chat,'void ChatHandler::BuildChatPacket(')
fixture+='\nPlayerbotAI* receiver=nullptr;\nvoid DoScriptText(int32 id,Unit* caster){WorldPacket p;ChatHandler::BuildChatPacket(p,CHAT_MSG_RAID_BOSS_EMOTE,sObjectMgr.GetMangosString(id,-1),LANG_UNIVERSAL,CHAT_TAG_NONE,caster->GetObjectGuid(),"Onyxia",ObjectGuid(),"",nullptr,0);ai::ObserveOnyxiaBreathPacket(receiver,p);}\n'
fixture+='constexpr int32 EMOTE_BREATH=-1249004;constexpr unsigned MOVE_RUN=0,MOVE_PATHFINDING=1,MOVE_FLY_MODE=2;constexpr float ONYXIA_NORMAL_SPEED=1.28571f;unsigned urand(unsigned,unsigned){return 99;}\n'
ids={'SPELL_BREATH_NE_TO_SW':18617,'SPELL_BREATH_EAST_TO_WEST':18576,'SPELL_BREATH_SE_TO_NW':18564,'SPELL_BREATH_SOUTH_TO_NORTH':18351,'SPELL_BREATH_SW_TO_NE':18596,'SPELL_BREATH_WEST_TO_EAST':18609,'SPELL_BREATH_NW_TO_SE':18584,'SPELL_BREATH_NORTH_TO_SOUTH':17086}
a=boss.index('struct sOnyxMove');b=boss.index('static float afSpawnLocations',a);table=boss[a:b]
for name,id in ids.items():table=table.replace(name,str(id))
fixture+=table+'struct CoreOnyxia {Unit* m_creature;int m_uiMovePoint=0;sOnyxMove* m_pPointData=nullptr;bool m_bDeepBreathIsCasting=false;uint32 m_uiDeepBreathTimer=0;\n'+function(boss,'sOnyxMove* GetMoveData()')+function(boss,'bool DoMovement()')+'};\n'
fixture+='int main(){\n'
for row in json.loads((r/'tools/fixtures/onyxia_breath_chains.json').read_text()):
 id,effect,nextid,castidx,radius,mapid,x,y=row
 fixture+=f'spells[{id}].Effect[0]={effect};spells[{id}].EffectTriggerSpell[1]={nextid};spells[{id}].time={dict([(1,0),(2,250),(6,5000)])[int(castidx)]};positions[{id}]={{{mapid},{x}f,{y}f}};\n'
fixture+=r'''
 PlayerbotAI ai;receiver=&ai;CoreOnyxia core{&ai.boss};
 for(unsigned i=0;i<8;++i){
  sPlayerbotAIConfig.stages.clear();
  nowMs+=20000;core.m_uiMovePoint=i;auto const& launch=aMoveData[i];ai.boss.x=launch.fX;ai.boss.y=launch.fY;
  assert(core.DoMovement());
  assert(sPlayerbotAIConfig.stages == std::vector<std::string>{"warning-accepted"});
  assert(core.m_bDeepBreathIsCasting&&core.m_uiDeepBreathTimer==5000);
  assert(ai.boss.cast==launch.uiSpellId);
  BreathObservation result;assert(GetBreathObservation(&ai,result));assert(result.spell==ai.boss.cast);
  assert(!result.circles.empty());std::cout<<"PASS root "<<result.spell<<" circles "<<result.circles.size()<<"\n";
 }
 // A real core launch away from its indexed endpoint must leave evidence.
 nowMs+=20000;sPlayerbotAIConfig.stages.clear();ai.boss.x=-25;ai.boss.y=-215;
 core.m_uiMovePoint=7;assert(core.DoMovement());
 assert(ai.boss.cast==17086);
 assert(sPlayerbotAIConfig.stages==std::vector<std::string>{"warning-off-launch-point"});
 // The same stage is rate limited, and debug-off never logs.
 assert(core.DoMovement());assert(sPlayerbotAIConfig.stages.size()==1);
 nowMs+=20000;ai.debug=false;assert(core.DoMovement());assert(sPlayerbotAIConfig.stages.size()==1);
 // An absent attacker still records the recognized warning.
 ai.debug=true;ai.context.attackers.clear();nowMs+=20000;sPlayerbotAIConfig.stages.clear();
 assert(core.DoMovement());assert(sPlayerbotAIConfig.stages==std::vector<std::string>{"warning-no-airborne-boss"});
 std::cout<<"PASS debug gate, rate limit, off-waypoint and missing-attacker diagnostics\n";
}
'''
p=w/'fixture.cpp';p.write_text(fixture)
subprocess.run([os.environ.get('CXX', 'g++'),'-std=c++17','-Wall','-Wextra',str(p),'-o',str(w/'fixture')],check=True)
subprocess.run([str(w/'fixture')],check=True)
