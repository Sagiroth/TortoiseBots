#pragma once
#include "playerbot/PlayerbotAI.h"
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
}