#include "../ai/playerbot/QuestLogPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsBannedQuest;
using ai::IsWarEffortTurnIn;
using ai::QuestAcceptSoloCapable;
using ai::QuestTriageShouldDrop;
using ai::ShouldRefuseQuestAtAccept;
using ai::kBoneChewToyGoEntry;
using ai::kBoneChewToyItemId;

int main()
{
    std::cout << "Starting TortoiseBots quest-log triage tests...\n";

    // (1) FAILED quests: dropped (donor OrganizeQuestLog drops FAILED).
    {
        CHECK(QuestTriageShouldDrop(10, 10, 0, 0, true, 0, 0));
        std::cout << "  [PASS] FAILED quests drop\n";
    }

    // (2) Over-level: donor IsQuestCapableDoing refuses botLevel + 3 <
    // questLevel. Scaling (0) never over-level.
    {
        CHECK(QuestTriageShouldDrop(14, 10, 0, 0, false, 0, 0));
        CHECK(!QuestTriageShouldDrop(13, 10, 0, 0, false, 0, 0));
        CHECK(!QuestTriageShouldDrop(0, 10, 0, 0, false, 0, 0));
        std::cout << "  [PASS] +3 over-level gate, scaling exempt\n";
    }

    // (3) Type != 0: elite/dungeon/raid/world-event picks are dropped;
    // solo (0) kept.
    {
        CHECK(QuestTriageShouldDrop(10, 10, 1, 0, false, 0, 0));
        CHECK(QuestTriageShouldDrop(10, 10, 81, 0, false, 0, 0));
        CHECK(QuestTriageShouldDrop(10, 10, 62, 0, false, 0, 0));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 0, 0));
        std::cout << "  [PASS] elite/dungeon/raid dropped, solo kept\n";
    }

    // (4) Suggested-group: donor refuses suggested >= 2.
    {
        CHECK(QuestTriageShouldDrop(10, 10, 0, 3, false, 0, 0));
        CHECK(QuestTriageShouldDrop(10, 10, 0, 2, false, 0, 0));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 1, false, 0, 0));
        std::cout << "  [PASS] group-suggested dropped, solo kept\n";
    }

    // (5) Zone mismatch: ZoneOrSort > 0 is a zone id - dropped when the
    // bot is elsewhere; sort ids (<= 0) and unknown bot zone (0) never
    // drop. The bot's own zone is kept.
    {
        CHECK(QuestTriageShouldDrop(10, 10, 0, 0, false, 14, 12));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 12, 12));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, -1, 12));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 0, 12));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 14, 0));
        std::cout << "  [PASS] zone mismatch dropped, sorts/unknown kept\n";
    }

    // (6) A normal doable quest survives every gate together.
    {
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 12, 12));
        std::cout << "  [PASS] doable quest kept\n";
    }

    // (7) War-effort item turn-ins (sort -365 with item objectives) are
    // refused: AQ copper/thick-leather turn-ins and the signet quests.
    // Sort -365 quests with no item objective (8792/8795 breadcrumb) stay
    // open, as do ordinary zone quests.
    {
        CHECK(IsWarEffortTurnIn(-365, true));
        CHECK(!IsWarEffortTurnIn(-365, false));
        CHECK(!IsWarEffortTurnIn(132, true));
        CHECK(!IsWarEffortTurnIn(-284, true));
        std::cout << "  [PASS] war-effort item turn-ins refused, breadcrumbs kept\n";
    }

    // (8) Banned quests: CLUCK! (3861) always, inactive templates (Method
    // disabled, e.g. [DEPRECATED] 40298) always; live quests untouched.
    {
        CHECK(IsBannedQuest(3861, false));
        CHECK(IsBannedQuest(3861, true));
        CHECK(IsBannedQuest(40298, true));
        CHECK(!IsBannedQuest(40298, false));
        CHECK(!IsBannedQuest(179, false));
        CHECK(!IsBannedQuest(8515, false));
        std::cout << "  [PASS] CLUCK!/inactive banned, live quests kept\n";
    }

    // (9) Combined accept/drop predicate: banned refuses for every bot,
    // war-effort only for upkeep bots (a level-60 owned bot can fill them).
    {
        CHECK(ShouldRefuseQuestAtAccept(3861, false, 132, false, false));
        CHECK(ShouldRefuseQuestAtAccept(3861, false, 132, false, true));
        CHECK(ShouldRefuseQuestAtAccept(8515, false, -365, true, true));
        CHECK(!ShouldRefuseQuestAtAccept(8515, false, -365, true, false));
        CHECK(!ShouldRefuseQuestAtAccept(8792, false, -365, false, true));
        CHECK(!ShouldRefuseQuestAtAccept(179, false, 132, true, true));
        std::cout << "  [PASS] combined refuse predicate matches accept and drop\n";
    }

    // (10) Bone Chew Toy ids stay pinned: the loot and bag rules key off
    // these, never a generic quest-class purge.
    {
        CHECK(kBoneChewToyItemId == 51751);
        CHECK(kBoneChewToyGoEntry == 1000380);
        std::cout << "  [PASS] Bone Chew Toy ids pinned\n";
    }

    // (11) Accept-time solo gate (RPG-A2): over-level (+3), non-solo type
    // and group-suggested quests are refused solo; scaling (0) never
    // over-level; grouped-and-able keeps them.
    {
        CHECK(!QuestAcceptSoloCapable(14, 10, 0, 0, false));
        CHECK(QuestAcceptSoloCapable(13, 10, 0, 0, false));
        CHECK(QuestAcceptSoloCapable(0, 10, 0, 0, false));
        CHECK(!QuestAcceptSoloCapable(10, 10, 1, 0, false));
        CHECK(!QuestAcceptSoloCapable(10, 10, 0, 2, false));
        CHECK(QuestAcceptSoloCapable(10, 10, 0, 0, false));
        CHECK(QuestAcceptSoloCapable(10, 10, 1, 3, true));
        CHECK(!QuestAcceptSoloCapable(14, 10, 1, 3, true));
        std::cout << "  [PASS] accept-time solo gate matches donor numbers\n";
    }

    std::cout << "All quest-log triage checks PASSED!\n";
    return 0;
}
