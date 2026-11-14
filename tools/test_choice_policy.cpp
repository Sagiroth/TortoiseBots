#include "../ai/playerbot/strategy/generic/ClassChoicePolicy.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(cond) do { if (!(cond)) { std::printf("FAILED: %s (line %d)\n", #cond, __LINE__); std::exit(1); } } while (0)

int main()
{
    CHECK(ai::ChoiceKindIndex("aura") == 0);
    CHECK(ai::ChoiceKindIndex("aspect") == 1);
    CHECK(ai::ChoiceKindIndex("curse") == 2);
    CHECK(ai::ChoiceKindIndex("poison_mh") == ai::CHOICE_POISON_MH);
    CHECK(ai::ChoiceKindIndex("poison_oh") == ai::CHOICE_POISON_OH);
    CHECK(ai::ChoiceKindIndex("sting") == ai::CHOICE_STING);
    CHECK(ai::ChoiceKindIndex("pet") == ai::CHOICE_PET);
    CHECK(ai::ChoiceKindIndex("poison") == -1);
    CHECK(ai::ChoiceKindIndex("demon") == -1);
    CHECK(ai::ChoiceKindIndex("") == -1);
    CHECK(std::string(ai::ChoiceKindWord(1)) == "aspect");
    CHECK(std::string(ai::ChoiceKindWord(9)) == "");
    CHECK(ai::ChoiceKindClassId(0) == 2);
    CHECK(ai::ChoiceKindClassId(1) == 3);
    CHECK(ai::ChoiceKindClassId(2) == 9);
    CHECK(ai::ChoiceKindClassId(7) == 0);
    CHECK(ai::ChoiceKindClassId(ai::CHOICE_POISON_MH) == 4);
    CHECK(ai::ChoiceKindClassId(ai::CHOICE_POISON_OH) == 4);
    CHECK(ai::ChoiceKindClassId(ai::CHOICE_STING) == 3);
    CHECK(ai::ChoiceKindClassId(ai::CHOICE_PET) == 9);
    // Several kinds per class, in wire order.
    CHECK(ai::ChoiceKindsForClass(2) == std::vector<int>({ ai::CHOICE_AURA }));
    CHECK(ai::ChoiceKindsForClass(3) == std::vector<int>({ ai::CHOICE_ASPECT, ai::CHOICE_STING }));
    CHECK(ai::ChoiceKindsForClass(9) == std::vector<int>({ ai::CHOICE_CURSE, ai::CHOICE_PET }));
    CHECK(ai::ChoiceKindsForClass(4) == std::vector<int>({ ai::CHOICE_POISON_MH, ai::CHOICE_POISON_OH }));
    CHECK(ai::ChoiceKindsForClass(1).empty());
    CHECK(ai::ChoiceKindsForClass(0).empty());
    // Kind words stay inside the addon's [%a_]+ pattern.
    for (int k = 0; k < ai::CHOICE_KIND_COUNT; ++k)
        for (char c : std::string(ai::ChoiceKindWord(k)))
            CHECK((c >= 'a' && c <= 'z') || c == '_');
    CHECK(ai::ChoiceKindUsesItem(ai::CHOICE_POISON_MH));
    CHECK(ai::ChoiceKindUsesItem(ai::CHOICE_POISON_OH));
    CHECK(!ai::ChoiceKindUsesItem(ai::CHOICE_STING));
    CHECK(!ai::ChoiceKindUsesItem(ai::CHOICE_PET));

    CHECK(ai::ChoiceWordCount(0) == 7);
    CHECK(ai::ChoiceWordCount(1) == 8);
    CHECK(ai::ChoiceWordCount(2) == 8);
    CHECK(ai::ChoiceWordCount(9) == 0);
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

    CHECK(ai::ChoiceWordCount(ai::CHOICE_POISON_MH) == 5);
    CHECK(ai::ChoiceWordCount(ai::CHOICE_STING) == 3);
    CHECK(ai::ChoiceWordCount(ai::CHOICE_PET) == 4);
    CHECK(ai::IsChoiceWord(ai::CHOICE_POISON_OH, "mind"));
    CHECK(!ai::IsChoiceWord(ai::CHOICE_POISON_OH, "anesthetic"));
    CHECK(ai::IsChoiceWord(ai::CHOICE_STING, "scorpid"));
    CHECK(!ai::IsChoiceWord(ai::CHOICE_STING, "wyvern"));
    CHECK(ai::IsChoiceWord(ai::CHOICE_PET, "felhunter"));
    CHECK(!ai::IsChoiceWord(ai::CHOICE_PET, "felguard"));
    CHECK(ai::ChoiceSpell(ai::CHOICE_POISON_MH, "mind") == "mind-numbing poison");
    CHECK(ai::ChoiceSpell(ai::CHOICE_POISON_OH, "deadly") == "deadly poison");
    CHECK(ai::ChoiceSpell(ai::CHOICE_STING, "viper") == "viper sting");
    CHECK(ai::ChoiceSpell(ai::CHOICE_PET, "voidwalker") == "summon voidwalker");
    CHECK(ai::ChoiceOrderKey(ai::CHOICE_POISON_MH) == "choice poison_mh");
    CHECK(ai::ChoiceOrderKey(ai::CHOICE_PET) == "choice pet");

    // Every poison word has items and weapon enchants, and no two words share one.
    for (int i = 0; i < ai::ChoiceWordCount(ai::CHOICE_POISON_MH); ++i)
    {
        std::string const word = ai::ChoiceWord(ai::CHOICE_POISON_MH, i);
        CHECK(!ai::ChoicePoisonItemIds(word).empty());
        CHECK(!ai::ChoicePoisonEnchantIds(word).empty());
        for (int j = i + 1; j < ai::ChoiceWordCount(ai::CHOICE_POISON_MH); ++j)
        {
            std::string const other = ai::ChoiceWord(ai::CHOICE_POISON_MH, j);
            for (std::uint32_t id : ai::ChoicePoisonEnchantIds(word))
                for (std::uint32_t otherId : ai::ChoicePoisonEnchantIds(other))
                    CHECK(id != otherId);
        }
    }
    CHECK(ai::ChoicePoisonItemIds("anesthetic").empty());

    CHECK(ai::ChoiceOrderedWord(ai::CHOICE_STING, "").empty());
    CHECK(ai::ChoiceOrderedWord(ai::CHOICE_STING, "auto").empty());
    CHECK(ai::ChoiceOrderedWord(ai::CHOICE_STING, "imp").empty());
    CHECK(ai::ChoiceOrderedWord(ai::CHOICE_STING, "scorpid") == "scorpid");

    // An order lets only its own row run; no order lets every row run.
    CHECK(ai::ChoiceRowAllowed("", "instant"));
    CHECK(ai::ChoiceRowAllowed("deadly", "deadly"));
    CHECK(!ai::ChoiceRowAllowed("deadly", "instant"));
    CHECK(!ai::ChoiceRowAllowed("scorpid", "serpent"));

    using ai::ChoicePetDecision;
    using ai::DecideChoicePet;
    CHECK(DecideChoicePet("", true, false, false, true, false) == ChoicePetDecision::LeaveAlone);
    CHECK(DecideChoicePet("felhunter", false, false, false, true, false) == ChoicePetDecision::LeaveAlone);
    CHECK(DecideChoicePet("felhunter", true, true, false, true, false) == ChoicePetDecision::LeaveAlone);
    CHECK(DecideChoicePet("felhunter", true, false, true, true, false) == ChoicePetDecision::WaitCombat);
    CHECK(DecideChoicePet("felhunter", true, false, false, false, false) == ChoicePetDecision::NoShard);
    CHECK(DecideChoicePet("felhunter", true, false, false, false, true) == ChoicePetDecision::Summon);
    CHECK(DecideChoicePet("felhunter", true, false, false, true, false) == ChoicePetDecision::Summon);
    CHECK(DecideChoicePet("imp", true, false, false, false, false) == ChoicePetDecision::Summon);

    CHECK(ai::ShouldNoCurseTriggerFire(""));
    CHECK(!ai::ShouldNoCurseTriggerFire("curse of the elements"));
    CHECK(!ai::ShouldNoCurseTriggerFire("curse of agony"));

    std::printf("All choice policy checks PASSED!\n");
    return 0;
}
