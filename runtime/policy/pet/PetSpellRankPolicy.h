#pragma once

#include <cstdint>
#include <utility>
#include <vector>

// Pet spell-rank ladders (hunter + warlock) and teach-time autocast defaults.
//
// Server data, not guesses: every (minLevel, spellId) pair below was verified
// against tw_world.spell_template (baseLevel) and the SkillLineAbility DBC
// (family skill lines 208-218/236/251/653-656/1009-1011; only learnOnGetSkill
// == 0 rows, i.e. Beast-Training teachables). Vanilla ranks that never landed
// in this DB (Dive 23146, Dash 23100/23111/23112, skill-261 rows) are NOT
// listed; Turtle custom ranks that ARE trainable (Bubble Barrier, Death Roll,
// Savage Rend, Cobra Reflexes, Strider Presence, Packleader, Roar of
// Fortitude, Poison Spit, Pollen Burst, Thunderstomp 51156) are included.
//
// Why this exists: pool bots never visit a pet trainer (hunter) or read a
// Grimoire (warlock), so ranks only ever advance when the module teaches
// them. PlayerbotFactory::InitPetSpells consumes these ladders on every
// provisioning pass and on the "often" initialize-pet tick, learning every
// rank at/below the pet's level; the core's Pet::AddSpell replaces the lower
// rank automatically and inherits its autocast state, and the factory then
// pins the surviving top rank to ShouldPetSpellAutocastDefault. Pinned by
// tools/test_pet_spell_rank_policy.cpp.

