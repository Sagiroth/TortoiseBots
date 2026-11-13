
#include "playerbot/playerbot.h"
#include "HunterTriggers.h"
#include "playerbot/GroupMembers.h"
#include "../../../runtime/PetRevivePolicy.h"
#include "../../../runtime/PetEngagePolicy.h"
#include "HunterActions.h"
#include "../../../runtime/HunterViperPolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/ThreatValues.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/HunterAoePolicy.h"

using namespace ai;

// A player-given "aspect <x>" strategy (HunterManualAspectStrategy) in either
// engine, or the saved `.bot choice` order: an explicit order beats the Viper
// automation. Only called after the cheap gates, so it costs nothing while
// mana is fine or the toggle is off.
static bool HasPlayerAspectOrder(PlayerbotAI* ai)
{
    std::string const stored =
        ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", "choice aspect")->Get();
    if (!stored.empty() && stored != "auto")
        return true;
    static char const* const names[] = { "aspect hawk", "aspect monkey", "aspect cheetah", "aspect pack", "aspect beast", "aspect wild" };
    for (char const* name : names)
        if (ai->HasStrategy(name, BotState::BOT_STATE_COMBAT) || ai->HasStrategy(name, BotState::BOT_STATE_NON_COMBAT))
            return true;
    return false;
}

static TortoiseBots::HunterViperInputs BuildViperInputs(PlayerbotAI* ai, Player* bot)
{
    TortoiseBots::HunterViperInputs in;
    in.toggleOn = sPlayerbotAIConfig.hunterViperOnLowMana;
    in.manaPct = ai->GetManaPercent();
    in.inViper = ai->HasAura("aspect of the viper", bot);
    return in;
}

namespace ai { std::string OrderedAspectFor(PlayerbotAI* ai); }

bool ChoiceAspectTrigger::IsActive()
{
    std::string const ordered = ai::OrderedAspectFor(ai);
    if (ordered.empty() || !ai->HasSpell(ordered))
        return false;
    Unit* target = GetTarget();
    return target && !ai->HasAura(ordered, bot);
}

bool AspectOfTheHawkTrigger::IsActive()
{
    // Toggle off: exactly the plain buff trigger.
    if (!sPlayerbotAIConfig.hunterViperOnLowMana)
        return BuffTrigger::IsActive();

    TortoiseBots::HunterViperInputs in = BuildViperInputs(ai, bot);
    if (in.inViper && in.manaPct < TortoiseBots::HUNTER_VIPER_EXIT_MANA_PCT)
    {
        in.playerAspectOrder = HasPlayerAspectOrder(ai);
        if (TortoiseBots::ViperHoldsCombatAspect(in))
            return false;
    }
    return BuffTrigger::IsActive();
}

// With a living tank in the group, mark only the mob the tank is fighting -
// not whichever mob is chewing on the hunter. "The tank's mob" is either what
// the tank is attacking or what is already attacking the tank; a solo hunter
// or a tank down marks freely.
bool HuntersMarkTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;

    bool const groupHasTank = HunterHasLivingTank(ai);
    bool targetIsTankTarget = false;
    if (groupHasTank)
    {
        for (Player* member : LiveGroupMembers(bot->GetGroup()))
        {
            if (!member || !member->IsAlive() || !PlayerbotAI::IsTank(member))
                continue;
            if (member->GetVictim() == target || target->GetVictim() == member)
            {
                targetIsTankTarget = true;
                break;
            }
        }
    }

    if (!ShouldHuntersMarkTarget(groupHasTank, targetIsTankTarget))
        return false;

    return DebuffTrigger::IsActive();
}

