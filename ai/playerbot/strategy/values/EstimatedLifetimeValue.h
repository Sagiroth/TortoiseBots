// Forward-ported from mod-playerbots EstimatedLifetimeValue.h
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ESTIMATEDLIFETIMEVALUE_H
#define PLAYERBOTS_ESTIMATEDLIFETIMEVALUE_H

#include "NamedObjectContext.h"
#include "PossibleTargetsValue.h"
#include "TargetValue.h"
#include "Value.h"

class PlayerbotAI;
class Unit;

// [target health] / [expected group single target dps] = [expected lifetime]
class EstimatedLifetimeValue : public FloatCalculatedValue, public Qualified
{
public:
    EstimatedLifetimeValue(PlayerbotAI* botAI) : FloatCalculatedValue(botAI, "estimated lifetime") {}

public:
    float Calculate() override;
};

class EstimatedGroupDpsValue : public FloatCalculatedValue
{
public:
    // checkInterval is seconds (halved by CalculatedValue::Refresh): 40 s ≈
    // 20 s effective. (Was 20*1000, donor milliseconds copied verbatim —
    // review PR #596 — which froze DPS for hours.)
    EstimatedGroupDpsValue(PlayerbotAI* botAI) : FloatCalculatedValue(botAI, "estimated group dps", 40) {}

public:
    float Calculate() override;

protected:
    float GetBasicDps(uint32 level);
    float GetBasicGs(uint32 level);
};

#endif
