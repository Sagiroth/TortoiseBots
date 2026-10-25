// Offline only. Link core Detour/G3D and vmap sources (NO_CORE_FUNCS,
// MMAP_GENERATOR, USE_STANDARD_MALLOC; preinclude memory and mutex).
// Arguments: extracted mmaps directory, extracted vmaps directory.
// No server, DB writes, or live character actions.
#include "../ai/playerbot/CthunSpreadPolicy.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourAlloc.h"
#include "VMapManager2.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
IPerfMonitor* gPerfMonitorInterface=nullptr;
using namespace ai;
int main(int argc,char**argv)
{
    assert(argc==3);
    std::filesystem::path directory(argv[1]);
    FILE* file=fopen((directory/"531.mmap").c_str(),"rb");assert(file);
    dtNavMeshParams params;assert(fread(&params,sizeof params,1,file)==1);fclose(file);
    dtNavMesh mesh;assert(dtStatusSucceed(mesh.init(&params)));
    for(auto const& entry:std::filesystem::directory_iterator(directory))
    {
        if(entry.path().extension()!=".mmtile" || entry.path().filename().string().substr(0,3)!="531") continue;
        file=fopen(entry.path().c_str(),"rb");assert(file);
        uint32_t header[5];assert(fread(header,sizeof header,1,file)==1);
        assert(header[0]==0x4d4d4150 && header[1]==DT_NAVMESH_VERSION && header[2]==6);
        auto* bytes=static_cast<unsigned char*>(dtAlloc(header[3],DT_ALLOC_PERM));
        assert(fread(bytes,header[3],1,file)==1);fclose(file);
        assert(dtStatusSucceed(mesh.addTile(bytes,header[3],DT_TILE_FREE_DATA,0,nullptr)));
    }
    dtNavMeshQuery query;assert(dtStatusSucceed(query.init(&mesh,4096)));
    dtQueryFilter filter;filter.setIncludeFlags(1);filter.setExcludeFlags(0);
    VMAP::VMapManager2 vmap;
    std::string vdir=std::string(argv[2])+"/";
    for(int x=27;x<=30;++x)for(int y=46;y<=49;++y)
        assert(vmap.loadMap(vdir.c_str(),531,x,y)==VMAP::VMAP_LOAD_RESULT_OK);
    auto nearest=[&](CthunPoint point,float z,float* actual) {
        float input[3]={point.y,z,point.x},extents[3]={.3f,2,.3f};dtPolyRef ref=0;
        assert(dtStatusSucceed(query.findNearestPoly(input,extents,&filter,&ref,actual)));
        assert(ref && std::hypot(actual[0]-point.y,actual[2]-point.x)<.2f);
        assert(CthunApproach(531,actual[2],actual[0],actual[1]));return ref;
    };
    auto los=[&](CthunPoint p,float z,float own,float boss) {
        return vmap.isInLineOfSight(531,p.x,p.y,z+own,kCthunCenter.x,kCthunCenter.y,100.4f+boss);
    };
    unsigned samples=0;
    // A three-yard staging footprint, sampled on a one-yard grid. Check a
    // conservative range of player/boss collision heights, not feet-to-feet.
    for(int dx=-3;dx<=3;++dx)for(int dy=-3;dy<=3;++dy)
    {
        if(dx*dx+dy*dy>9)continue;
        CthunPoint p{kCthunWaiting.x+dx,kCthunWaiting.y+dy};float ground[3];
        nearest(p,kCthunWaitingZ,ground);
        for(float own:{1.f,2.f,3.f,4.f})for(float boss:{2.f,5.f,10.f,15.f})
            assert(!los(p,ground[1],own,boss));
        ++samples;
    }
    assert(los(kCthunEntry,100.579f,2,2)); // loaded collision, positive control
    assert(!los({-8679.15f,1975.54f},108.90f,2,2)); // old point also static-LOS blocked
    printf("WAIT %.3f %.3f %.3f: %u ground/LOS-blocked samples PASS\n",
        kCthunWaiting.x,kCthunWaiting.y,kCthunWaitingZ,samples);
    struct Start {CthunPoint p;float z;};
    Start starts[]={{kCthunWaiting,kCthunWaitingZ},{{-8679.15f,1975.54f},108.9f},
        {{-8641.8669f,1979.7118f},101.0793f}, {CthunPolar(80,157.5f*kCthunPi/180),108.577f}};
    unsigned routes=0;
    for(auto start:starts)for(unsigned sector=0;sector<8;++sector)for(unsigned rank=0;rank<5;++rank)
    {
        auto here=start.p;float z=start.z;auto destination=CthunChartSlot(sector,rank);
        unsigned steps=0;float length=0;
        while(CthunDistance(here,destination)>.1f && steps++<20)
        {
            auto next=CthunEntryStep(here,z,destination,sector);
            float a[3],b[3];auto ar=nearest(here,z,a);
            auto br=nearest(next,100.5f,b);
            dtPolyRef path[512];int count=0;
            auto status=query.findPath(ar,br,a,b,&filter,path,&count,512);
            assert(dtStatusSucceed(status) && !(status&DT_PARTIAL_RESULT) && count>0 && path[count-1]==br);
            float vertices[1536];unsigned char flags[512];dtPolyRef refs[512];int n=0;
            assert(dtStatusSucceed(query.findStraightPath(a,b,path,count,vertices,flags,refs,&n,512)));
            assert(n>0);
            for(int i=0;i<n;++i)
            {
                assert(CthunApproach(531,vertices[i*3+2],vertices[i*3],vertices[i*3+1]));
                if(i)length+=std::hypot(vertices[i*3+2]-vertices[(i-1)*3+2],vertices[i*3]-vertices[(i-1)*3]);
            }
            here=next;z=b[1];
        }
        assert(CthunDistance(here,destination)<.1f);
        printf("ROUTE %.2f %.2f group=%u slot=%u legs=%u length=%.1f PASS\n",
            start.p.x,start.p.y,sector+1,rank,steps,length);
        ++routes;
    }
    printf("%u complete ground routes; all checked waypoints within scope PASS\n",routes);
}
