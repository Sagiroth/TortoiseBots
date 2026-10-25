// Optional offline check: compile against the core's Detour library, then
// run with the extracted mmaps directory. Read-only; no server/DB required.
#include "../ai/playerbot/CthunSpreadPolicy.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourAlloc.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
using namespace ai;
int main(int argc,char** argv)
{
    assert(argc==2);
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
    dtQueryFilter filter;filter.setIncludeFlags(1);filter.setExcludeFlags(0); // NAV_GROUND
    auto nearest=[&](CthunPoint point,float* actual) {
        float input[3]={point.y,100.5f,point.x},extents[3]={.3f,12,.3f};
        dtPolyRef ref=0;
        assert(dtStatusSucceed(query.findNearestPoly(input,extents,&filter,&ref,actual)));
        assert(ref && std::hypot(actual[0]-point.y,actual[2]-point.x)<.2f && actual[1]>90 && actual[1]<110);
        return ref;
    };
    puts("group,slot,x,y,z,ground,complete_entry_path");
    for(unsigned group=0;group<8;++group) for(unsigned rank=0;rank<5;++rank)
    {
        CthunPoint destination=CthunChartSlot(group,rank);
        float actual[3];nearest(destination,actual);
        // Starting inside the documented southwest doorway, follow the same
        // fixed lane points as the production action. No direct-line claim.
        CthunPoint here=CthunPolar(80,157.5f*kCthunPi/180);
        unsigned steps=0;
        while(CthunDistance(here,destination)>.1f && steps++<20)
        {
            auto next=CthunChartStep(here,destination,group);
            float from[3],to[3];dtPolyRef a=nearest(here,from),b=nearest(next,to);
            dtPolyRef path[512];int count=0;
            assert(dtStatusSucceed(query.findPath(a,b,from,to,&filter,path,&count,512)));
            assert(count>0 && path[count-1]==b); // reject partial paths
            here=next;
        }
        assert(CthunDistance(here,destination)<.1f);
        printf("%u,%u,%.3f,%.3f,%.3f,PASS,PASS\n",group+1,rank,destination.x,destination.y,actual[1]);
    }
}
