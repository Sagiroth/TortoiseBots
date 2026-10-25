"""Production healer selector: ready Sulfuron Heal before unreachable Heal."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(src, sig):
    start=src.index(sig);opening=src.index('{',start);depth,end=1,opening+1
    while depth:
        depth+=(src[end]=='{')-(src[end]=='}');end+=1
    return src[start:end]


class SulfuronInterruptTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_ready_heal_priority(self):
        root=pathlib.Path(os.environ.get('TBOTS_TEST_SOURCE',pathlib.Path(__file__).resolve().parents[1]))
        source=(root/'ai/playerbot/strategy/values/EnemyHealerTargetValue.cpp').read_text()
        code=r'''
#include <cassert>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <type_traits>
using ObjectGuid=unsigned;
constexpr int CURRENT_GENERIC_SPELL=0,CURRENT_CHANNELED_SPELL=1,SPELL_STATE_PREPARING=1;
constexpr int SPELL_PREVENTION_TYPE_SILENCE=1,SPELL_INTERRUPT_FLAG_DAMAGE=1;
enum SpellCastResult{SPELL_FAILED_NOT_READY,SPELL_CAST_OK};
enum class BotState{BOT_STATE_COMBAT};
struct SpellEntry{unsigned Id=19775,PreventionType=1,InterruptFlags=1;bool positive=true;};
struct Spell{SpellEntry* m_spellInfo;int state=1;int getState(){return state;}};
struct Unit {
 unsigned entry=11662,map=409,instance=1;bool alive=true,combat=true,los=true,interruptible=true,ready=true;
 float distance=3;Spell* cast=nullptr;
 unsigned GetEntry(){return entry;}unsigned GetMapId(){return map;}
 bool IsAlive(){return alive;}bool IsInCombat(){return combat;}
 bool IsInMap(Unit* u){return instance==u->instance&&map==u->map;}
 bool IsWithinLOSInMap(Unit* u){return u->los;}
 Spell* GetCurrentSpell(int kind){return kind==0?cast:nullptr;}
};
struct Player:Unit{};
template<class T>struct Value{T value;T Get(){return value;}};
struct Context {
 Value<std::list<ObjectGuid>> attackers;Value<Unit*> current;
 template<class T>Value<T>* GetValue(std::string){
  if constexpr(std::is_same_v<T,Unit*>)return &current;else return &attackers;
 }
};
struct PlayerbotAI {
 static bool IsHealSpell(SpellEntry* s){return s->positive;}
 bool enabled=true;Context context;std::map<ObjectGuid,Unit*> units;
 Context* GetAiObjectContext(){return &context;}Unit* GetUnit(ObjectGuid g){return units[g];}
 bool HasStrategy(std::string,BotState){return enabled;}float GetRange(std::string){return 30;}
 bool IsInterruptableSpellCasting(Unit* u,std::string,bool){return u->cast&&u->interruptible;}
 bool CanCastSpell(std::string,Unit* u,int,void*,bool,bool,bool,SpellCastResult* result){
  bool ready=u->ready&&u->distance<=5;*result=ready?SPELL_CAST_OK:SPELL_FAILED_NOT_READY;return ready;
 }
};
struct {float getDistance2d(Player*,Unit* u){return u->distance;}}sServerFacade;
bool IsPositiveSpell(SpellEntry* p){return p->positive;}
struct CcGuard{CcGuard(PlayerbotAI*){}bool IsHeldCcMark(Unit*){return false;}};
struct EnemyHealerTargetValue{PlayerbotAI* ai;Player* bot;std::string qualifier="kick";Unit* Calculate();};
'''
        code = (root/'ai/playerbot/strategy/values/CcMarkHoldPolicy.h').read_text().replace('#pragma once','') + '\nusing namespace ai;\n' + code
        code+=function(source,'Unit* EnemyHealerTargetValue::Calculate()')
        code+=r'''
int main(){
 Player bot;Unit boss,far,near;boss.entry=12098;far.distance=20;
 SpellEntry entry;Spell heal{&entry};far.cast=near.cast=&heal;
 PlayerbotAI ai;ai.units={{1,&boss},{2,&far},{3,&near}};ai.context.attackers.value={1,2,3};
 EnemyHealerTargetValue value{&ai,&bot};
 assert(value.Calculate()==&near); // old selector returns the out-of-kick-range priest
 ai.enabled=false;assert(value.Calculate()==&far);ai.enabled=true;
 bot.map=1;assert(value.Calculate()==&far);bot.map=409;
 boss.combat=false;assert(value.Calculate()==&far);boss.combat=true;
 boss.instance=2;assert(value.Calculate()==&far);boss.instance=1;
 near.ready=false;assert(value.Calculate()==&far);near.ready=true;
 near.los=false;assert(value.Calculate()==&far);near.los=true;
 ai.context.current.value=&near;assert(value.Calculate()==&far);ai.context.current.value=nullptr;
 near.interruptible=false;assert(value.Calculate()==&far);near.interruptible=true;
 entry.Id=19776;assert(value.Calculate()==&far);entry.Id=19775;
 heal.state=2;assert(value.Calculate()==&far);
 std::cout<<"PASS: ready in-range Heal prioritized; opt-out/scope/cooldown/LOS/current/cast controls preserve fallback\n";
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp=pathlib.Path(tmp)/'fixture.cpp';exe=pathlib.Path(tmp)/'fixture';cpp.write_text(code)
            subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__=='__main__':unittest.main()
