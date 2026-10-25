#pragma once
#include <chrono>
#include <cstdlib>
#include <array>
#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif
#include "Log.h"
namespace TBPassDiag {
using Clock=std::chrono::steady_clock;
inline uint64_t CpuUs(){
#ifdef _WIN32
 FILETIME c,e,k,u; if(GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u)){ ULARGE_INTEGER a,b; a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return (a.QuadPart+b.QuadPart)/10; }
#endif
 return 0;
}
struct Event{std::array<char,128> stage{};uint32_t guid=0;uint64_t wall=0,cpu=0;};
struct State{bool active=false;std::array<Event,24> top{};uint32_t slow=0;};
inline thread_local State state;
struct Scope{std::array<char,128> stage{};uint32_t guid;bool on;Clock::time_point start;uint64_t cpu=0;
 Scope(const char*s,uint32_t g=0,const char* detail=""):guid(g),on(state.active){if(on){std::snprintf(stage.data(),stage.size(),"%s:%s",s,detail);start=Clock::now();cpu=CpuUs();}}
 ~Scope(){if(!on)return;uint64_t w=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count();if(w<1000)return;++state.slow;Event e{stage,guid,w,CpuUs()-cpu};for(auto&v:state.top)if(e.wall>v.wall)std::swap(e,v);}
};
struct Pass{bool on=false;Clock::time_point start;uint64_t cpu=0;uint32_t bots;
 Pass(uint32_t n):bots(n){static bool enabled=[](){const char*v=std::getenv("TB_PASS_DIAG");return v&&v[0]=='1';}();static Clock::time_point first{};if(!enabled||!n)return;auto now=Clock::now();if(first==Clock::time_point{})first=now;if(now-first>std::chrono::minutes(3))return;state=State{};state.active=true;on=true;start=now;cpu=CpuUs();}
 ~Pass(){if(!on)return;uint64_t w=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count(),c=CpuUs()-cpu;state.active=false;static Clock::time_point last{};auto now=Clock::now();if(w<100000||(w<1000000&&now-last<std::chrono::seconds(10)))return;last=now;sLog.outString("TBPASSDIAG wallUs=%llu threadCpuUs=%llu bots=%u slowScopes=%u nested=1",(unsigned long long)w,(unsigned long long)c,bots,state.slow);for(auto const&e:state.top)if(e.wall)sLog.outString("TBPASSDIAG stage=%s guid=%u wallUs=%llu threadCpuUs=%llu",e.stage.data(),e.guid,(unsigned long long)e.wall,(unsigned long long)e.cpu);}
};
}
