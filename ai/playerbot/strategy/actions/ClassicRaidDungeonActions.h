#pragma once
#include "playerbot/strategy/actions/DungeonActions.h"
#include "playerbot/strategy/actions/ChangeStrategyAction.h"

namespace ai
{
class ZulgurubEnableDungeonStrategyAction : public ChangeAllStrategyAction
{
public:
    ZulgurubEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable zul'gurub strategy", "+zul'gurub") {}
};

class ZulgurubDisableDungeonStrategyAction : public ChangeAllStrategyAction
{
public:
    ZulgurubDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable zul'gurub strategy", "-zul'gurub") {}
};

class RuinsOfAhnqirajEnableDungeonStrategyAction : public ChangeAllStrategyAction
{
public:
    RuinsOfAhnqirajEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable ruins of ahn'qiraj strategy", "+ruins of ahn'qiraj") {}
};

class RuinsOfAhnqirajDisableDungeonStrategyAction : public ChangeAllStrategyAction
{
public:
    RuinsOfAhnqirajDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable ruins of ahn'qiraj strategy", "-ruins of ahn'qiraj") {}
};

class AhnqirajTempleEnableDungeonStrategyAction : public ChangeAllStrategyAction
{
public:
    AhnqirajTempleEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable ahn'qiraj temple strategy", "+ahn'qiraj temple") {}
};

class AhnqirajTempleDisableDungeonStrategyAction : public ChangeAllStrategyAction
{
public:
    AhnqirajTempleDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable ahn'qiraj temple strategy", "-ahn'qiraj temple") {}
};
}
