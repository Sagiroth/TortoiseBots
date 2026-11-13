#include "../ai/playerbot/strategy/generic/ClassChoicePolicy.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(cond) do { if (!(cond)) { std::printf("FAILED: %s (line %d)\n", #cond, __LINE__); std::exit(1); } } while (0)

int main()
{
    CHECK(ai::ChoiceKindIndex("aura") == 0);
    CHECK(ai::ChoiceKindIndex("aspect") == 1);
    CHECK(ai::ChoiceKindIndex("curse") == 2);
    CHECK(ai::ChoiceKindIndex("sting") == -1);
    CHECK(ai::ChoiceKindIndex("") == -1);
    CHECK(std::string(ai::ChoiceKindWord(1)) == "aspect");
    CHECK(std::string(ai::ChoiceKindWord(9)) == "");
    CHECK(ai::ChoiceKindClassId(0) == 2);
    CHECK(ai::ChoiceKindClassId(1) == 3);
    CHECK(ai::ChoiceKindClassId(2) == 9);
    CHECK(ai::ChoiceKindClassId(7) == 0);
    CHECK(ai::ChoiceKindForClass(2) == 0);
    CHECK(ai::ChoiceKindForClass(3) == 1);
    CHECK(ai::ChoiceKindForClass(9) == 2);
    CHECK(ai::ChoiceKindForClass(1) == -1);
    CHECK(ai::ChoiceKindForClass(0) == -1);

    CHECK(ai::ChoiceWordCount(0) == 7);
    CHECK(ai::ChoiceWordCount(1) == 8);
    CHECK(ai::ChoiceWordCount(2) == 8);
    CHECK(ai::ChoiceWordCount(5) == 0);
    CHECK(ai::ChoiceWordIndex(0, "retribution") >= 0);
    CHECK(ai::ChoiceWordIndex(0, "sanctity") >= 0);
    CHECK(ai::ChoiceWordIndex(0, "hawk") < 0);
    CHECK(ai::ChoiceWordIndex(1, "hawk") >= 0);
    CHECK(ai::ChoiceWordIndex(1, "wolf") >= 0);
    CHECK(ai::ChoiceWordIndex(1, "viper") >= 0);
    CHECK(ai::ChoiceWordIndex(2, "agony") >= 0);
    CHECK(ai::ChoiceWordIndex(2, "doom") >= 0);
    CHECK(ai::ChoiceWordIndex(2, "exhaustion") >= 0);
    CHECK(ai::ChoiceWordIndex(2, "might") < 0);

    CHECK(ai::ChoiceSpell(0, "retribution") == "retribution aura");
    CHECK(ai::ChoiceSpell(0, "shadow") == "shadow resistance aura");
    CHECK(ai::ChoiceSpell(0, "fire") == "fire resistance aura");
    CHECK(ai::ChoiceSpell(0, "frost") == "frost resistance aura");
    CHECK(ai::ChoiceSpell(0, "sanctity") == "sanctity aura");
    CHECK(ai::ChoiceSpell(0, "hawk").empty());
    CHECK(ai::ChoiceSpell(1, "hawk") == "aspect of the hawk");
    CHECK(ai::ChoiceSpell(1, "wolf") == "aspect of the wolf");
    CHECK(ai::ChoiceSpell(1, "viper") == "aspect of the viper");
    CHECK(ai::ChoiceSpell(1, "retribution").empty());
    CHECK(ai::ChoiceSpell(2, "agony") == "curse of agony");
    CHECK(ai::ChoiceSpell(2, "elements") == "curse of the elements");
    CHECK(ai::ChoiceSpell(2, "doom") == "curse of doom");
    CHECK(ai::ChoiceSpell(2, "exhaustion") == "curse of exhaustion");
    CHECK(ai::ChoiceSpell(2, "hawk").empty());

    CHECK(ai::ChoiceOrderKey(0) == "choice aura");
    CHECK(ai::ChoiceOrderKey(1) == "choice aspect");
    CHECK(ai::ChoiceOrderKey(2) == "choice curse");
    CHECK(ai::IsChoiceWord(0, "auto"));
    CHECK(ai::IsChoiceWord(0, "devotion"));
    CHECK(!ai::IsChoiceWord(0, "hawk"));
    CHECK(!ai::IsChoiceWord(0, ""));
    CHECK(ai::IsChoiceWord(1, "pack"));
    CHECK(!ai::IsChoiceWord(1, "doom"));
    CHECK(ai::IsChoiceWord(2, "tongues"));
    CHECK(!ai::IsChoiceWord(2, "cheetah"));

    CHECK(ai::ShouldNoCurseTriggerFire(""));
    CHECK(!ai::ShouldNoCurseTriggerFire("curse of the elements"));
    CHECK(!ai::ShouldNoCurseTriggerFire("curse of agony"));

    std::printf("All choice policy checks PASSED!\n");
    return 0;
}
