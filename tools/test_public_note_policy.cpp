// Standalone test for runtime/PublicNotePolicy.h (guild public note:
// spec, talents and both primary professions with skill, e.g.
// "Arms 31/20/0 Mi245 BS230"). No core, no DB: feeds ComposePublicNote
// plain values. Pins the spec label per class/tab, the abbreviation per
// profession, the 31-character core note cap (both professions fit for
// every spec at 300/300), and the skill-refresh throttle production uses
// (PlayerbotAI::RefreshPublicNote).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_public_note_policy.cpp -o /tmp/test_public_note
//   /tmp/test_public_note

#include "../runtime/PublicNotePolicy.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestAbbreviations()
{
    CHECK(std::string(PublicNoteProfessionAbbreviation(171)) == "Al");
    CHECK(std::string(PublicNoteProfessionAbbreviation(164)) == "BS");
    CHECK(std::string(PublicNoteProfessionAbbreviation(333)) == "En");
    CHECK(std::string(PublicNoteProfessionAbbreviation(202)) == "Eg");
    CHECK(std::string(PublicNoteProfessionAbbreviation(182)) == "Hb");
    CHECK(std::string(PublicNoteProfessionAbbreviation(165)) == "LW");
    CHECK(std::string(PublicNoteProfessionAbbreviation(186)) == "Mi");
    CHECK(std::string(PublicNoteProfessionAbbreviation(393)) == "Sk");
    CHECK(std::string(PublicNoteProfessionAbbreviation(197)) == "Ta");
    CHECK(PublicNoteProfessionAbbreviation(185) == nullptr); // Cooking is a secondary
    CHECK(PublicNoteProfessionAbbreviation(0) == nullptr);
}

// All 27 spec labels (class ids mirror SharedDefines.h).
static void TestSpecLabels()
{
    CHECK(std::string(PublicNoteSpecLabel(11, 0)) == "Bal");
    CHECK(std::string(PublicNoteSpecLabel(11, 1)) == "Feral");
    CHECK(std::string(PublicNoteSpecLabel(11, 2)) == "Resto");
    CHECK(std::string(PublicNoteSpecLabel(3, 0)) == "BM");
    CHECK(std::string(PublicNoteSpecLabel(3, 1)) == "MM");
    CHECK(std::string(PublicNoteSpecLabel(3, 2)) == "Surv");
    CHECK(std::string(PublicNoteSpecLabel(8, 0)) == "Arc");
    CHECK(std::string(PublicNoteSpecLabel(8, 1)) == "Fire");
    CHECK(std::string(PublicNoteSpecLabel(8, 2)) == "Frost");
    CHECK(std::string(PublicNoteSpecLabel(2, 0)) == "Holy");
    CHECK(std::string(PublicNoteSpecLabel(2, 1)) == "Prot");
    CHECK(std::string(PublicNoteSpecLabel(2, 2)) == "Ret");
    CHECK(std::string(PublicNoteSpecLabel(5, 0)) == "Disc");
    CHECK(std::string(PublicNoteSpecLabel(5, 1)) == "Holy");
    CHECK(std::string(PublicNoteSpecLabel(5, 2)) == "Shadow");
    CHECK(std::string(PublicNoteSpecLabel(4, 0)) == "Assn");
    CHECK(std::string(PublicNoteSpecLabel(4, 1)) == "Combat");
    CHECK(std::string(PublicNoteSpecLabel(4, 2)) == "Sub");
    CHECK(std::string(PublicNoteSpecLabel(7, 0)) == "Elem");
    CHECK(std::string(PublicNoteSpecLabel(7, 1)) == "Enh");
    CHECK(std::string(PublicNoteSpecLabel(7, 2)) == "Resto");
    CHECK(std::string(PublicNoteSpecLabel(9, 0)) == "Affl");
    CHECK(std::string(PublicNoteSpecLabel(9, 1)) == "Demo");
    CHECK(std::string(PublicNoteSpecLabel(9, 2)) == "Destro");
    CHECK(std::string(PublicNoteSpecLabel(1, 0)) == "Arms");
    CHECK(std::string(PublicNoteSpecLabel(1, 1)) == "Fury");
    CHECK(std::string(PublicNoteSpecLabel(1, 2)) == "Prot");
    CHECK(std::string(PublicNoteSpecLabel(0, 0)) == "Spec");
    CHECK(std::string(PublicNoteSpecLabel(1, 7)) == "Spec");
}

