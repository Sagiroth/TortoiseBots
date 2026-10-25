// Offline only. Link core Detour/G3D and vmap sources (NO_CORE_FUNCS,
// MMAP_GENERATOR, USE_STANDARD_MALLOC; preinclude memory and mutex).
// Arguments: extracted mmaps directory, extracted vmaps directory.
// Optional fourth argument scans waiting candidates outside both spawn radii.
// No server, DB writes, or live character actions.
#include <cmath>
#include "../ai/playerbot/TwinsOpeningPolicy.h"
#include <vector>
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourAlloc.h"
#include "VMapManager2.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
IPerfMonitor* gPerfMonitorInterface=nullptr;

int main(int argc,char**argv)
{
    assert(argc>=3);
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
    using namespace ai;
    assert(TwinsSeparated(kTwinsWest,kTwinsEast,3));
    if(argc>3) {
        for(int side=0;side<2;++side) for(float x=(side ? -8900.f:-9060.f);x<(side ? -8820.f:-8990.f);x+=5)
        for(float y=1200;y<=1260;y+=5) {
            TwinsPoint p{x,y,-104.2f};if(!TwinsRoom(531,p) || TwinsDistance(p,kTwinsWest)<56 || TwinsDistance(p,kTwinsEast)<56) continue;
            float a[3]={y,-104.2f,x},ext[3]={.5f,18,.5f},b[3];dtPolyRef ref=0;
            query.findNearestPoly(a,ext,&filter,&ref,b);if(!ref || std::hypot(b[0]-y,b[2]-x)>.3f || b[1]>-85 || b[1]<-120) continue;
            TwinsPoint home=side ? kTwinsEast:kTwinsWest;
            float goal[3]={home.y,home.z,home.x},g[3];dtPolyRef gr=0;query.findNearestPoly(goal,ext,&filter,&gr,g);
            dtPolyRef path[512];int n=0;auto status=query.findPath(ref,gr,b,g,&filter,path,&n,512);
            if(!dtStatusSucceed(status)||!n||path[n-1]!=gr)continue;
            printf("side=%d x=%.2f y=%.2f z=%.3f distance=%.1f LOS=%d\n",side,x,y,b[1],TwinsDistance(p,home),int(vmap.isInLineOfSight(531,x,y,b[1]+2,home.x,home.y,g[1]+2)));
        }
        return 0;
    }

    struct Route {TwinsPoint start,end,caster;bool clearance;bool escape=false;bool requireLos=true;};
    std::vector<Route> routes;
    for(int side=0;side<2;++side) {
        TwinsPoint home=side ? kTwinsEast:kTwinsWest;
        TwinsPoint other=side ? kTwinsWest:kTwinsEast;
        TwinsPoint stage=side ? TwinsPoint{-8900,1260,-112.221f}:TwinsPoint{-9035,1245,-112.221f};
        assert(TwinsDistance(stage,kTwinsWest)>55 && TwinsDistance(stage,kTwinsEast)>55);
        routes.push_back({TwinsOutward(home,side,24),TwinsOutward(home,side,15),home,true});
        routes.push_back({TwinsOutward(home,side,15),TwinsOutward(home,side,5),other,true});
        routes.push_back({TwinsOutward(home,side,5),TwinsOutward(home,side,24),home,true,true});
        // Collapsed opening: acquire each boss from range, then lead outward.
        TwinsPoint collapsedPhysical{-8950.6f,1192.4f,-104.17f},collapsedCaster{-8942.6f,1192.4f,-104.17f};
        TwinsPoint boss=side ? collapsedCaster:collapsedPhysical;
        float dx=home.x-boss.x,dy=home.y-boss.y,length=std::hypot(dx,dy);
        TwinsPoint acquisition{boss.x+dx/length*24,boss.y+dy/length*24,boss.z};
        routes.push_back({stage,acquisition,collapsedCaster,true});
        routes.push_back({acquisition,TwinsOutward(home,side,side ? 20:5),collapsedCaster,true,false,false});
        routes.push_back({acquisition,boss,collapsedCaster,false});
        for(float offset:{5.f,15.f,20.f,24.f,35.f}) {
            auto goal=TwinsOutward(home,side,offset);
            // Opening parks the spare at 24 first; preparation goes 24 -> 15,
            // checked above. Do not claim a direct stage -> 15 route is safe.
            if(offset!=15) routes.push_back({stage,goal,offset==5 ? other:home,true,false,false});
            routes.push_back({goal,home,home,false}); // spell/melee LOS
        }
    }
    for(auto const& route:routes) {
        float x=route.start.x,y=route.start.y,z=route.start.z;
        float bx=route.end.x,by=route.end.y,bz=route.end.z;
        float in[3]={y,z,x},goal[3]={by,bz,bx},ext[3]={.5f,30,.5f},a[3]{},b[3]{};
        dtPolyRef ar=0,br=0;query.findNearestPoly(in,ext,&filter,&ar,a);query.findNearestPoly(goal,ext,&filter,&br,b);
        bool ground=ar && std::hypot(a[0]-y,a[2]-x)<.3f;
        printf("%.2f %.2f %.3f ground=%d",x,y,a[1],int(ground));
        assert(ground && br);
        bool los=vmap.isInLineOfSight(531,x,y,a[1]+2,bx,by,b[1]+2);
        dtPolyRef path[512];int count=0;auto status=query.findPath(ar,br,a,b,&filter,path,&count,512);
        bool complete=dtStatusSucceed(status)&&count && path[count-1]==br;
        float verts[1536];int n=0;unsigned char flags[512];dtPolyRef refs[512];
        query.findStraightPath(a,b,path,count,verts,flags,refs,&n,512);
        float length=0;for(int i=1;i<n;++i)length+=std::hypot(verts[3*i]-verts[3*i-3],verts[3*i+2]-verts[3*i-1]);
        printf(" los=%d complete=%d length=%.2f end=%.2f:%.2f:%.2f\n",int(los),int(complete),length,b[2],b[0],b[1]);
        assert((los || !route.requireLos) && complete && n>0);
        if(route.clearance) for(int i=1;i<n;++i) {
            TwinsPoint start{verts[3*i-1],verts[3*i-3],verts[3*i-2]};
            TwinsPoint end{verts[3*i+2],verts[3*i],verts[3*i+1]};
            int steps=std::max(1,int(std::ceil(TwinsDistance(start,end))));
            TwinsPoint previous=start;
            for(int j=0;j<=steps;++j) {
                float f=float(j)/steps;
                TwinsPoint p{start.x+f*(end.x-start.x),start.y+f*(end.y-start.y),start.z+f*(end.z-start.z)};
                if(route.escape) assert(TwinsSafeStep(previous,p,route.caster,14.5f));
                else assert(TwinsDistance(p,route.caster)>14.5f);
                previous=p;
            }
        }
    }
}
