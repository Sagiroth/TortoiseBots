#pragma once
#include "ScriptObjects.h"
namespace TortoiseBots
{
    class ViscidusDiagnostics final : public UnitScript
    {
    public:
        ViscidusDiagnostics() : UnitScript("tortoisebots_viscidus_diagnostics",
            {UNITHOOK_ON_UNIT_UPDATE, UNITHOOK_ON_UNIT_DEATH, UNITHOOK_ON_UNIT_EXIT_COMBAT}) {}
        void OnUnitUpdate(Unit* unit, uint32 elapsed) override;
        void OnUnitDeath(Unit* unit, Unit* killer) override;
        void OnUnitExitCombat(Unit* unit) override;
    };
}
