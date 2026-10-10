#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class ZulgurubEnterDungeonTrigger : public EnterDungeonTrigger
    {
    public:
        ZulgurubEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter zul'gurub", "zul'gurub", 309) {}
    };

    class ZulgurubLeaveDungeonTrigger : public LeaveDungeonTrigger
    {
    public:
        ZulgurubLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave zul'gurub", "zul'gurub", 309) {}
    };

    class RuinsOfAhnqirajEnterDungeonTrigger : public EnterDungeonTrigger
    {
    public:
        RuinsOfAhnqirajEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter ruins of ahn'qiraj", "ruins of ahn'qiraj", 509) {}
    };

    class RuinsOfAhnqirajLeaveDungeonTrigger : public LeaveDungeonTrigger
    {
    public:
        RuinsOfAhnqirajLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave ruins of ahn'qiraj", "ruins of ahn'qiraj", 509) {}
    };

    class AhnqirajTempleEnterDungeonTrigger : public EnterDungeonTrigger
    {
    public:
        AhnqirajTempleEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter ahn'qiraj temple", "ahn'qiraj temple", 531) {}
    };

    class AhnqirajTempleLeaveDungeonTrigger : public LeaveDungeonTrigger
    {
    public:
        AhnqirajTempleLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave ahn'qiraj temple", "ahn'qiraj temple", 531) {}
    };
}
