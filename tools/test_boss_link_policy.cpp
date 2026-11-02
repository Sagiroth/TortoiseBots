// Standalone check for the boss encounter's creature links (runtime/DungeonRunPolicy.h, BossLinkNamesBoss): the core
// keeps two kinds of link, creature_linking_template (its masterId is a creature entry and the row carries a map) and
// creature_linking (its masterId is the master spawn's DB guid and its map is the core's INVALID_MAP_ID). A template
// row may carry a masterDBGuid too (a unique master, the FOLLOW flag), so that field never marks the kind on its own.
// A GUID link read against the boss entry recognises nothing; a template link read against the spawn does the same.
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

static BossLink EntryLink(std::uint32_t masterEntry, std::uint32_t mapId = 309)
{
    BossLink link;
    link.masterId = masterEntry;
    link.mapId = mapId;
    return link;
}

// A template row whose master is unique on its map: the core fills masterDBGuid from the creature table, the row is
// an entry link all the same.
static BossLink FollowEntryLink(std::uint32_t masterEntry, std::uint32_t masterDbGuid)
{
    BossLink link = EntryLink(masterEntry);
    link.masterDBGuid = masterDbGuid;
    return link;
}

static BossLink GuidLink(std::uint32_t masterDbGuid)
{
    BossLink link;
    link.masterId = masterDbGuid; // the core puts the same guid in both fields
    link.masterDBGuid = masterDbGuid;
    link.mapId = kInvalidMapId;
    return link;
}

int main()
{
    BossEncounter boss;
    boss.entry = 14507;   // a boss's creature entry
    boss.guid = 0x0000000100000002ull;
    boss.dbGuid = 4242;   // ... and the spawn row a creature_linking row names

    // A template link names the master entry: it is the boss only for the boss's own entry. Map 0 is no map yet: the
    // core's entry case allows it, and it is no GUID marker.
    CHECK(BossLinkNamesBoss(boss, EntryLink(boss.entry)));
    CHECK(BossLinkNamesBoss(boss, EntryLink(boss.entry, 0)));
    CHECK(!BossLinkNamesBoss(boss, EntryLink(boss.entry + 1)));

    // A template row's masterDBGuid does not make it a GUID link: the entry stays the evidence ...
    CHECK(BossLinkNamesBoss(boss, FollowEntryLink(boss.entry, 4243)));
    CHECK(!BossLinkNamesBoss(boss, FollowEntryLink(boss.entry + 1, 4243)));
    // ... and a unique master that is the boss's own spawn is the same evidence read as a guid.
    CHECK(BossLinkNamesBoss(boss, FollowEntryLink(boss.entry + 1, boss.dbGuid)));

    // A GUID link names the boss spawn, not his entry: the two numbers are independent.
    CHECK(boss.dbGuid != boss.entry);
    CHECK(BossLinkNamesBoss(boss, GuidLink(boss.dbGuid)));
    CHECK(!BossLinkNamesBoss(boss, GuidLink(boss.entry))); // the entry read as a spawn matches no row
    CHECK(!BossLinkNamesBoss(boss, GuidLink(boss.dbGuid + 1)));

    // Nothing to compare against: a boss spawn the glue did not read leaves a GUID link, and a template row whose only
    // evidence is that spawn, unmatched; a template row that names his entry still matches.
    BossEncounter unread;
    unread.entry = boss.entry;
    CHECK(!BossLinkNamesBoss(unread, GuidLink(boss.dbGuid)));
    CHECK(!BossLinkNamesBoss(unread, FollowEntryLink(boss.entry + 1, boss.dbGuid)));
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
