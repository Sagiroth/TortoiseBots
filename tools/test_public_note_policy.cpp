// Standalone test for runtime/PublicNotePolicy.h (guild public note:
// spec, talents and both primary professions with skill, e.g.
// "Arms 31/20/0 Min245 BS230"). No core, no DB: feeds ComposePublicNote
// plain values. Pins the abbreviation per profession, the 31-character
// core note cap and the skill-refresh throttle production uses
// (TalentSpec::SetPublicNote, PlayerbotAI::UpdateAI).
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
    CHECK(std::string(PublicNoteProfessionAbbreviation(171)) == "Alc");
    CHECK(std::string(PublicNoteProfessionAbbreviation(164)) == "BS");
    CHECK(std::string(PublicNoteProfessionAbbreviation(333)) == "Enc");
    CHECK(std::string(PublicNoteProfessionAbbreviation(202)) == "Eng");
    CHECK(std::string(PublicNoteProfessionAbbreviation(182)) == "Herb");
    CHECK(std::string(PublicNoteProfessionAbbreviation(165)) == "LW");
    CHECK(std::string(PublicNoteProfessionAbbreviation(186)) == "Min");
    CHECK(std::string(PublicNoteProfessionAbbreviation(393)) == "Skin");
    CHECK(std::string(PublicNoteProfessionAbbreviation(197)) == "Tail");
    CHECK(PublicNoteProfessionAbbreviation(185) == nullptr); // Cooking is a secondary
    CHECK(PublicNoteProfessionAbbreviation(0) == nullptr);
}

// The contract example from the plan: a 60 warrior with Mining 245 and
// Blacksmithing 230. The spec name arrives lowercase from the chat helper
// and is capitalised for the note.
static void TestContractExample()
{
    PublicNoteProfession profs[] = { {186, 245}, {164, 230} };
    std::string note = ComposePublicNote("arms", 31, 20, 0, profs, 2);
    CHECK(note == "Arms 31/20/0 Min245 BS230");
    CHECK(note.size() <= kPublicNoteMaxLength);
}

// No professions: talents only.
static void TestNoProfessions()
{
    std::string note = ComposePublicNote("frost", 10, 0, 41, nullptr, 0);
    CHECK(note == "Frost 10/0/41");
}

// One profession held: a single token.
static void TestOneProfession()
{
    PublicNoteProfession profs[] = { {393, 300} };
    CHECK(ComposePublicNote("survival", 0, 31, 20, profs, 1) == "Survival 0/31/20 Skin300");
}

// A third held primary (self-taught extras) never makes the note: the
// format carries at most two profession tokens.
static void TestThirdProfessionDropped()
{
    PublicNoteProfession profs[] = { {186, 100}, {202, 100}, {182, 100} };
    std::string note = ComposePublicNote("arms", 31, 20, 0, profs, 3);
    CHECK(note == "Arms 31/20/0 Min100 Eng100");
}

// Non-primary ids (secondaries, weapon skills) are skipped, not rendered.
static void TestNonPrimarySkipped()
{
    PublicNoteProfession profs[] = { {185, 300}, {186, 75} };
    CHECK(ComposePublicNote("arms", 5, 5, 5, profs, 2) == "Arms 5/5/5 Min75");
}

// Long spec names ("beast mastery") push the note near the 31-char cap:
// the second profession token is dropped rather than truncated.
static void TestLengthCapDropsSecondProfession()
{
    PublicNoteProfession profs[] = { {182, 300}, {171, 300} };
    std::string note = ComposePublicNote("beast mastery", 20, 0, 31, profs, 2);
    CHECK(note.size() <= kPublicNoteMaxLength);
    CHECK(note == "Beast mastery 20/0/31 Herb300");
    CHECK(note.find("Alc") == std::string::npos);
}

// Worst case stays within the cap: the longest spec name plus two
// max-skill tokens is cut to what fits, never over 31 characters.
static void TestWorstCaseWithinCap()
{
    PublicNoteProfession profs[] = { {182, 300}, {393, 300} };
    std::string specs[] = { "beast mastery", "feral combat", "discipline", "protection", "assasination" };
    for (auto const& spec : specs)
    {
        std::string note = ComposePublicNote(spec, 31, 20, 0, profs, 2);
        CHECK(note.size() <= kPublicNoteMaxLength);
    }
}

// The cap itself is the core limit, not a tunable: pin it.
static void TestCapValue()
{
    CHECK(kPublicNoteMaxLength == 31);
    CHECK(kPublicNoteSkillRefreshSec == 180);
}

// Throttle: the skill-driven refresh fires at most every few minutes.
static void TestRefreshThrottle()
{
    CHECK(!PublicNoteRefreshDue(1000, 1000));
    CHECK(!PublicNoteRefreshDue(1000, 1000 + 179));
    CHECK(PublicNoteRefreshDue(1000, 1000 + 180));
    CHECK(PublicNoteRefreshDue(1000, 1000 + 3600));
    CHECK(PublicNoteRefreshDue(0, 1000000)); // never refreshed yet
}

int main()
{
    TestAbbreviations();
    TestContractExample();
    TestNoProfessions();
    TestOneProfession();
    TestThirdProfessionDropped();
    TestNonPrimarySkipped();
    TestLengthCapDropsSecondProfession();
    TestWorstCaseWithinCap();
    TestCapValue();
    TestRefreshThrottle();
    std::printf("test_public_note_policy ok (%d checks)\n", checks);
    return 0;
}
