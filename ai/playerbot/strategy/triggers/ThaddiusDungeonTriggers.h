#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    // Either add entering combat opens the fight (Thaddius himself is
    // not selectable until phase 2; the balconies are ~84yd apart, so a
    // single-entry trigger strands the far side). Custom trigger, not
    // StartBossFightTrigger, to watch both entries.
    class ThaddiusStartFightTrigger : public Trigger
    {
    public:
        ThaddiusStartFightTrigger(PlayerbotAI* ai, std::string name = "start thaddius fight", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        bool IsActive() override;
    };

    class ThaddiusEndFightTrigger : public EndBossFightTrigger
    {
    public:
        ThaddiusEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end thaddius fight", "thaddius", 15929) {}
    };

    // Pet phase: Stalagg (15929) or Feugen (15930) active — alive and
    // selectable (core fake-deaths adds with NOT_SELECTABLE at 0 HP).
    class ThaddiusPhasePetTrigger : public Trigger
    {
    public:
        ThaddiusPhasePetTrigger(PlayerbotAI* ai, std::string name = "thaddius phase pet", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        bool IsActive() override;
    };

    // Transition: both adds down, Thaddius (15928) still non-attackable.
    class ThaddiusPhaseTransitionTrigger : public Trigger
    {
    public:
        ThaddiusPhaseTransitionTrigger(PlayerbotAI* ai, std::string name = "thaddius phase transition", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        bool IsActive() override;
    };

    // Thaddius phase: adds down and Thaddius attackable.
    class ThaddiusPhaseThaddiusTrigger : public Trigger
    {
    public:
        ThaddiusPhaseThaddiusTrigger(PlayerbotAI* ai, std::string name = "thaddius phase thaddius", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        bool IsActive() override;
    };
}