namespace TortoiseBots
{

using PetRankLadder = std::vector<std::pair<uint32_t, uint32_t>>; // (minLevel, spellId)

inline PetRankLadder const& BiteLadder()
{
    static PetRankLadder const ladder = {
        {1, 17253}, {8, 17255}, {16, 17256}, {24, 17257},
        {32, 17258}, {40, 17259}, {48, 17260}, {56, 17261},
    };
    return ladder;
}

inline PetRankLadder const& ClawLadder()
{
    static PetRankLadder const ladder = {
        {1, 16827}, {8, 16828}, {16, 16829}, {24, 16830},
        {32, 16831}, {40, 16832}, {48, 3010}, {56, 3009},
    };
    return ladder;
}

inline PetRankLadder const& CowerLadder()
{
    static PetRankLadder const ladder = {
        {5, 1742}, {15, 1753}, {25, 1754}, {35, 1755}, {45, 1756}, {55, 16697},
    };
    return ladder;
}

inline PetRankLadder const& GrowlLadder()
{
    static PetRankLadder const ladder = {
        {1, 2649}, {10, 14916}, {20, 14917}, {30, 14918},
        {40, 14919}, {50, 14920}, {60, 14921},
    };
    return ladder;
}

inline PetRankLadder const& DiveLadder()
{
    // NOTE: 23146 looks like Dive rank 2 but is a skill-261-only row with no
    // family skill line; the real chain is 23145 -> 23147 -> 23148.
    static PetRankLadder const ladder = { {30, 23145}, {40, 23147}, {50, 23148} };
    return ladder;
}

inline PetRankLadder const& DashLadder()
{
    static PetRankLadder const ladder = { {30, 23099}, {40, 23109}, {50, 23110} };
    return ladder;
}

inline PetRankLadder const& ScreechLadder()
{
    static PetRankLadder const ladder = { {8, 24423}, {24, 24577}, {48, 24578}, {56, 24579} };
    return ladder;
}

inline PetRankLadder const& ProwlLadder()
{
    static PetRankLadder const ladder = { {30, 24450}, {40, 24452}, {50, 24453} };
    return ladder;
}

inline PetRankLadder const& FuriousHowlLadder()
{
    static PetRankLadder const ladder = { {10, 24604}, {24, 24605}, {40, 24603}, {56, 24597} };
    return ladder;
}

inline PetRankLadder const& ChargeLadder()
{
    static PetRankLadder const ladder = {
        {1, 7371}, {12, 26177}, {24, 26178}, {36, 26179}, {48, 26201}, {60, 27685},
    };
    return ladder;
}

inline PetRankLadder const& ScorpidPoisonLadder()
{
    static PetRankLadder const ladder = { {8, 24640}, {24, 24583}, {40, 24586}, {56, 24587} };
    return ladder;
}

inline PetRankLadder const& LightningBreathLadder()
{
    static PetRankLadder const ladder = {
        {1, 24844}, {12, 25008}, {24, 25009}, {36, 25010}, {48, 25011}, {60, 25012},
    };
    return ladder;
}

inline PetRankLadder const& ThunderstompLadder()
{
    static PetRankLadder const ladder = { {30, 26090}, {40, 26187}, {50, 26188}, {56, 51156} };
    return ladder;
}

inline PetRankLadder const& NaturalArmorLadder()
{
    static PetRankLadder const ladder = {
        {1, 24545}, {12, 24549}, {18, 24550}, {24, 24551}, {30, 24552},
        {36, 24553}, {42, 24554}, {48, 24555}, {54, 24629}, {60, 24630},
    };
    return ladder;
}

inline PetRankLadder const& GreatStaminaLadder()
{
    static PetRankLadder const ladder = {
        {1, 4187}, {12, 4188}, {18, 4189}, {24, 4190}, {30, 4191},
        {36, 4192}, {42, 4193}, {48, 4194}, {54, 5041}, {60, 5042},
    };
    return ladder;
}

// Resistances: ranks 1-4 at 20/30/40/50. The DB lists Shadow rank 4 at 40
// next to rank 3; kept at 50 like the other schools.
inline PetRankLadder const& ArcaneResistanceLadder()
{
    static PetRankLadder const ladder = { {20, 24493}, {30, 24497}, {40, 24500}, {50, 24501} };
    return ladder;
}

inline PetRankLadder const& FireResistanceLadder()
{
    static PetRankLadder const ladder = { {20, 23992}, {30, 24439}, {40, 24444}, {50, 24445} };
    return ladder;
}

inline PetRankLadder const& FrostResistanceLadder()
{
    static PetRankLadder const ladder = { {20, 24446}, {30, 24447}, {40, 24448}, {50, 24449} };
    return ladder;
}

inline PetRankLadder const& NatureResistanceLadder()
{
    static PetRankLadder const ladder = { {20, 24492}, {30, 24502}, {40, 24503}, {50, 24504} };
    return ladder;
}

inline PetRankLadder const& ShadowResistanceLadder()
{
    static PetRankLadder const ladder = { {20, 24488}, {30, 24505}, {40, 24506}, {50, 24507} };
    return ladder;
}

// Turtle custom family abilities (trainable per the SkillLineAbility DBC).
inline PetRankLadder const& BubbleBarrierLadder() // crab
{
    static PetRankLadder const ladder = { {10, 36523}, {24, 36524}, {40, 36525}, {56, 36526} };
    return ladder;
}

inline PetRankLadder const& DeathRollLadder() // crocolisk
{
    static PetRankLadder const ladder = {
        {10, 36548}, {20, 36549}, {30, 36550}, {40, 36551}, {50, 36552}, {60, 36553},
    };
    return ladder;
}

inline PetRankLadder const& SavageRendLadder() // raptor
{
    static PetRankLadder const ladder = {
        {1, 36536}, {12, 36537}, {24, 36538}, {36, 36539}, {48, 36540}, {60, 36541},
    };
    return ladder;
}

inline PetRankLadder const& PoisonSpitLadder() // serpent (Turtle family 35)
{
    static PetRankLadder const ladder = { {15, 46271}, {45, 46272}, {60, 46273} };
    return ladder;
}

// Single-rank family abilities, as one-row ladders so every ability goes
// through the same planner.
inline PetRankLadder const& CobraReflexesLadder() { static PetRankLadder const l = { {10, 25076} }; return l; }   // raptor, passive
inline PetRankLadder const& StriderPresenceLadder() { static PetRankLadder const l = { {10, 36531} }; return l; } // tallstrider
inline PetRankLadder const& PackleaderLadder() { static PetRankLadder const l = { {20, 36532} }; return l; }      // hyena
inline PetRankLadder const& RoarOfFortitudeLadder() { static PetRankLadder const l = { {30, 36535} }; return l; } // bear
inline PetRankLadder const& ShellShieldLadder() { static PetRankLadder const l = { {20, 26064} }; return l; }     // turtle
inline PetRankLadder const& WebLadder() { static PetRankLadder const l = { {20, 36533} }; return l; }             // spider
inline PetRankLadder const& GraceLadder() { static PetRankLadder const l = { {10, 46296} }; return l; }           // fox
inline PetRankLadder const& PollenBurstLadder() { static PetRankLadder const l = { {52, 42051} }; return l; }     // moth

// The core caps a hunter pet at 4 active abilities, Growl included
// (Pet::CanTakeMoreActiveSpells, ACTIVE_SPELLS_MAX), and spends training
// points of (loyalty - 1) x pet level; a pet that loads above level x 5
// spent points has every spell wiped (Pet::LoadPetFromDB).
inline constexpr uint32_t kHunterPetActiveSlots = 4;

inline int32_t HunterPetTrainingBudget(uint32_t petLevel, uint32_t loyaltyLevel)
{
    return loyaltyLevel > 1 ? int32_t(petLevel * (loyaltyLevel - 1)) : 0;
}

struct HunterPetBuildEntry
{
    PetRankLadder const* ladder = nullptr;
    bool passive = false;
};

// Best family build: the active abilities after Growl, in priority order,
// plus free family passives. Picks follow the Turtle hunter community:
// Bite as the damage button where the family has it, the family special,
// then a utility skill. Bite and Claw are never both in one build: with
// both on autocast the pet only ever casts Claw. Cower and Prowl are left
// out (threat drop / stealth the bot never uses).
inline std::vector<HunterPetBuildEntry> HunterPetBuild(uint32_t beastFamily)
{
    auto active = [](PetRankLadder const& ladder) { return HunterPetBuildEntry{&ladder, false}; };
    auto passive = [](PetRankLadder const& ladder) { return HunterPetBuildEntry{&ladder, true}; };
    switch (beastFamily)
    {
        case 1:  // wolf
        case 37: // plagued (wolf skill line)
        case 38: // horse (wolf skill line)
            return { active(BiteLadder()), active(FuriousHowlLadder()), active(DashLadder()) };
        case 2:  return { active(ClawLadder()), active(DashLadder()) };                                     // cat
        case 3:  return { active(BiteLadder()), active(WebLadder()) };                                      // spider
        case 4:  return { active(ClawLadder()), active(RoarOfFortitudeLadder()) };                          // bear
        case 5:  return { active(BiteLadder()), active(ChargeLadder()), active(DashLadder()) };             // boar
        case 6:  return { active(BiteLadder()), active(DeathRollLadder()) };                                // crocolisk
        case 7:  return { active(BiteLadder()), active(ScreechLadder()), active(DiveLadder()) };            // carrion bird
        case 8:  return { active(ClawLadder()), active(BubbleBarrierLadder()) };                            // crab
        case 9:  return { active(BiteLadder()), active(ThunderstompLadder()) };                             // gorilla
        case 11: return { active(BiteLadder()), active(SavageRendLadder()), active(DashLadder()),
                          passive(CobraReflexesLadder()) };                                                 // raptor
        case 12: return { active(BiteLadder()), active(StriderPresenceLadder()), active(DashLadder()) };   // tallstrider
        case 20: return { active(ClawLadder()), active(ScorpidPoisonLadder()) };                           // scorpid
        case 21: return { active(BiteLadder()), active(ShellShieldLadder()) };                              // turtle
        case 24: return { active(BiteLadder()), active(ScreechLadder()), active(DiveLadder()) };            // bat
        case 25: return { active(BiteLadder()), active(PackleaderLadder()), active(DashLadder()) };         // hyena
        case 26: return { active(ClawLadder()), active(ScreechLadder()), active(DiveLadder()) };            // owl
        case 27: return { active(BiteLadder()), active(LightningBreathLadder()), active(DiveLadder()) };    // wind serpent
        case 35: return { active(BiteLadder()), active(PoisonSpitLadder()) };                              // serpent (Turtle)
        case 36: return { active(BiteLadder()), active(GraceLadder()), active(DashLadder()) };              // fox (Turtle)
        case 39: return { active(BiteLadder()), active(PollenBurstLadder()), active(DiveLadder()) };        // moth (Turtle)
        default: return { active(BiteLadder()) };
    }
}

// Every hunter pet ladder the module trains or may find on a pet: the
// pool rebuild unlearns what the plan does not name, and an alt keeps the
// actives its player chose. Passive ladders are marked so they never take
// an active slot.
inline std::vector<HunterPetBuildEntry> const& HunterPetManagedLadders()
{
    static std::vector<HunterPetBuildEntry> const ladders = {
        {&GrowlLadder(), false}, {&BiteLadder(), false}, {&ClawLadder(), false}, {&CowerLadder(), false},
        {&DashLadder(), false}, {&DiveLadder(), false}, {&ProwlLadder(), false}, {&ScreechLadder(), false},
        {&FuriousHowlLadder(), false}, {&ChargeLadder(), false}, {&ScorpidPoisonLadder(), false},
        {&LightningBreathLadder(), false}, {&ThunderstompLadder(), false}, {&BubbleBarrierLadder(), false},
        {&DeathRollLadder(), false}, {&SavageRendLadder(), false}, {&PoisonSpitLadder(), false},
        {&StriderPresenceLadder(), false}, {&PackleaderLadder(), false}, {&RoarOfFortitudeLadder(), false},
        {&ShellShieldLadder(), false}, {&WebLadder(), false}, {&GraceLadder(), false},
        {&PollenBurstLadder(), false},
        {&CobraReflexesLadder(), true}, {&NaturalArmorLadder(), true}, {&GreatStaminaLadder(), true},
        {&ArcaneResistanceLadder(), true}, {&FireResistanceLadder(), true}, {&FrostResistanceLadder(), true},
        {&NatureResistanceLadder(), true}, {&ShadowResistanceLadder(), true},
    };
    return ladders;
}

// The managed ladder holding this spell id, with its rank index; nullptr
// for spells the module does not train (Avoidance, family passives the
// core teaches itself, ...).
inline HunterPetBuildEntry const* FindHunterPetLadder(uint32_t spellId, int& rankIndex)
{
    for (HunterPetBuildEntry const& entry : HunterPetManagedLadders())
        for (size_t i = 0; i < entry.ladder->size(); ++i)
            if ((*entry.ladder)[i].second == spellId)
            {
                rankIndex = int(i);
                return &entry;
            }
    rankIndex = -1;
    return nullptr;
}

// One planned spell: the rank the pet should end up with.
struct PetWantedSpell
{
    uint32_t spellId = 0;
    bool passive = false;
};

// Plans the pet's whole spell set within its training budget:
//   1. Growl (free, always);
//   2. `kept` - actives the alt's player taught, ranked up, never dropped;
//   3. the family build, until the 4 active slots are full;
//   4. Great Stamina, Natural Armor, then the five resistances one rank at a
//      time with whatever points are left.
// Each ladder takes its highest rank the level allows and the points cover.
// `cost(spellId)` is the rank's training-point cost (the caller returns a
// huge value for ids this core cannot teach, so they are never picked);
// `knownRank(ladder)` is the rank index the pet already holds or -1 - a
// held rank is never planned lower (the core will not learn a lower rank
// over a higher one) and its cost is charged as is.
template<typename CostFn, typename KnownFn, typename Sink>
inline void PlanHunterPetSpells(uint32_t beastFamily, uint32_t petLevel, int32_t budget,
    std::vector<PetRankLadder const*> const& kept, CostFn&& cost, KnownFn&& knownRank, Sink&& sink)
{
    uint32_t slots = kHunterPetActiveSlots;
    auto take = [&](PetRankLadder const& ladder, bool passive) -> bool
    {
        int known = knownRank(ladder);
        for (int i = int(ladder.size()) - 1; i >= 0; --i)
        {
            uint32_t const spellId = ladder[size_t(i)].second;
            bool const held = i <= known;
            if (!held && (petLevel < ladder[size_t(i)].first || cost(spellId) > budget))
                continue;
            budget -= cost(spellId);
            sink(PetWantedSpell{spellId, passive});
            return true;
        }
        return false;
    };
    auto isKept = [&kept](PetRankLadder const* ladder)
    {
        for (PetRankLadder const* k : kept)
            if (k == ladder)
                return true;
        return false;
    };

    if (take(GrowlLadder(), false))
        --slots;
    for (PetRankLadder const* ladder : kept)
        if (ladder && slots > 0 && take(*ladder, false))
            --slots;
    for (HunterPetBuildEntry const& entry : HunterPetBuild(beastFamily))
    {
        if (isKept(entry.ladder))
            continue;
        if (entry.passive)
            take(*entry.ladder, true);
        else if (slots > 0 && take(*entry.ladder, false))
            --slots;
    }
    take(GreatStaminaLadder(), true);
    take(NaturalArmorLadder(), true);

    PetRankLadder const* const schools[] = { &FireResistanceLadder(), &NatureResistanceLadder(),
        &ShadowResistanceLadder(), &FrostResistanceLadder(), &ArcaneResistanceLadder() };
    int rank[5] = { -1, -1, -1, -1, -1 };
    for (int s = 0; s < 5; ++s)
    {
        rank[s] = knownRank(*schools[s]);
        if (rank[s] >= 0)
            budget -= cost((*schools[s])[size_t(rank[s])].second);
    }
    for (size_t r = 0; r < 4; ++r)
        for (int s = 0; s < 5; ++s)
        {
            PetRankLadder const& ladder = *schools[s];
            if (rank[s] >= int(r) || rank[s] != int(r) - 1 || petLevel < ladder[r].first)
                continue;
            int32_t const delta = cost(ladder[r].second) - (r ? cost(ladder[r - 1].second) : 0);
            if (delta > budget)
                continue;
            budget -= delta;
            rank[s] = int(r);
        }
    for (int s = 0; s < 5; ++s)
        if (rank[s] >= 0)
            sink(PetWantedSpell{(*schools[s])[size_t(rank[s])].second, true});
}

// Highest rank of a ladder the level allows (warlock demons: no training
// points, every rank at its level).
inline uint32_t TopRankAtLevel(PetRankLadder const& ladder, uint32_t petLevel)
{
    uint32_t spellId = 0;
    for (auto const& rank : ladder)
    {
        if (petLevel < rank.first)
            break;
        spellId = rank.second;
    }
    return spellId;
}

// Warlock demon rank ladders by pet entry. Levels verified against
// tw_world.spell_template baseLevel; the Imp/Felhunter/Voidwalker/Succubus
// tables the factory already carried were correct and are preserved here.
inline PetRankLadder const* WarlockPetLadder(uint32_t petEntry, char const* line)
{
    static PetRankLadder const impBloodPact = { {4, 6307}, {14, 7804}, {26, 7805}, {38, 11766}, {50, 11767} };
    static PetRankLadder const impFireShield = { {14, 2947}, {24, 8316}, {34, 8317}, {44, 11770}, {54, 11771} };
    static PetRankLadder const impFirebolt = {
        {1, 3110}, {8, 7799}, {18, 7800}, {28, 7801}, {38, 7802}, {48, 11762}, {58, 11763},
    };
    static PetRankLadder const felhunterDevour = { {30, 19505}, {38, 19731}, {46, 19734}, {54, 19736} };
    static PetRankLadder const felhunterTainted = { {32, 19478}, {40, 19655}, {48, 19656}, {56, 19660} };
    static PetRankLadder const felhunterSpellLock = { {36, 19244}, {52, 19647} };
    static PetRankLadder const voidwalkerConsume = {
        {18, 17767}, {26, 17850}, {34, 17851}, {42, 17852}, {50, 17853}, {58, 17854},
    };
    static PetRankLadder const voidwalkerSacrifice = {
        {16, 7812}, {24, 19438}, {32, 19440}, {40, 19441}, {48, 19442}, {56, 19443},
    };
    static PetRankLadder const voidwalkerSuffering = { {24, 17735}, {36, 17750}, {48, 17751}, {60, 17752} };
    static PetRankLadder const voidwalkerTorment = {
        {10, 3716}, {20, 7809}, {30, 7810}, {40, 7811}, {50, 11774}, {60, 11775},
    };
    static PetRankLadder const succubusLash = {
        {20, 7814}, {28, 7815}, {36, 7816}, {44, 11778}, {52, 11779}, {60, 11780},
    };
    static PetRankLadder const succubusKiss = { {22, 6360}, {34, 7813}, {46, 11784}, {58, 11785} };

    auto match = [line](char const* name) {
        if (!line || !name)
            return false;
        for (char const *a = line, *b = name; *b; ++a, ++b)
            if (*a != *b)
                return false;
        return line[0] != '\0' || true;
    };
    (void)match;

    switch (petEntry)
    {
        case 416: // imp
            if (line[0] == 'b')
                return &impBloodPact;
            if (line[0] == 's' && line[1] == 'h')
                return &impFireShield;
            return &impFirebolt;
        case 417: // felhunter
            if (line[0] == 't')
                return &felhunterTainted;
            if (line[0] == 's')
                return &felhunterSpellLock;
            return &felhunterDevour;
        case 1860: // voidwalker
            if (line[0] == 'c')
                return &voidwalkerConsume;
            if (line[0] == 's' && line[1] == 'a')
                return &voidwalkerSacrifice;
            if (line[0] == 's' && line[1] == 'u')
                return &voidwalkerSuffering;
            return &voidwalkerTorment;
        case 1863: // succubus
            if (line[0] == 'k')
                return &succubusKiss;
            return &succubusLash;
        default:
            return nullptr;
    }
}

inline std::vector<std::pair<char const*, PetRankLadder const*>> WarlockPetLadders(uint32_t petEntry)
{
    switch (petEntry)
    {
        case 416:
            return { {"blood", WarlockPetLadder(416, "blood")}, {"shield", WarlockPetLadder(416, "shield")},
                     {"bolt", WarlockPetLadder(416, "bolt")} };
        case 417:
            return { {"devour", WarlockPetLadder(417, "devour")}, {"tainted", WarlockPetLadder(417, "tainted")},
                     {"spelllock", WarlockPetLadder(417, "spelllock")} };
        case 1860:
            return { {"consume", WarlockPetLadder(1860, "consume")}, {"sacrifice", WarlockPetLadder(1860, "sacrifice")},
                     {"suffering", WarlockPetLadder(1860, "suffering")},
                     {"torment", WarlockPetLadder(1860, "torment")} };
        case 1863:
            return { {"lash", WarlockPetLadder(1863, "lash")}, {"kiss", WarlockPetLadder(1863, "kiss")} };
        default:
            return {};
    }
}

// Single-rank warlock pet spells: (petEntry, minLevel, spellId).
inline std::vector<std::pair<uint32_t, uint32_t>> WarlockPetSingleSpells(uint32_t petEntry)
{
    switch (petEntry)
    {
        case 416: return { {12, 4511} };                       // Phase Shift
        case 417: return { {42, 19480} }; // Paranoia (Spell Lock is a rank ladder)
        case 1863: return { {32, 7870}, {26, 6358} };         // Lesser Invisibility, Seduction
        default: return {};
    }
}

// Every spell a demon at this level should know: top rank per line plus the
// single-rank rows. Same shared-table rule as the hunter helper: the check
// and the teach path both walk this, and the caller skips ids with no
// SpellEntry, so an unteachable row can never pin isUseful true.
template<typename Sink>
inline void ForEachWarlockWantedSpell(uint32_t petEntry, uint32_t petLevel, Sink&& sink)
{
    for (auto const& ladder : WarlockPetLadders(petEntry))
    {
        if (!ladder.second)
            continue;
        if (uint32_t top = TopRankAtLevel(*ladder.second, petLevel))
            sink(PetWantedSpell{top, false});
    }
    for (auto const& single : WarlockPetSingleSpells(petEntry))
        if (petLevel >= single.first)
            sink(PetWantedSpell{single.second, false});
}

// Teach-time autocast default. Mirrors the runtime sweep
// (runtime/PetUpkeepPolicy.h: Prowl, Cower, Spell Lock, Devour Magic and
// Sacrifice stay manual) plus Seduction, which the sweep would leave on but
// must never fire without an order: Sacrifice kills the Voidwalker, Seduction
// is a manual CC. Everything else (Growl, Torment, Firebolt, Bite, Claw,
// family abilities) starts autocast-on so a fresh or relearned rank works
// before the next sweep tick - and for owned/hired pets, which never sweep.
inline bool ShouldPetSpellAutocastDefault(uint32_t spellId)
{
    switch (spellId)
    {
        case 24450: case 24452: case 24453: // Prowl 1-3
        case 1742: case 1753: case 1754: case 1755: case 1756: case 16697: // Cower 1-6
        case 19244: case 19647: // Spell Lock 1-2
        case 19505: case 19731: case 19734: case 19736: // Devour Magic 1-4
        case 7812: case 19438: case 19440: case 19441: case 19442: case 19443: // Sacrifice 1-6
        case 6358: // Seduction
            return false;
        default:
            return true;
    }
}

} // namespace TortoiseBots
