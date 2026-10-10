#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

namespace ai
{
    // Writer for the "spread distance" knob (mirrors RangeAction): whisper
    // "spread distance <yards>" to set, "spread distance ?" to read, "spread
    // distance off" to reset to the role default. Non-positive means unset.
    class SpreadDistanceAction : public ChatCommandAction
    {
    public:
        SpreadDistanceAction(PlayerbotAI* ai) : ChatCommandAction(ai, "spread distance") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }
    };
}
