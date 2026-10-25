#pragma once
#include "AttackAction.h"

namespace ai
{
    Unit* GetRagnarosSonTarget(PlayerbotAI* ai);

    class RagnarosSonAttackAction : public AttackAction
    {
    public:
        RagnarosSonAttackAction(PlayerbotAI* ai) : AttackAction(ai, "ragnaros son attack") {}
        Unit* GetTarget() override { return GetRagnarosSonTarget(ai); }
        bool isUseful() override;
    };
}
