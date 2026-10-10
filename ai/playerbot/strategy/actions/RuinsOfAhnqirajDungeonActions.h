#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"

namespace ai
{
class OssirianEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    OssirianEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable ossirian fight strategy", "+ossirian") {}
};

class OssirianDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    OssirianDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable ossirian fight strategy", "-ossirian") {}
};

// Crystal runner (mod-playerbots parity: Aq20UseCrystalAction). Moves to
// the nearest Ossirian Crystal (GO 180619), waits in range until the
// buff is up or imminent, then uses it iff Ossirian is within 25yd and
// the crystal is not already firing. Single queued CMSG_GAMEOBJ_USE
// packet like the suppression-device precedent (1.12 has no separate
// report-use opcode).
class UseOssirianCrystalAction : public MovementAction
{
public:
    UseOssirianCrystalAction(PlayerbotAI* ai) : MovementAction(ai, "use ossirian crystal") {}
    bool Execute(Event& event) override;
    bool isPossible() override;
};
}
