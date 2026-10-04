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
        {1, 24545}, {12, 24549}, {18, 24550}, {24, 24551},
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

inline uint32_t ShellShieldSpell() { return 26064; } // single rank, pet level 20

inline uint32_t HunterPetResistanceMinLevel() { return 20; }

inline std::vector<uint32_t> const& HunterPetResistanceSpells()
{
    static std::vector<uint32_t> const spells = {
        24493, // Arcane
        23992, // Fire
        24446, // Frost
        24492, // Nature
        24488, // Shadow
    };
    return spells;
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

inline uint32_t CobraReflexesSpell() { return 25076; }   // raptor, pet level 10
inline uint32_t StriderPresenceSpell() { return 36531; } // tallstrider, pet level 10
inline uint32_t PackleaderSpell() { return 36532; }     // hyena, pet level 20
inline uint32_t RoarOfFortitudeSpell() { return 36535; } // bear, pet level 30

inline PetRankLadder const& PoisonSpitLadder() // serpent (Turtle family 35)
{
    static PetRankLadder const ladder = { {15, 46271}, {45, 46272}, {60, 46273} };
    return ladder;
}

inline uint32_t PollenBurstSpell() { return 42051; } // moth (Turtle family 39), pet level 52

// Hunter pet family spells by beast_family (CreatureFamily DBC). Families 35
// (Serpent), 36 (Fox) and 39 (Moth) are Turtle additions with no vanilla
// table; without them those pets kept only Growl + passives. Growl and the
// passive/resistance rows are NOT per-family ladders below: the shared
// HunterPetWantedSpellIds helper adds them for every family, and both the
// missing-rank check and the teach path walk that one helper - so isUseful
// can never name a spell the teach path cannot add. Unknown families fall
// back to Bite + Cower so any future family still deals damage.
inline std::vector<PetRankLadder const*> HunterPetLadders(uint32_t beastFamily)
{
    static PetRankLadder const empty;
    switch (beastFamily)
    {
        case 1: // wolf
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &DashLadder(), &FuriousHowlLadder() };
            return ladders;
        }
        case 2: // cat
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &ClawLadder(), &CowerLadder(), &DashLadder(), &ProwlLadder() };
            return ladders;
        }
        case 3: // spider
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder() };
            return ladders;
        }
        case 4: // bear
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &ClawLadder(), &CowerLadder() };
            return ladders;
        }
        case 5: // boar
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &ChargeLadder(), &CowerLadder(), &DashLadder() };
            return ladders;
        }
        case 6: // crocolisk
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &DeathRollLadder() };
            return ladders;
        }
        case 7: // carrion bird
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &ClawLadder(), &CowerLadder(), &DiveLadder(), &ScreechLadder() };
            return ladders;
        }
        case 8: // crab
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &ClawLadder(), &CowerLadder(), &BubbleBarrierLadder() };
            return ladders;
        }
        case 9: // gorilla
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &ThunderstompLadder() };
            return ladders;
        }
        case 11: // raptor
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &ClawLadder(), &CowerLadder(), &DashLadder(), &SavageRendLadder() };
            return ladders;
        }
        case 12: // tallstrider
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &DashLadder() };
            return ladders;
        }
        case 20: // scorpid
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &ClawLadder(), &CowerLadder(), &ScorpidPoisonLadder() };
            return ladders;
        }
        case 21: // turtle
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder() };
            return ladders;
        }
        case 24: // bat
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &DiveLadder(), &ScreechLadder() };
            return ladders;
        }
        case 25: // hyena
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &DashLadder() };
            return ladders;
        }
        case 26: // owl
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &ClawLadder(), &CowerLadder(), &DiveLadder(), &ScreechLadder() };
            return ladders;
        }
        case 27: // wind serpent
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &DiveLadder(), &LightningBreathLadder() };
            return ladders;
        }
        case 35: // serpent (Turtle)
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &PoisonSpitLadder() };
            return ladders;
        }
        case 36: // fox (Turtle)
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &DashLadder() };
            return ladders;
        }
        case 39: // moth (Turtle)
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder(), &DiveLadder() };
            return ladders;
        }
        default:
        {
            static std::vector<PetRankLadder const*> ladders;
            if (ladders.empty())
                ladders = { &BiteLadder(), &CowerLadder() };
            return ladders;
        }
    }
}

// Single-rank family spells taught alongside the ladders: (beastFamily,
// minLevel, spellId). Cobra Reflexes / Strider Presence / Packleader / Roar
// of Fortitude / Pollen Burst / Shell Shield.
inline std::vector<std::pair<uint32_t, uint32_t>> HunterPetSingleSpells(uint32_t beastFamily)
{
    switch (beastFamily)
    {
        case 4: return { {30, RoarOfFortitudeSpell()} };
        case 8: return {};
        case 11: return { {10, CobraReflexesSpell()} };
        case 12: return { {10, StriderPresenceSpell()} };
        case 21: return { {20, ShellShieldSpell()} };
        case 25: return { {20, PackleaderSpell()} };
        case 39: return { {52, PollenBurstSpell()} };
        default: return {};
    }
}

// Shared rank-upkeep helpers: the missing-rank CHECK (action isUseful) and
// the TEACH path (factory InitPetSpells) both derive from these, so the
// check can never name a spell the teach path cannot add. The check looks
// only at the TOP rank each ladder allows: the core's Pet::AddSpell replaces
// a lower rank when the higher is learned, so lower ranks stay "unknown"
// forever and must not count as missing (else isUseful never clears).
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

// One flat (spellId, isPassive) row the check and the teach path both walk:
// top rank per ladder plus singles, in teach order. Passive rows (armor /
// stamina / resistances) ARE wanted - LearnSpell casts them once - but the
// callers skip ToggleAutocast for them, same as before. No vector: the
// caller passes a tiny sink, so the per-tick check pays no allocation.
struct PetWantedSpell
{
    uint32_t spellId = 0;
    bool passive = false;
};

template<typename Sink>
inline void ForEachHunterWantedSpell(uint32_t beastFamily, uint32_t petLevel, Sink&& sink)
{
    for (PetRankLadder const* ladder : HunterPetLadders(beastFamily))
    {
        if (!ladder)
            continue;
        if (uint32_t top = TopRankAtLevel(*ladder, petLevel))
            sink(PetWantedSpell{top, false});
    }
    for (auto const& single : HunterPetSingleSpells(beastFamily))
        if (petLevel >= single.first)
            sink(PetWantedSpell{single.second, false});
    if (uint32_t growl = TopRankAtLevel(GrowlLadder(), petLevel))
        sink(PetWantedSpell{growl, false});
    if (uint32_t armor = TopRankAtLevel(NaturalArmorLadder(), petLevel))
        sink(PetWantedSpell{armor, true});
    if (uint32_t stamina = TopRankAtLevel(GreatStaminaLadder(), petLevel))
        sink(PetWantedSpell{stamina, true});
    if (petLevel >= HunterPetResistanceMinLevel())
        for (uint32_t spellId : HunterPetResistanceSpells())
            sink(PetWantedSpell{spellId, true});
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
// (runtime/PetUpkeepPolicy.h: Prowl, Cower, Spell Lock, Devour Magic stay
// manual) plus Sacrifice and Seduction, which the sweep would leave on but
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