// The contract example: a 60 warrior with Mining 245 and Blacksmithing 230.
static void TestContractExample()
{
    PublicNoteProfession profs[] = { {186, 245}, {164, 230} };
    std::string note = ComposePublicNote(1, 0, 31, 20, 0, profs, 2);
    CHECK(note == "Arms 31/20/0 Mi245 BS230");
    CHECK(note.size() <= kPublicNoteMaxLength);
}

// No professions: talents only.
static void TestNoProfessions()
{
    CHECK(ComposePublicNote(8, 2, 10, 0, 41, nullptr, 0) == "Frost 10/0/41");
}

// One profession held: a single token.
static void TestOneProfession()
{
    PublicNoteProfession profs[] = { {393, 300} };
    CHECK(ComposePublicNote(3, 2, 0, 31, 20, profs, 1) == "Surv 0/31/20 Sk300");
}

// A third held primary (self-taught extras) never makes the note: the
// format carries at most two profession tokens.
static void TestThirdProfessionDropped()
{
    PublicNoteProfession profs[] = { {186, 100}, {202, 100}, {182, 100} };
    CHECK(ComposePublicNote(1, 0, 31, 20, 0, profs, 3) == "Arms 31/20/0 Mi100 Eg100");
}

// Non-primary ids (secondaries, weapon skills) are skipped, not rendered.
static void TestNonPrimarySkipped()
{
    PublicNoteProfession profs[] = { {185, 300}, {186, 75} };
    CHECK(ComposePublicNote(1, 0, 5, 5, 5, profs, 2) == "Arms 5/5/5 Mi75");
}

// Every one of the 27 specs carries BOTH professions at 300/300 within the
// cap, using the longest profession pair (two-letter codes + 3 digits each).
static void TestAllSpecsFitBothProfessions()
{
    uint32_t classes[] = { 11, 3, 8, 2, 5, 4, 7, 9, 1 };
    PublicNoteProfession profs[] = { {182, 300}, {393, 300} };
    for (uint32_t cls : classes)
        for (uint32_t tab = 0; tab < 3; ++tab)
        {
            std::string note = ComposePublicNote(cls, tab, 31, 20, 0, profs, 2);
            CHECK(note.size() <= kPublicNoteMaxLength);
            CHECK(note.find("Hb300") != std::string::npos);
            CHECK(note.find("Sk300") != std::string::npos);
        }
}

// The cap itself is the core limit, not a tunable: pin it.
static void TestCapValue()
{
    CHECK(kPublicNoteMaxLength == 31);
    CHECK(kPublicNoteSkillRefreshSec == 180);
}

// Throttle: the skill-driven refresh fires at most every few minutes.
// Production passes WorldTimer ms; the policy compares in ms.
static void TestRefreshThrottle()
{
    uint32_t const window = kPublicNoteSkillRefreshSec * 1000;
    CHECK(PublicNoteRefreshDue(0, 1000)); // never refreshed: due at once
    CHECK(!PublicNoteRefreshDue(1000, 1000));
    CHECK(!PublicNoteRefreshDue(1000, 1000 + window - 1));
    CHECK(PublicNoteRefreshDue(1000, 1000 + window));
    CHECK(PublicNoteRefreshDue(1000, 1000 + 2 * window));
}

// Input comparison: equal snapshots suppress the recompute.
static void TestInputsCompare()
{
    PublicNoteInputs a;
    a.classId = 1; a.tab = 0; a.tree0 = 31; a.tree1 = 20;
    a.skillId0 = 186; a.skillValue0 = 245; a.skillId1 = 164; a.skillValue1 = 230;
    PublicNoteInputs b = a;
    CHECK(a == b);
    CHECK(!(a != b));
    b.skillValue1 = 231;
    CHECK(a != b);
    b = a;
    b.tree2 = 1;
    CHECK(a != b);
}

int main()
{
    TestAbbreviations();
    TestSpecLabels();
    TestContractExample();
    TestNoProfessions();
    TestOneProfession();
    TestThirdProfessionDropped();
    TestNonPrimarySkipped();
    TestAllSpecsFitBothProfessions();
    TestCapValue();
    TestRefreshThrottle();
    TestInputsCompare();
    std::printf("test_public_note_policy ok (%d checks)\n", checks);
    return 0;
}