// The generic CC gate (HasCcTargetTrigger::CcTargetPending) holds dungeon
// crowd control to an assigned raid mark, so the trap never fired in a
// markless dungeon run. The trap goes on the chooser's own pick instead -
// the same target tournament every other crowd control follows: never over
// an existing CC, never a mob the group is already fighting, never dotted,
// never the kill target. While our own trap holds a mob the value goes
// empty, so no second trap is laid on top.
bool FreezingTrapTrigger::IsActive()
{
    // The action feigns before dropping the trap (traps cannot be laid in
    // combat), so Feign Death must be known - IsSpellReady is true for an
    // unlearned spell - and ready. Below level 30 the trap row stands down
    // instead of walking the hunter into melee for a cast that cannot happen.
    if (!ai->HasSpell("feign death") || !sServerFacade.IsSpellReady(bot, 5384))
        return false;

    uint32 const trapId = AI_VALUE2(uint32, "spell id", getName());
    if (!trapId || !sServerFacade.IsSpellReady(bot, trapId))
        return false;

    return AI_VALUE2(Unit*, "cc target", getName()) && !AI_VALUE2(Unit*, "current cc target", getName());
}

// The splash is measured around the current target: an attacker inside the
// 10 yd chain-jump radius (core CHAIN_SPELL_JUMP_RADIUS, starting at the
// shot's target) gets hit when the shot lands. Members must be fighting -
// an idle add selected by nobody never counts. A breakable CC inside the
// splash vetoes the whole shot; damage-immune members (banish, bubbles) are
// excluded from the count but do not veto. Stuns, fears and roots take the
// splash harmlessly and still count. Skull-marked mobs opted out of CC
// protection (same as the AoE interlock): their CC is treated as stale.
bool HunterMultiShotTrigger::IsActive()
{
    if (!SpellCanBeCastedTrigger::IsActive())
        return false;

    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;

    uint8_t splashTargets = 0;
    bool breakableCcInSplash = false;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit) || !unit->IsInCombat())
            continue;

        if (sServerFacade.getDistance2d(unit, target) > kHunterMultiShotSplashYd)
            continue;

        if (PossibleAttackTargetsValue::HasIgnoreCCRti(unit, bot))
        {
            ++splashTargets;
            continue;
        }

        if (PossibleAttackTargetsValue::HoldsBreakableCc(ai, unit, bot))
        {
            breakableCcInSplash = true;
            continue;
        }

        if (PossibleAttackTargetsValue::IsImmuneToDamage(unit, bot))
            continue;

        ++splashTargets;
    }

    return ShouldHunterMultiShot(splashTargets, breakableCcInSplash);
}

bool AspectOfTheViperLowManaTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.hunterViperOnLowMana)
        return false;

    TortoiseBots::HunterViperInputs in = BuildViperInputs(ai, bot);
    // Cheap mana read first: most ticks stop here.
    if (in.inViper || in.manaPct >= TortoiseBots::HUNTER_VIPER_ENTER_MANA_PCT)
        return false;
    in.movementAspect = ai->HasAura("aspect of the cheetah", bot) || ai->HasAura("aspect of the pack", bot);
    in.playerAspectOrder = HasPlayerAspectOrder(ai);
    return TortoiseBots::ShouldSwitchToViper(in) && BuffTrigger::IsActive();
}

bool HunterNoStingsActiveTrigger::IsActive()
{
    if (!ai->HasSpell("serpent sting") && !ai->HasSpell("scorpid sting") && !ai->HasSpell("viper sting"))
        return false;

	Unit* target = AI_VALUE(Unit*, "current target");
    return target && AI_VALUE2(uint8, "health", "current target") > 40 &&
        !ai->HasAura("serpent sting", target) &&
        !ai->HasAura("scorpid sting", target) &&
        !ai->HasAura("viper sting", target);
}

bool HuntersPetDeadTrigger::IsActive()
{
    return AI_VALUE(bool, "pet dead") && !AI_VALUE2(bool, "mounted", "self target");
}

// PET-5: dead pet plus nobody hitting the bot — the 10s Revive channel only
// starts when it won't be interrupted on the first tick. Cheap-first: pet
// state and attacker count are scalar reads; mounted last.
bool SafeToRevivePetTrigger::IsActive()
{
    TortoiseBots::PetReviveGateInputs gate;
    gate.petDead = AI_VALUE(bool, "pet dead");
    if (!gate.petDead)
        return false;
    gate.attackerCount = AI_VALUE(uint8, "my attacker count");
    if (gate.attackerCount > 0)
        return false;
    gate.mounted = AI_VALUE2(bool, "mounted", "self target");
    return TortoiseBots::CanRevivePetNow(gate);
}

