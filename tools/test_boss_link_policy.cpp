// Standalone check for the boss encounter's creature links (runtime/DungeonRunPolicy.h, BossLinkNamesBoss): the core
// keeps two kinds of link, creature_linking_template (its masterId is a creature entry) and creature_linking (its
// masterId is the master spawn's DB guid, flagged by a nonzero masterDBGuid). A GUID link read against the boss
// entry recognises nothing; a template link read against the spawn does the same.
//   g++ -std=c++17 -Wall -Wextra tools/test_boss_link_policy.cpp -o /tmp/test_boss_link_policy && /tmp/test_boss_link_policy
#include "../runtime/DungeonRunPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots::DungeonRun;

static BossLink EntryLink(std::uint32_t masterEntry)
{
    BossLink link;
    link.masterId = masterEntry;
    return link;
}

static BossLink GuidLink(std::uint32_t masterDbGuid)
{
    BossLink link;
    link.masterId = masterDbGuid; // the core puts the same guid in both fields
    link.masterDBGuid = masterDbGuid;
    return link;
}

int main()
{
    BossEncounter boss;
    boss.entry = 14507;   // a boss's creature entry
    boss.guid = 0x0000000100000002ull;
    boss.dbGuid = 4242;   // ... and the spawn row a creature_linking row names

    // A template link names the master entry: it is the boss only for the boss's own entry.
    CHECK(BossLinkNamesBoss(boss, EntryLink(boss.entry)));
    CHECK(!BossLinkNamesBoss(boss, EntryLink(boss.entry + 1)));

    // A GUID link names the boss spawn, not his entry: the two numbers are independent.
    CHECK(boss.dbGuid != boss.entry);
    CHECK(BossLinkNamesBoss(boss, GuidLink(boss.dbGuid)));
    CHECK(!BossLinkNamesBoss(boss, GuidLink(boss.entry))); // the entry read as a spawn matches no row
    CHECK(!BossLinkNamesBoss(boss, GuidLink(boss.dbGuid + 1)));

    // Nothing to compare against: a boss spawn the glue did not read leaves a GUID link unmatched.
    BossEncounter unread;
    unread.entry = boss.entry;
    CHECK(!BossLinkNamesBoss(unread, GuidLink(boss.dbGuid)));
    CHECK(BossLinkNamesBoss(unread, EntryLink(boss.entry)));

    // The composite membership still reads the link's verdict, and nothing else about the add.
    BossEncounterScan add;
    add.guid = 0x0000000100000099ull;
    add.entry = boss.entry; // its entry alone never makes it the boss's fight
    add.linked = BossLinkNamesBoss(boss, GuidLink(boss.dbGuid));
    CHECK(BossEncounterUnit(boss, add));
    add.linked = BossLinkNamesBoss(boss, GuidLink(boss.entry));
    CHECK(!BossEncounterUnit(boss, add));

    return 0;
}
