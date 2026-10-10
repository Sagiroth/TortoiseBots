#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/GolemaggPolicy.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"
#include "UseItemAction.h"

namespace ai
{
    class MoltenCoreEnableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MoltenCoreEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable molten core strategy", "+molten core") {}
    };

    class MoltenCoreDisableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MoltenCoreDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable molten core strategy", "-molten core") {}
    };

    class MagmadarEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MagmadarEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable magmadar fight strategy", "+magmadar") {}
    };

    class MagmadarDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MagmadarDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable magmadar fight strategy", "-magmadar") {}
    };

    class GeddonEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        GeddonEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable geddon fight strategy", "+geddon") {}
    };

    class GeddonDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        GeddonDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable geddon fight strategy", "-geddon") {}
    };

    class MagmadarMoveAwayFromLavaBombAction : public MoveAwayFromHazard
    {
    public:
        MagmadarMoveAwayFromLavaBombAction(PlayerbotAI* ai) : MoveAwayFromHazard(ai, "move away from magmadar lava bomb") {}
    };

    class MagmadarMoveAwayAction : public MoveAwayFromCreature
    {
    public:
        MagmadarMoveAwayAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from magmadar", 11982, 31.0f) {}
    };

    class GeddonMoveAwayAction : public MoveAwayFromCreature
    {
    public:
        // Donor INFERNO_DISTANCE: 20y clear while Inferno burns.
        GeddonMoveAwayAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from geddon", 12056, 20.0f) {}
    };

    class MoveToMCRuneAction : public MoveToAction
    {
    public:
        MoveToMCRuneAction(PlayerbotAI* ai) : MoveToAction(ai, "move to mc rune") { qualifier = "entry filter::{gos in sight,mc runes}"; }
    };

    class DouseMCRuneActionAqual : public UseItemIdAction
    {
    public:
        // Aqual Quintessence (17333): Majordomo cache / Hydraxian quest item.
        DouseMCRuneActionAqual(PlayerbotAI* ai) : UseItemIdAction(ai, "douse mc rune aqual") { qualifier = "{17333,entry filter::{gos close,mc runes}}"; }
    };

    class DouseMCRuneActionEternal : public UseItemIdAction
    {
    public:
        // Eternal Quintessence (22754): exalted-Hydraxian upgrade, same use.
        DouseMCRuneActionEternal(PlayerbotAI* ai) : UseItemIdAction(ai, "douse mc rune eternal") { qualifier = "{22754,entry filter::{gos close,mc runes}}"; }
    };

    class GarrEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        GarrEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable garr fight strategy", "+garr") {}
    };

    class GarrDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        GarrDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable garr fight strategy", "-garr") {}
    };

    class ShazzrahEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        ShazzrahEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable shazzrah fight strategy", "+shazzrah") {}
    };

    class ShazzrahDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        ShazzrahDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable shazzrah fight strategy", "-shazzrah") {}
    };

    class ShazzrahMoveAwayAction : public MoveAwayFromCreature
    {
    public:
        // Donor ARCANE_EXPLOSION_DISTANCE: ranged holds 26y.
        ShazzrahMoveAwayAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from shazzrah", 12264, 26.0f) {}
    };

    class GolemaggEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        GolemaggEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable golemagg fight strategy", "+golemagg") {}
    };

    class GolemaggDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        GolemaggDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable golemagg fight strategy", "-golemagg") {}
    };

    class GolemaggBackOffAction : public MoveAwayFromCreature
    {
    public:
        // Donor 12y clear until the splash stack expires.
        GolemaggBackOffAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "back off golemagg", kGolemaggEntry, kMagmaSplashBackOffDistance) {}
    };

    // Healer midpoint move (donor camp midpoint 821.2,-1007).
    class GolemaggHealerPositionAction : public MoveToAction
    {
    public:
        GolemaggHealerPositionAction(PlayerbotAI* ai) : MoveToAction(ai, "golemagg healer position") {}
        bool Execute(Event& event) override;
    };

    // Main-tank hold: keep Golemagg at his camp while the Trust buff lives
    // on the ragers (donor 795.7,-994.9). Assist holds a rager at its camp
    // (donor 846.6,-1019.1) via the same move shape.
    class GolemaggTankHoldAction : public MoveToAction
    {
    public:
        GolemaggTankHoldAction(PlayerbotAI* ai) : MoveToAction(ai, "golemagg tank hold") {}
        bool Execute(Event& event) override;
    };
}
