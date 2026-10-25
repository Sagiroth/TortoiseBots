"""Compile the production Sons selector and action usefulness with world stubs."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(src, sig):
    start=src.index(sig); opening=src.index('{',start); depth,end=1,opening+1
    while depth:
        depth+=(src[end]=='{')-(src[end]=='}');end+=1
    return src[start:end]


class RagnarosSonsTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_submerged_owner_and_pickup(self):
        root=pathlib.Path(__file__).resolve().parents[1]
        src=(root/'ai/playerbot/strategy/actions/RagnarosActions.cpp').read_text()
        code=r'''
#include <cassert>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <type_traits>
using ObjectGuid=unsigned;
constexpr unsigned UNIT_STAT_ISOLATED=1,UNIT_FIELD_FLAGS=0,UNIT_FLAG_SPAWNING=1,UNIT_FLAG_NOT_SELECTABLE=2;
struct Unit {
 virtual ~Unit()=default;
 unsigned entry=0,map=409,instance=1,guid=0,flags=0;
 bool alive=true,combat=true,isolated=false,cc=false,valid=true;
 float distance=5;
 Unit* victim=nullptr;
 unsigned GetEntry(){return entry;} unsigned GetMapId(){return map;}
 ObjectGuid GetObjectGuid(){return guid;}
 bool IsAlive(){return alive;} bool IsInCombat(){return combat;}
 bool IsInMap(Unit* u){return map==u->map&&instance==u->instance;}
 bool HasUnitState(unsigned){return isolated;}
 bool HasFlag(unsigned,unsigned f){return (flags&f)!=0;}
 Unit* GetVictim(){return victim;}
};
struct TemporarySummon:Unit { ObjectGuid owner=1;ObjectGuid GetSummonerGuid(){return owner;} };
struct Player:Unit {
 bool world=true,teleport=false,tank=false,heal=false;
 bool IsInWorld(){return world;}bool IsBeingTeleported(){return teleport;}
};
struct Position {bool set=false;bool isSet(){return set;}};
using PositionMap=std::map<std::string,Position>;
struct AiObjectContext {
 ObjectGuid order=0;Unit* rti=nullptr;Unit* current=nullptr;
 PositionMap positions;std::list<ObjectGuid> candidates;
 template<class T>T Value(std::string n){
  if constexpr(std::is_same_v<T,ObjectGuid>)return order;
  else if constexpr(std::is_same_v<T,Unit*>)return n=="rti target"?rti:current;
  else if constexpr(std::is_same_v<T,std::list<ObjectGuid>>)return candidates;
  else return (positions);
 }
};
#define AI_VALUE(type,name) context->Value<type>(name)
enum class BotState{BOT_STATE_COMBAT};
struct PlayerbotAI {
 Player* bot;AiObjectContext context;std::map<ObjectGuid,Unit*> units;std::set<std::string> strategies;
 Player* GetBot(){return bot;}AiObjectContext* GetAiObjectContext(){return &context;}
 Unit* GetUnit(ObjectGuid g){auto i=units.find(g);return i==units.end()?nullptr:i->second;}
 bool HasStrategy(std::string s,BotState){return strategies.count(s);}
 bool IsTank(Player* p){return p->tank;}bool IsHeal(Player* p){return p->heal;}
};
struct PossibleAttackTargetsValue {
 static bool IsValid(Unit* u,Player*,float,bool,bool){return u&&u->valid;}
 static bool HasBreakableCC(Unit* u,Player*){return u->cc;}
 static bool HasUnBreakableCC(Unit*,Player*){return false;}
};
struct {float sightDistance=60;}sPlayerbotAIConfig;
struct {float getDistance2d(Player*,Unit* u){return u->distance;}}sServerFacade;
namespace ai {
 Unit* GetRagnarosSonTarget(PlayerbotAI*);
 struct RagnarosSonAttackAction {
  PlayerbotAI* ai;AiObjectContext* context;
  Unit* GetTarget(){return GetRagnarosSonTarget(ai);}bool isUseful();
 };
}
using namespace ai;
'''
        code+=function(src,'Unit* ai::GetRagnarosSonTarget(')
        code+=function(src,'bool RagnarosSonAttackAction::isUseful(')
        code+=r'''
int main(){
 Player bot,tank,healer;bot.tank=tank.tank=true;healer.heal=true;
 Unit boss;boss.entry=11502;boss.guid=1;boss.flags=3;
 TemporarySummon held,loose;held.entry=loose.entry=12143;held.guid=2;loose.guid=3;
 held.victim=&tank;loose.victim=&healer;held.distance=1;loose.distance=25;
 PlayerbotAI ai{&bot};ai.units={{1,&boss},{2,&held},{3,&loose}};
 ai.context.candidates={2,3}; // boss intentionally absent: submerged/invisible/friendly
 assert(!GetRagnarosSonTarget(&ai));
 ai.strategies.insert("ragnaros sons");ai.context.current=&held;
 assert(GetRagnarosSonTarget(&ai)==&loose);
 RagnarosSonAttackAction action{&ai,&ai.context};assert(action.isUseful());
 ai.context.current=&loose;assert(!action.isUseful());
 loose.isolated=true;assert(GetRagnarosSonTarget(&ai)==&held);loose.isolated=false;
 loose.cc=true;assert(GetRagnarosSonTarget(&ai)==&held);loose.cc=false;
 boss.flags=0;assert(!GetRagnarosSonTarget(&ai));boss.flags=3;
 boss.combat=false;assert(!GetRagnarosSonTarget(&ai));boss.combat=true;
 boss.entry=1;assert(!GetRagnarosSonTarget(&ai));boss.entry=11502;
 boss.instance=2;assert(!GetRagnarosSonTarget(&ai));boss.instance=1;
 ai.context.order=2;assert(!GetRagnarosSonTarget(&ai));ai.context.order=0;
 ai.context.rti=&held;assert(!GetRagnarosSonTarget(&ai));ai.context.rti=nullptr;
 for(auto hold:{"stay","follow","wait for attack","grind"}){
  ai.strategies.insert(hold);assert(!GetRagnarosSonTarget(&ai));ai.strategies.erase(hold);
 }
 ai.context.positions["pull hold"].set=true;assert(!GetRagnarosSonTarget(&ai));
 ai.context.positions.clear();bot.tank=false;bot.heal=true;assert(!GetRagnarosSonTarget(&ai));
 bot.heal=false;ai.context.current=nullptr;assert(GetRagnarosSonTarget(&ai)==&held);
 loose.owner=99;held.owner=99;assert(!GetRagnarosSonTarget(&ai));
 held.owner=1;bot.map=533;assert(!GetRagnarosSonTarget(&ai));
 std::cout<<"PASS: actual hidden-owner submerge path, loose pickup, CC/orders/holds/role/map/owner and stale action controls\n";
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp=pathlib.Path(tmp)/'fixture.cpp';exe=pathlib.Path(tmp)/'fixture';cpp.write_text(code)
            subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__=='__main__':unittest.main()