bool HuntersPetLowHealthTrigger::IsActive()
{
    Unit* pet = AI_VALUE(Unit*, "pet target");
    return pet && AI_VALUE2(uint8, "health", "pet target") < 40 &&
        !AI_VALUE2(bool, "dead", "pet target") && !AI_VALUE2(bool, "mounted", "self target");
}

// Fires when the pet holds the enemy's attention AND pet taunts are stood
// down (grouped with a real tank), or — first, so the mob never turns on
// the pet — when the pet's threat on the target leads the tank's
// (runtime/PetEngagePolicy.h ShouldPetCower). Cheap-first: the victim check
// and group check run before the member walk inside the helper, so solo
// hunters never pay for the scan.
bool PetHasAggroTrigger::IsActive()
{
    Unit* pet = AI_VALUE(Unit*, "pet target");
    if (!pet || !pet->IsAlive())
        return false;
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;
    bool const petHasAggro = target->GetVictim() == pet;
    if (!bot->GetGroup() || ai::IsPetTauntAllowed(ai, bot))
        return false;
    TortoiseBots::PetThreatLeadInputs cower;
    cower.petHasAggro = petHasAggro;
    if (!petHasAggro)
    {
        // Threat-lead reads only for the pre-aggro case: two threat lookups
        // and the tank walk stay behind the victim check. A dead or absent
        // tank reports < 0 and the pet (the fallback holder) never cowers.
        cower.petThreat = sServerFacade.GetThreatManager(target).getThreat(pet);
        cower.tankThreat = ThreatValue::GetTankThreat(ai, target);
    }
    return TortoiseBots::ShouldPetCower(cower);
}

// PET-4 (donor GenericHunterStrategy.cpp:72-73): the medium band below
// MediumHealth (70) — heals chip damage before it becomes the low band.
// Overlaps low below 40; the low node outranks it so low wins there.
bool HuntersPetMediumHealthTrigger::IsActive()
{
    Unit* pet = AI_VALUE(Unit*, "pet target");
    return pet && AI_VALUE2(uint8, "health", "pet target") < sPlayerbotAIConfig.mediumHealth &&
        !AI_VALUE2(bool, "dead", "pet target") && !AI_VALUE2(bool, "mounted", "self target");
}

bool HunterPetNotHappy::IsActive()
{
    return !AI_VALUE(bool, "pet happy") && !AI_VALUE2(bool, "mounted", "self target");
}

bool ViperStingOnAttackerTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (target)
    {
        const bool noStings = !ai->HasAura("serpent sting", target) &&
                              !ai->HasAura("scorpid sting", target) &&
                              !ai->HasAura("viper sting", target);
        if (noStings)
        {
            if (target->GetPower(POWER_MANA) >= 10)
            {
                return DebuffOnAttackerTrigger::IsActive();
            }
        }
    }

    return false;
}

bool SerpentStingOnAttackerTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (target)
    {
        const bool noStings = !ai->HasAura("serpent sting", target) &&
                              !ai->HasAura("scorpid sting", target) &&
                              !ai->HasAura("viper sting", target);
        if (noStings)
        {
            if (target->GetPower(POWER_MANA) < 10)
            {
                return DebuffOnAttackerTrigger::IsActive();
            }
        }
    }

    return false;
}

// Volley is a channel: stop it once the pack is gone, the same rule as the
// warlock's Rain of Fire check. Turtle has three Volley ranks (1510, 14294,
// 14295); the WotLK ranks 27022/58431/58434 are not in its spell data.
bool VolleyChannelCheckTrigger::IsActive()
{
    if (Spell* spell = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        if (spell->m_spellInfo)
        {
            uint32 id = spell->m_spellInfo->Id;
            if (id == 1510 || id == 14294 || id == 14295)
            {
                // A unit under our crowd control inside the volley breaks the
                // hold on its next tick: stop the channel at once.
                CcGuard ccGuard(ai);
                if (ccGuard.CcInRunningArea(spell))
                    return true;
                return AI_VALUE(uint8, "aoe count") < 2;
            }
        }
    }
    return false;
}
