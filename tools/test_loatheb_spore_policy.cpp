#include "../ai/playerbot/LoathebSporePolicy.h"
#include "../ai/playerbot/GarrAoePolicy.h"
#include <cassert>

int main()
{
    using namespace ai;
    assert(LoathebSporeProtected(true, true, true, 16286, false));
    assert(!LoathebSporeProtected(false, true, true, 16286, false));
    assert(!LoathebSporeProtected(true, false, true, 16286, false));
    assert(!LoathebSporeProtected(true, true, false, 16286, false));
    assert(!LoathebSporeProtected(true, true, true, 16011, false));
    assert(!LoathebSporeProtected(true, true, true, 16286, true));
    assert(GarrIncidentalDamageAction("multi-shot", false, false, false));
    assert(GarrIncidentalDamageAction("blizzard", true, false, false));
    assert(!GarrIncidentalDamageAction("flash heal", false, true, false));
    assert(!GarrIncidentalDamageAction("polymorph", false, false, true));
    assert(!GarrIncidentalDamageAction("attack", false, false, false));
}
