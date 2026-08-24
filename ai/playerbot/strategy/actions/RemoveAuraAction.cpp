
#include "playerbot/playerbot.h"
#include "RemoveAuraAction.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

bool RemoveAuraAction::Execute(Event& event)
{
    std::string spell = aura;
    if (spell.empty())
    {
        spell = event.GetParam();
    }

    return ai->RemoveAura(spell);
}
