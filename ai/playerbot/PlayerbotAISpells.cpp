#include "playerbot/strategy/ZulGurubCurePolicy.h"
#include "playerbot/playerbot.h"
#include "PlayerbotAI.h"
#include "playerbot/GroupMembers.h"
#include "../../runtime/RemoteCommanderTasks.h"
#include "playerbot/InterruptCastingPolicy.h"
#include "strategy/values/LastMovementValue.h"
#include "strategy/values/LastSpellCastValue.h"
#include "strategy/values/PositionValue.h"
#include "LootObjectStack.h"
#include "BotActionLog.h"
#include "Spells/SpellMgr.h"
#include "playerbot/strategy/values/MountValues.h"
#include "playerbot/MountDismountPolicy.h"

using namespace ai;

namespace
{
// Would Spell::prepare() refuse this cast for lack of power?
//
// The throwaway Spell the CanCastSpell probes build never runs prepare(), and
// m_powerCost is documented as "initialized only in Spell::prepare"
// (Spells/Spell.h), so CheckCast(true) approves every power-starved cast. The
// action was then selected, executed and refused by prepare() with
// SPELL_FAILED_NO_POWER - 88k of 190k live cast failures over 47 minutes, and
// nearly all of them rogue energy for Sinister Strike/Eviscerate. This mirrors
// the core's own gate (Spell::CheckPower) for the probe, so the AI picks
// something else instead of burning the attempt on a cast that cannot start.
//
// The cost is deliberately computed without caster spellmods: ApplySpellMod()
// spends mod charges (Player::DropModCharge) and a probe must not consume the
// charge the real cast needs. Cost-reduction procs (Clearcasting and the like)
// are therefore not priced in, so such a cast waits until the bot could pay
// the full price anyway; the probe never approves a cast the core will refuse.
SpellCastResult CheckSpellPower(SpellEntry const* spellInfo, Unit* caster, Item* castItem)
{
    // Item casts pay no power (Spell::CheckPower).
    if (!spellInfo || !caster || castItem)
        return SPELL_CAST_OK;

    // POWER_HEALTH sits outside MAX_POWERS and needs health, not power, so it
    // is the one "invalid" power type that must still be measured.
    if (spellInfo->powerType >= MAX_POWERS && spellInfo->powerType != POWER_HEALTH)
        return SPELL_CAST_OK;

    uint32 const cost = Spell::CalculatePowerCost(spellInfo, caster, nullptr, nullptr);
    if (spellInfo->powerType == POWER_HEALTH)
        return caster->GetHealth() > cost ? SPELL_CAST_OK : SPELL_FAILED_CASTER_AURASTATE;

    return caster->GetPower(Powers(spellInfo->powerType)) < cost ? SPELL_FAILED_NO_POWER : SPELL_CAST_OK;
}
} // namespace

bool IsRealAura(Player* bot, Aura const* aura, Unit* unit)
{
    if (!aura)
        return false;

    if (!sServerFacade.IsHostileTo(unit, bot))
        return true;

    uint32 stacks = aura->GetStackAmount();
    if (stacks >= aura->GetSpellProto()->StackAmount)
        return true;

    if (aura->GetCaster() == bot || IsPositiveSpell(aura->GetSpellProto()) || aura->IsAreaAura())
        return true;

    return false;
}


bool PlayerbotAI::HasAura(std::string name, Unit* unit, bool maxStack, bool checkIsOwner, int maxAuraAmount, bool hasMyAura, int minDuration, int auraTypeId)
{
    if (!unit)
        return false;

    std::wstring wnamepart;

    std::vector<uint32> ids = chatHelper.SpellIds(name);

    if (ids.empty())
    {
        //sLog.outError("Please add %s to spell list", name.c_str());
        if (!Utf8toWStr(name, wnamepart))
            return 0;

        wstrToLower(wnamepart);
    }

    int auraAmount = 0;

	for (uint32 auraType = SPELL_AURA_BIND_SIGHT; auraType < TOTAL_AURAS; auraType++)
	{
        if (auraTypeId != TOTAL_AURAS && auraType != auraTypeId)
            continue;

		Unit::AuraList const& auras = unit->GetAurasByType((AuraType)auraType);

        if (auras.empty())
            continue;

        for (Unit::AuraList::const_iterator i = auras.begin(); i != auras.end(); i++)
        {
            Aura* aura = *i;
            if (!aura)
                continue;

            if (ids.empty())
            {
                const std::string auraName = aura->GetSpellProto()->SpellName[0];
                if (auraName.empty() || auraName.length() != wnamepart.length() || !Utf8FitTo(auraName, wnamepart))
                    continue;
            }
            else
            {
                if (std::find(ids.begin(), ids.end(), aura->GetSpellProto()->Id) == ids.end())
                    continue;
            }

			if (IsRealAura(bot, aura, unit))
            {
                if (hasMyAura && aura->GetHolder())
                {
                    if (aura->GetHolder()->GetCasterGuid() == bot->getObjectGuid())
                        return true;
                    else
                        continue;
                }

                if (checkIsOwner && aura->GetHolder())
                {
                    if (aura->GetHolder()->GetCasterGuid() != bot->getObjectGuid())
                        continue;
                }

                uint32 maxStackAmount = aura->GetSpellProto()->StackAmount;
                uint32 maxProcCharges = aura->GetSpellProto()->procCharges;

                if (maxStack)
                {
                    if (maxStackAmount && aura->GetStackAmount() >= maxStackAmount)
                        auraAmount++;

                    if (maxProcCharges && aura->GetHolder()->GetAuraCharges() >= maxProcCharges)
                        auraAmount++;
                }
                else
                    auraAmount++;

                bool minDurationPassed = true;

                if (minDuration > 0)
                {
                    int32 auraDuration = aura->GetHolder()->GetAuraDuration();
                    minDurationPassed = minDuration <= auraDuration;
                }

                if (maxAuraAmount < 0 && minDurationPassed)
                    return auraAmount > 0;
            }
		}
    }

    if (maxAuraAmount >= 0)
    {
        return auraAmount == maxAuraAmount || (auraAmount > 0 && auraAmount <= maxAuraAmount);
    }

    return false;
}

bool PlayerbotAI::HasAura(uint32 spellId, Unit* unit, bool checkOwner)
{
    Aura* aura = GetAura(spellId, unit, checkOwner);
    return aura != nullptr;
}

Aura* PlayerbotAI::GetAura(uint32 spellId, Unit* unit, bool checkIsOwner)
{
    Aura* aura = nullptr;
    if (spellId != 0 && unit)
    {
        for (uint32 effect = EFFECT_INDEX_0; effect <= EFFECT_INDEX_2; effect++)
        {
            Aura* auraTmp = ((Unit*)unit)->GetAura(spellId, (SpellEffectIndex)effect);
            if (IsRealAura(bot, auraTmp, (Unit*)unit))
            {
                if (checkIsOwner)
                {
                    if (aura->GetHolder() && aura->GetHolder()->GetCasterGuid() == bot->getObjectGuid())
                    {
                        aura = auraTmp;
                        break;
                    }
                }
                else
                {
                    aura = auraTmp;
                    break;
                }
            }
        }
    }

    return aura;
}

Aura* PlayerbotAI::GetAura(std::string name, Unit* unit, bool checkIsOwner)
{
    if (!name.empty() && unit)
    {
        std::wstring wnamepart;
        std::vector<uint32> ids = chatHelper.SpellIds(name);

        if (ids.empty())
        {
            //sLog.outError("Please add %s to spell list", name.c_str());
            if (!Utf8toWStr(name, wnamepart))
                return 0;

            wstrToLower(wnamepart);
        }

        for (uint32 auraType = SPELL_AURA_BIND_SIGHT; auraType < TOTAL_AURAS; auraType++)
        {
            Unit::AuraList const& auras = unit->GetAurasByType((AuraType)auraType);

            if (auras.empty())
                continue;

            for (Unit::AuraList::const_iterator i = auras.begin(); i != auras.end(); i++)
            {
                Aura* aura = *i;
                if (!aura)
                    continue;

                if (ids.empty())
                {
                    const std::string auraName = aura->GetSpellProto()->SpellName[0];
                    if (auraName.empty() || auraName.length() != wnamepart.length() || !Utf8FitTo(auraName, wnamepart))
                        continue;
                }
                else
                {
                    if (std::find(ids.begin(), ids.end(), aura->GetSpellProto()->Id) == ids.end())
                        continue;
                }

                if (IsRealAura(bot, aura, unit))
                {
                    if (checkIsOwner && aura->GetHolder())
                    {
                        if (aura->GetHolder()->GetCasterGuid() == bot->getObjectGuid())
                        {
                            return aura;
                        }
                    }
                    else
                    {
                        return aura;
                    }
                }
            }
        }
    }

    return nullptr;
}

std::vector<Aura*> PlayerbotAI::GetAuras(Unit* unit, bool allAuras, bool positive)
{
    std::vector<Aura*> outAuras;
    for (uint32 auraType = SPELL_AURA_BIND_SIGHT; auraType < TOTAL_AURAS; auraType++)
    {
        Unit::AuraList const& auras = unit->GetAurasByType((AuraType)auraType);

        if (auras.empty())
            continue;

        for (Unit::AuraList::const_iterator i = auras.begin(); i != auras.end(); i++)
        {
            Aura* aura = *i;
            if (aura)
            {
                if (allAuras || (positive && aura->IsPositive()) || (!positive && !aura->IsPositive()))
                {
                    outAuras.push_back(aura);
                }
            }
        }
    }

    return outAuras;
}

bool PlayerbotAI::HasAnyAuraOf(Unit* player, ...)
{
    if (!player)
        return false;

    va_list vl;
    va_start(vl, player);

    const char* cur;
    do {
        cur = va_arg(vl, const char*);
        if (cur && HasAura(cur, player)) {
            va_end(vl);
            return true;
        }
    }
    while (cur);

    va_end(vl);
    return false;
}

bool PlayerbotAI::GetSpellRange(std::string name, float* maxRange, float* minRange)
{
    // Copied from Spell::GetMinMaxRange
    const uint32 spellId = aiObjectContext->GetValue<uint32>("spell id", name)->Get();
    if(spellId)
    {
        const SpellEntry* spellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (spellInfo)
        {
            const SpellRangeEntry* spellRangeEntry = sServerFacade.LookupSpellRangeEntry(spellInfo->rangeIndex);
            if(spellRangeEntry)
            {
                float spellMinRange = 0.0f, spellMaxRange = 0.0f, rangeMod = 0.0f;
                if (spellInfo->rangeIndex == SPELL_RANGE_IDX_COMBAT)
                    rangeMod = bot->GetCombatReach();
                else
                {
                    spellMinRange = spellRangeEntry->minRange;
                    spellMaxRange = spellRangeEntry->maxRange;
                }

                if (spellInfo->Attributes & SPELL_ATTR_USES_RANGED_SLOT)
                {
                    if (Item* rangedWeapon = bot->GetWeaponForAttack(RANGED_ATTACK))
                    {
                        spellMaxRange *= rangedWeapon->GetProto()->RangedModRange * 0.01f;
                    }
                }

                if (Player* modOwner = bot->GetSpellModOwner())
                {
                    modOwner->ApplySpellMod(spellInfo->Id, SPELLMOD_RANGE, spellMaxRange);
                }

                spellMaxRange += rangeMod;

                if(maxRange)
                {
                    *maxRange = spellMaxRange;
                }

                if(minRange)
                {
                    *minRange = spellMinRange;
                }

                return true;
            }
        }
    }

    return false;
}

uint32 PlayerbotAI::GetSpellCastDuration(Spell* spell)
{
    uint32 spellDuration = 0;
    if(spell)
    {
        const SpellEntry* const pSpellInfo = spell->m_spellInfo;
        spellDuration = spell->GetCastTime();
        if (IsChanneledSpell(pSpellInfo))
        {
            int32 duration = GetSpellDuration(pSpellInfo);
            if (duration > 0)
            {
                spellDuration += duration;
            }
        }

        spellDuration = ceil(spellDuration);

        // fix Feign Death
        if (pSpellInfo->Id == 5384)
        {
            spellDuration = 1000;
        }
        // fix cannibalize
        else if (pSpellInfo->Id == 20577)
        {
            spellDuration = 10000;
        }

        uint32 globalCooldown = CalculateGlobalCooldown(pSpellInfo->Id);
        if (spellDuration < (int32)globalCooldown)
        {
            spellDuration = globalCooldown;
        }
    }

    return spellDuration + sPlayerbotAIConfig.reactDelay;
}

bool PlayerbotAI::HasSpell(std::string name) const
{
    return HasSpell(aiObjectContext->GetValue<uint32>("spell id", name)->Get());
}

bool PlayerbotAI::HasSpell(uint32 spellid) const
{
    Pet* pet = bot->GetPet();
    if (pet && pet->HasSpell(spellid))
    {
        return true;
    }
    else if (bot->HasSpell(spellid))
    {
        return true;
    }

    return false;
}

SpellCastResult PlayerbotAI::CheckSpellTargetAlignment(SpellEntry const* spellInfo, Unit* target)
{
    // Consider neutral spells (spells that are neither positive or negative (e.g. feign death, hunter traps, ...)
    const std::list<uint32> neutralSpells = { 1499, 5384, 13795, 13809, 13813, 14302, 14303, 14304, 14305, 14310, 14311, 14316, 14317 };
    const bool neutralSpell = std::find(neutralSpells.begin(), neutralSpells.end(), spellInfo->Id) != neutralSpells.end();
    if (neutralSpell)
        return SPELL_CAST_OK;

    const bool positiveSpell = IsPositiveSpell(spellInfo);
    if (positiveSpell && sServerFacade.IsHostileTo(bot, target))
        return SPELL_FAILED_TARGET_ENEMY;

    if (!positiveSpell && sServerFacade.IsFriendlyTo(bot, target))
        return SPELL_FAILED_TARGET_FRIENDLY;

    return SPELL_CAST_OK;
}

bool PlayerbotAI::CanCastSpell(std::string name, Unit* target, uint8 effectMask, Item* itemTarget, bool ignoreRange, bool ignoreInCombat, bool ignoreMount, SpellCastResult* checkResult)
{
    return CanCastSpell(aiObjectContext->GetValue<uint32>("spell id", name)->Get(), target, 0, true, itemTarget, ignoreRange, ignoreInCombat, ignoreMount, checkResult);
}

bool PlayerbotAI::CanCastSpell(uint32 spellid, Unit* target, uint8 effectMask, bool checkHasSpell, Item* itemTarget, bool ignoreRange, bool ignoreInCombat, bool ignoreMount, SpellCastResult* checkResult)
{
    if (checkResult)
        return CheckCanCastSpell(spellid, target, effectMask, checkHasSpell, itemTarget, ignoreRange, ignoreInCombat, ignoreMount, checkResult);

    // Callers that ask for no result: remember why the check failed, only when it did fail (a result the check
    // lets through, such as MOVING or an ignored range, is not a reason), for the remote commander's feed.
    SpellCastResult localCheck = SPELL_CAST_OK;
    bool const canCast = CheckCanCastSpell(spellid, target, effectMask, checkHasSpell, itemTarget, ignoreRange, ignoreInCombat, ignoreMount, &localCheck);
    if (!canCast && localCheck != SPELL_CAST_OK)
        NoteCastCheckFail(localCheck);
    return canCast;
}

bool PlayerbotAI::CheckCanCastSpell(uint32 spellid, Unit* target, uint8 effectMask, bool checkHasSpell, Item* itemTarget, bool ignoreRange, bool ignoreInCombat, bool ignoreMount, SpellCastResult* checkResult)
{
    if (!spellid)
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_KNOWN;
        }

        return false;
    }

    Pet* pet = bot->GetPet();
    if (pet && pet->HasSpell(spellid) && !pet->HasSpellCooldown(spellid))
    {
        if (checkResult)
        {
            *checkResult = SPELL_CAST_OK;
        }

        return true;
    }

    if (bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        // Spells that can be casted while out of control
        const std::list<uint32> ignoreOutOfControllSpells = { 642, 1020, 1499, 1953, 7744, 11958, 13795, 13809, 13813, 14302, 14303, 14304, 14305, 14310, 14311, 14316, 14317 };
        if (std::find(ignoreOutOfControllSpells.begin(), ignoreOutOfControllSpells.end(), spellid) == ignoreOutOfControllSpells.end())
        {
            if (checkResult)
            {
                *checkResult = SPELL_FAILED_NOT_IN_CONTROL;
            }

            return false;
        }
    }

    if (!target)
        target = bot;

    if (checkHasSpell && !HasSpell(spellid))
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_KNOWN;
        }

        return false;
    }

    if (!sServerFacade.IsSpellReady(bot, spellid))
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_READY;
        }

        return false;
    }

	SpellEntry const *spellInfo = sServerFacade.LookupSpellInfo(spellid);
	if (!spellInfo)
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_KNOWN;
        }

        return false;
    }

    // A mounted travelling bot must not start an out-of-combat cast the core
    // would strip its mount for (issue #1097: a Blessing of Wisdom refresh
    // every few seconds kept a paladin dismounting and re-mounting in place
    // for 30+ min). Every bot spell cast on a unit passes here, so the buff
    // waits for the next dismounted stop. Combat casts, mount spells (incl.
    // travel form / ghost wolf), ride-safe spells and callers that pass
    // ignoreMount (reach probes) are not affected; loot/gather and commanded
    // casts dismount first, so the mounted check lets them through.
    if (!ignoreMount && bot->IsMounted() &&
        !(spellInfo->Attributes & SPELL_ATTR_CASTABLE_WHILE_MOUNTED) &&
        !MountValue::IsMountSpell(spellid) && aiObjectContext &&
        !ai::MountedTravelCastAllowed(bot->IsInCombat(),
            aiObjectContext->GetValue<bool>("travel target traveling")->Get()))
    {
        if (checkResult)
            *checkResult = SPELL_FAILED_NOT_MOUNTED;
        return false;
    }

    // Spell::prepare() refuses a new cast while another non-melee spell is
    // still being cast, and that is where SPELL_FAILED_SPELL_IN_PROGRESS comes
    // from. CheckCast(true) alone does not cover it, so without the same gate
    // here the AI re-attempted the spell every tick from the moment the cast
    // bar ended until the server's spell state cleared - 98k of 190k live cast
    // failures over 47 minutes (Smite, Shadow Bolt, Fireball, heals). The
    // expression is spelled exactly as Spell::prepare() spells it: channeled
    // and auto-repeat spells are not "in progress" for this purpose.
    if (bot->IsNonMeleeSpellCasted(false, true, true) ||
        TortoiseBots::RemoteCommander::RitualHolds().Holds(bot->GetObjectGuid().GetRawValue()))
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_SPELL_IN_PROGRESS;
        }

        return false;
    }

    // Spell::prepare() also refuses a cast the caster cannot pay for, but only
    // because it is the one place that fills in m_powerCost; this probe never
    // runs prepare(), so CheckCast(true) approved power-starved casts.
    Item* castItem = itemTarget ? itemTarget : aiObjectContext->GetValue<Item*>("item for spell", spellid)->Get();
    // An item-targeted spell (weapon imbues) takes "item for spell" as its
    // TARGET, not as the item that casts it: as a cast item the core found no
    // item target (SPELL_FAILED_ITEM_GONE) and charged no mana. Same split in
    // CastSpell below.
    Item* spellItemTarget = nullptr;
    if (!itemTarget && (spellInfo->Targets & TARGET_FLAG_ITEM))
    {
        spellItemTarget = castItem;
        castItem = nullptr;
    }
    SpellCastResult powerResult = CheckSpellPower(spellInfo, bot, castItem);
    if (powerResult != SPELL_CAST_OK)
    {
        if (checkResult)
        {
            *checkResult = powerResult;
        }

        return false;
    }

    // already active next melee swing spell
    if (IsNextMeleeSwingSpell(spellInfo))
    {
        Spell* autorepeatSpell = bot->GetCurrentSpell(CURRENT_MELEE_SPELL);
        if (autorepeatSpell)
        {
            if (autorepeatSpell->m_spellInfo->Id == spellInfo->Id)
            {
                if (checkResult)
                {
                    *checkResult = SPELL_FAILED_SPELL_IN_PROGRESS;
                }

                return false;
            }
        }
    }

	if (!itemTarget)
	{
        // Consider neutral spells (spells that are neither positive or negative (e.g. feign death, hunter traps, ...)
        const std::list<uint32> neutralSpells = { 1499, 5384, 13795, 13809, 13813, 14302, 14303, 14304, 14305, 14310, 14311, 14316, 14317 };
        const bool neutralSpell = std::find(neutralSpells.begin(), neutralSpells.end(), spellid) != neutralSpells.end();
        if(!neutralSpell)
        {
            const bool positiveSpell = IsPositiveSpell(spellInfo);
            if (positiveSpell && sServerFacade.IsHostileTo(bot, target))
            {
                if (checkResult)
                {
                    *checkResult = SPELL_FAILED_TARGET_ENEMY;
                }

                return false;
            }

            if (!positiveSpell && sServerFacade.IsFriendlyTo(bot, target))
            {
                if (checkResult)
                {
                    *checkResult = SPELL_FAILED_TARGET_FRIENDLY;
                }

                return false;
            }
        }

        bool damage = false;
        for (int32 i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; i++)
        {
            // direct damage
            if (spellInfo->Effect[(SpellEffectIndex)i] == SPELL_EFFECT_SCHOOL_DAMAGE)
            {
                damage = true;
                break;
            }
            // periodic damage
            if (spellInfo->Effect[(SpellEffectIndex)i] == SPELL_EFFECT_APPLY_AURA && spellInfo->EffectBasePoints[i] >= 0)
            {
                if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE)
                {
                    damage = true;
                    break;
                }
            }
        }

        if (damage)
        {
            // Damage spells skip the per-effect check below (mechanic/effect immunities do not stop damage),
            // but a school or damage immunity (fire elementals, Ice Block) makes the cast do nothing: report it
            // as not castable so the rotation falls through to the next row (e.g. Frostbolt).
            if (target->IsImmuneToDamage(spellInfo->GetSpellSchoolMask(), spellInfo) ||
                target->IsImmuneToSpell(spellInfo, bot == target))
            {
                if (checkResult)
                {
                    *checkResult = SPELL_FAILED_IMMUNE;
                }

                return false;
            }
        }
        else
        {
            for (int32 i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; i++)
            {
                bool immune = target->IsImmuneToSpellEffect(spellInfo, (SpellEffectIndex)i, false);
                if (immune)
                {
                    if (checkResult)
                    {
                        *checkResult = SPELL_FAILED_IMMUNE;
                    }

                    return false;
                }
            }
        }

        // Issue #903 (donor mod-playerbots @ 79bd4281,
        // src/Bot/PlayerbotAI.cpp:3459-3466): a cast-time cast started while
        // the bot is still moving is cancelled on the first movement packet,
        // so it only burns the GCD and logs an interrupt. Refuse it here so
        // the trigger falls through to instant casts instead of firing the
        // doomed cast. Same cast-time shape as CastSpell below; a spline
        // still in flight counts as moving (GO-overload clause).
        {
            uint32 castTime = GetSpellCastTime(spellInfo, bot);
            bool spellNeedsStandStill = castTime > 0 ||
                (IsChanneledSpell(spellInfo) && GetSpellDuration(spellInfo) > 0);
            if (ai::CastStartBlockedByMove(sServerFacade.isMoving(bot),
                !bot->movespline->Finalized(), spellNeedsStandStill))
            {
                if (checkResult)
                {
                    *checkResult = SPELL_FAILED_MOVING;
                }

                return false;
            }
        }

        if (!ignoreRange && bot != target && sServerFacade.getDistance2d(bot, target) > sPlayerbotAIConfig.sightDistance)
        {
            if (checkResult)
            {
                *checkResult = SPELL_FAILED_OUT_OF_RANGE;
            }

            return false;
        }
	}

	ObjectGuid oldSel = bot->GetSelectionGuid();
	//bot->SetSelectionGuid(target->getObjectGuid());
	Spell *spell = new Spell(bot, spellInfo, false);

    spell->m_targets.setUnitTarget(target);
    spell->SetCastItem(castItem);
    spell->m_targets.setItemTarget(spell->m_targets.getItemTarget());
    spell->m_targets.setItemTarget(spellItemTarget);

    SpellCastResult result = spell->CheckCast(true);
    delete spell;
	//if (oldSel)
	//	bot->SetSelectionGuid(oldSel);

    if (checkResult)
    {
        *checkResult = result;
    }

    switch (result)
    {
        case SPELL_FAILED_NOT_INFRONT:
        case SPELL_FAILED_NOT_STANDING:
        case SPELL_FAILED_UNIT_NOT_INFRONT:
        case SPELL_FAILED_MOVING:
        case SPELL_FAILED_TRY_AGAIN:
        case SPELL_CAST_OK:
            return true;
        case SPELL_FAILED_OUT_OF_RANGE:
        case SPELL_FAILED_LINE_OF_SIGHT:
            return ignoreRange;
        case SPELL_FAILED_AFFECTING_COMBAT:
            return ignoreInCombat;
        case SPELL_FAILED_NOT_MOUNTED:
            return ignoreMount;
        default:
            return false;
    }
}

bool PlayerbotAI::CanCastSpell(uint32 spellid, GameObject* goTarget, uint8 effectMask, bool checkHasSpell, bool ignoreRange, bool ignoreInCombat, bool ignoreMount, SpellCastResult* checkResult)
{
    if (!spellid)
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_KNOWN;
        }

        return false;
    }

    Pet* pet = bot->GetPet();
    if (pet && pet->HasSpell(spellid) && !pet->HasSpellCooldown(spellid))
    {
        if (checkResult)
        {
            *checkResult = SPELL_CAST_OK;
        }

        return true;
    }

    if (bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        // Spells that can be casted while out of control
        const std::list<uint32> ignoreOutOfControllSpells = { 642, 1020, 1499, 1953, 7744, 11958, 13795, 13809, 13813, 14302, 14303, 14304, 14305, 14310, 14311, 14316, 14317 };
        if (std::find(ignoreOutOfControllSpells.begin(), ignoreOutOfControllSpells.end(), spellid) == ignoreOutOfControllSpells.end())
        {
            if (checkResult)
            {
                *checkResult = SPELL_FAILED_NOT_IN_CONTROL;
            }

            return false;
        }
    }

    if (checkHasSpell && !HasSpell(spellid))
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_KNOWN;
        }

        return false;
    }

    if (!sServerFacade.IsSpellReady(bot, spellid))
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_READY;
        }

        return false;
    }

    SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(spellid);
    if (!spellInfo)
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_KNOWN;
        }

        return false;
    }

    bool damage = false;
    for (int32 i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; i++)
    {
        if (spellInfo->Effect[(SpellEffectIndex)i] == SPELL_EFFECT_SCHOOL_DAMAGE)
        {
            damage = true;
            break;
        }
    }

    if (sServerFacade.getDistance2d(bot, goTarget) > sPlayerbotAIConfig.sightDistance)
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_OUT_OF_RANGE;
        }

        return false;
    }

    //ObjectGuid oldSel = bot->GetSelectionGuid();
    bot->SetSelectionGuid(goTarget->getObjectGuid());
    Spell* spell = new Spell(bot, spellInfo, false);

    spell->m_targets.setGOTarget(goTarget);
    spell->SetCastItem(aiObjectContext->GetValue<Item*>("item for spell", spellid)->Get());
    spell->m_targets.setItemTarget(spell->m_targets.getItemTarget());

    SpellCastResult result = spell->CheckCast(true);
    delete spell;
    //if (oldSel)
    //    bot->SetSelectionGuid(oldSel);

    if (checkResult)
    {
        *checkResult = result;
    }

    switch (result)
    {
        case SPELL_FAILED_NOT_INFRONT:
        case SPELL_FAILED_NOT_STANDING:
        case SPELL_FAILED_UNIT_NOT_INFRONT:
        case SPELL_FAILED_MOVING:
        case SPELL_FAILED_TRY_AGAIN:
        case SPELL_CAST_OK:
            return true;
        case SPELL_FAILED_OUT_OF_RANGE:
            return ignoreRange;
        case SPELL_FAILED_AFFECTING_COMBAT:
            return ignoreInCombat;
        case SPELL_FAILED_NOT_MOUNTED:
            return ignoreMount;
        default:
            return false;
    }
}

bool PlayerbotAI::CanCastSpell(uint32 spellid, float x, float y, float z, uint8 effectMask, bool checkHasSpell, Item* itemTarget, bool ignoreRange, bool ignoreInCombat, bool ignoreMount, SpellCastResult* checkResult)
{
    if (!spellid)
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_KNOWN;
        }

        return false;
    }

    Pet* pet = bot->GetPet();
    if (pet && pet->HasSpell(spellid) && !pet->HasSpellCooldown(spellid))
    {
        if (checkResult)
        {
            *checkResult = SPELL_CAST_OK;
        }

        return true;
    }

    if (bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        // Spells that can be casted while out of control
        const std::list<uint32> ignoreOutOfControllSpells = { 642, 1020, 1499, 1953, 7744, 11958, 13795, 13809, 13813, 14302, 14303, 14304, 14305, 14310, 14311, 14316, 14317 };
        if (std::find(ignoreOutOfControllSpells.begin(), ignoreOutOfControllSpells.end(), spellid) == ignoreOutOfControllSpells.end())
        {
            if (checkResult)
            {
                *checkResult = SPELL_FAILED_NOT_IN_CONTROL;
            }

            return false;
        }
    }

    if (checkHasSpell && !HasSpell(spellid))
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_KNOWN;
        }

        return false;
    }

    SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(spellid);
    if (!spellInfo)
    {
        if (checkResult)
        {
            *checkResult = SPELL_FAILED_NOT_KNOWN;
        }

        return false;
    }

    if (!itemTarget)
    {
        if (bot->GetDistance(x, y, z) > sPlayerbotAIConfig.sightDistance)
        {
            if (checkResult)
            {
                *checkResult = SPELL_FAILED_OUT_OF_RANGE;
            }

            return false;
        }
    }

    Spell* spell = new Spell(bot, spellInfo, false);

    spell->m_targets.setDestination(x, y, z);
    spell->SetCastItem(itemTarget ? itemTarget : aiObjectContext->GetValue<Item*>("item for spell", spellid)->Get());
    spell->m_targets.setItemTarget(spell->m_targets.getItemTarget());

    SpellCastResult result = spell->CheckCast(true);
    delete spell;

    if (checkResult)
    {
        *checkResult = result;
    }

    switch (result)
    {
        case SPELL_FAILED_NOT_INFRONT:
        case SPELL_FAILED_NOT_STANDING:
        case SPELL_FAILED_UNIT_NOT_INFRONT:
        case SPELL_FAILED_MOVING:
        case SPELL_FAILED_TRY_AGAIN:
        case SPELL_CAST_OK:
            return true;
        case SPELL_FAILED_OUT_OF_RANGE:
            return ignoreRange;
        case SPELL_FAILED_AFFECTING_COMBAT:
            return ignoreInCombat;
        case SPELL_FAILED_NOT_MOUNTED:
            return ignoreMount;
        default:
            return false;
    }
}

uint8 PlayerbotAI::GetHealthPercent(const Unit& target) const
{
   return (static_cast<float>(target.GetHealth()) / target.GetMaxHealth()) * 100;
}

uint8 PlayerbotAI::GetHealthPercent() const
{
   return GetHealthPercent(*bot);
}

uint8 PlayerbotAI::GetManaPercent(const Unit& target) const
{
   return (static_cast<float>(target.GetPower(POWER_MANA)) / target.GetMaxPower(POWER_MANA)) * 100;
}

uint8 PlayerbotAI::GetManaPercent() const
{
   return GetManaPercent(*bot);
}

bool PlayerbotAI::CastSpell(std::string name, Unit* target, Item* itemTarget, bool waitForSpell, uint32* outSpellDuration)
{
    bool result = CastSpell(aiObjectContext->GetValue<uint32>("spell id", name)->Get(), target, itemTarget, waitForSpell, outSpellDuration);
    if (result)
    {
        aiObjectContext->GetValue<time_t>("last spell cast time", name)->Set(time(0));
    }

    return result;
}

bool PlayerbotAI::CastSpell(uint32 spellId, Unit* target, Item* itemTarget, bool waitForSpell, uint32* outSpellDuration)
{
    if (!spellId)
        return false;

    if (!target)
        target = bot;

    // Catches both "no target given" (defaulted to self above) and "current target"
    // having already resolved to the bot's own Unit* before CastSpell was ever called -
    // the latter sailed past the old !target-only check with a perfectly valid non-null
    // pointer that just happened to be self, so a harmful spell like Fireball could still
    // land on the caster.
    if (target == bot)
    {
        SpellEntry const* fallbackSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (fallbackSpellInfo && CheckSpellTargetAlignment(fallbackSpellInfo, bot) != SPELL_CAST_OK)
        {
            sLog.outDetail("PlayerbotAI::CastSpell: %s target resolved to self for harmful spell %u - refusing to self-cast", bot->GetName(), spellId);
            botdiag::BotActionLog::Write(this, "CAST_GATE", "spell=%u targetGuid=0x%llx reason=self-harmful-refused", spellId, (unsigned long long)target->getObjectGuid().GetRawValue());
            return false;
        }
    }

    Pet* pet = bot->GetPet();
    if (pet && pet->HasSpell(spellId))
    {
        // M1 trail: pet-owned ability leaves the unit-cast path here; the
        // pet overload owns the actual attempt/result from this point.
        botdiag::BotActionLog::Write(this, "CAST_GATE", "spell=%u targetGuid=0x%llx reason=pet-redirect", spellId, (unsigned long long)target->getObjectGuid().GetRawValue());
        return CastPetSpell(spellId, target);
    }

    // Spell::prepare() refuses a second non-melee cast while one is still running, and that is
    // where SPELL_FAILED_SPELL_IN_PROGRESS comes from. CanCastSpell() mirrors the gate for every
    // caller that asks before casting (CastSpellAction::isPossible and the cast triggers), but the
    // loot chain calls this overload directly: OpenLootAction::DoLoot starts Skinning / Herb
    // Gathering / Mining and the opening spells from its own action. While the 2s gather cast ran,
    // "can loot" stayed true - the loot target is still valid until the cast completes and the
    // corpse is skinned - so the chain re-issued the cast every tick and the server refused each
    // one. Live: 545 of the 750 SPELL_IN_PROGRESS refusals in a 47-minute window were Skinning,
    // plus the herb/ore/opening spells of the same path. Same expression as prepare(): a channeled
    // or auto-repeat spell does not count as "in progress".
    if (bot->IsNonMeleeSpellCasted(false, true, true) ||
        TortoiseBots::RemoteCommander::RitualHolds().Holds(bot->GetObjectGuid().GetRawValue()))
    {
        botdiag::BotActionLog::Write(this, "CAST_GATE", "spell=%u targetGuid=0x%llx reason=cast-in-progress", spellId, (unsigned long long)target->getObjectGuid().GetRawValue());
        NoteCastCheckFail(SPELL_FAILED_SPELL_IN_PROGRESS);
        return false;
    }

    aiObjectContext->GetValue<LastMovement&>("last movement")->Get().Set(NULL);
    aiObjectContext->GetValue<time_t>("stay time")->Set(0);

    MotionMaster &mm = *bot->GetMotionMaster();

    if (bot->IsFlying() || bot->IsTaxiFlying())
    {
        botdiag::BotActionLog::Write(this, "CAST_GATE", "spell=%u targetGuid=0x%llx reason=flying", spellId, (unsigned long long)target->getObjectGuid().GetRawValue());
        return false;
    }

	//bot->ClearUnitState(UNIT_STAT_CHASE);
	//bot->ClearUnitState(UNIT_STAT_FOLLOW);

	bool failWithDelay = false;
    if (bot->GetStandState() != UNIT_STAND_STATE_STAND)
    {
        bot->SetStandState(UNIT_STAND_STATE_STAND);
        failWithDelay = true;
    }

	ObjectGuid oldSel = bot->GetSelectionGuid();
	bot->SetSelectionGuid(target->getObjectGuid());

    WorldObject* faceTo = target;
    // A self-cast (Evocation, Combustion, Ice Block, ...) needs no facing; a bot is never "in
    // front" of itself, so the check below deferred every self-cast while the bot moved.
    if (target != bot && !sServerFacade.isInFront(bot, faceTo, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))
    {
        sServerFacade.SetFacingTo(bot, faceTo);
        // ServerFacade::SetFacingTo only turns instantly (Unit::SetOrientation) while the bot
        // is stationary; while moving it's a spline-based turn that takes real time to finish,
        // which is what this GCD-delay-and-retry was guarding against. For a stationary bot
        // the facing was just corrected synchronously above, so proceed with this same cast
        // attempt instead of deferring - deferring here against a moving target (wandering
        // wildlife, etc.) meant the target had already drifted again by the retry, so the bot
        // never actually caught up and stayed stuck failing to cast indefinitely.
        if (!HasRealPlayerMaster() && !bot->IsStopped())
            failWithDelay = true;
    }

    if (failWithDelay)
    {
        if(waitForSpell)
        {
            SetAIInternalUpdateDelay(sPlayerbotAIConfig.globalCoolDown);
        }

        if(outSpellDuration)
        {
            *outSpellDuration = sPlayerbotAIConfig.globalCoolDown;
        }

        botdiag::BotActionLog::Write(this, "CAST_GATE", "spell=%u targetGuid=0x%llx reason=stand-or-facing-delay stand=%u stopped=%u realMaster=%u motion=%u", spellId, (unsigned long long)target->getObjectGuid().GetRawValue(),
            (uint32)bot->GetStandState(), (uint32)bot->IsStopped(), (uint32)HasRealPlayerMaster(), (uint32)mm.GetCurrentMovementGeneratorType());
        return false;
    }

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
    Spell *spell = new Spell(bot, pSpellInfo, false);

    SpellCastTargets targets;
    if ((pSpellInfo->Targets & TARGET_FLAG_ITEM) || spellId == 1804)
    {
        // Crash guard (core SIGSEGV in Spell::cast via SpellEvent::Execute):
        // the core keeps a raw m_CastItem pointer with no lifetime check. If
        // the item left the bot's inventory between pick and cast (sold,
        // consumed, moved by another tick), the core dereferences a dangling
        // pointer. Validate ownership here; on mismatch cast without the item
        // instead of crashing. The core has no such check, so this stays.
        Item* castItem = itemTarget ? itemTarget : aiObjectContext->GetValue<Item*>("item for spell", spellId)->Get();
        if (castItem && bot->GetItemByGuid(castItem->GetObjectGuid()) != castItem)
            castItem = nullptr;
        // "item for spell" is the spell's item target (weapon imbues), not
        // its cast item: as a cast item the imbue failed with ITEM_GONE every
        // time. An explicit itemTarget keeps the cast-item meaning (custom
        // casts from bag items).
        if (!itemTarget && (pSpellInfo->Targets & TARGET_FLAG_ITEM))
        {
            targets.setItemTarget(castItem);
            castItem = nullptr;
        }
        spell->SetCastItem(castItem);
        targets.setItemTarget(spell->m_targets.getItemTarget());

        if (bot->GetTradeData())
        {
            bot->GetTradeData()->SetSpell(spellId);
			delete spell;
            return true;
        }
    }
    else if (pSpellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
    {
        WorldLocation aoe = aiObjectContext->GetValue<WorldLocation>("aoe position")->Get();
        if (aoe.x != 0)
            targets.setDestination(aoe.x, aoe.y, aoe.z);
        else if (target && target->getObjectGuid() != bot->getObjectGuid())
            targets.setDestination(target->getPositionX(), target->getPositionY(), target->getPositionZ());
    }
    else if (pSpellInfo->Targets & TARGET_FLAG_SOURCE_LOCATION)
    {
        targets.setDestination(bot->getPositionX(), bot->getPositionY(), bot->getPositionZ());
    }
    else
    {
        targets.setUnitTarget(target);
    }

    if (spellId == 1953) // simulate blink coordinates
    {
        float angle = bot->getOrientation();
        float distance = 20.0f;
        float fx = bot->getPositionX() + cos(angle) * distance;
        float fy = bot->getPositionY() + sin(angle) * distance;
        float fz = bot->getPositionZ();

        bot->UpdateAllowedPositionZ(fx, fy, fz);
        targets.setDestination(fx, fy, fz);
    }

    if (pSpellInfo->Effect[0] == SPELL_EFFECT_OPEN_LOCK ||
        pSpellInfo->Effect[0] == SPELL_EFFECT_SKINNING)
    {
        LootObject loot = *aiObjectContext->GetValue<LootObject>("loot target");
        GameObject* go = GetGameObject(loot.guid);
        if (go && sServerFacade.isSpawned(go))
        {
            targets.setGOTarget(go);
            faceTo = go;
        }
        else
        {
            Unit* creature = GetUnit(loot.guid);
            if (creature)
            {
                targets.setUnitTarget(creature);
                faceTo = creature;
            }
        }
    }

    // Fail the cast if the bot is moving and the spell is a casting/channeled spell
    // (#903: CastStartBlockedByMove - the probe above already refused this
    // case, this is the direct-call backstop for callers that skip it).
    if (ai::CastStartBlockedByMove(sServerFacade.isMoving(bot), false,
        (GetSpellCastTime(pSpellInfo, bot, spell) > 0) || (IsChanneledSpell(pSpellInfo) && (GetSpellDuration(pSpellInfo) > 0))))
    {
        // always fail when jumping
        if (IsJumping() || bot->IsFalling())
        {
            spell->cancel();
            botdiag::BotActionLog::Write(this, "CAST_GATE", "spell=%u targetGuid=0x%llx reason=moving-jump-fall", spellId, (unsigned long long)target->getObjectGuid().GetRawValue());
            NoteCastCheckFail(SPELL_FAILED_MOVING);
            return false;
        }

        StopMoving();

        // fail if not with real player to avoid movement glitches
        if (!HasActivePlayerMaster())
        {
            if (waitForSpell)
            {
                SetAIInternalUpdateDelay(sPlayerbotAIConfig.globalCoolDown);
            }

            spell->cancel();
            botdiag::BotActionLog::Write(this, "CAST_GATE", "spell=%u targetGuid=0x%llx reason=moving-no-master", spellId, (unsigned long long)target->getObjectGuid().GetRawValue());
            NoteCastCheckFail(SPELL_FAILED_MOVING);
            return false;
        }
    }

    for (uint32 j = 0; j < MAX_EFFECT_INDEX; ++j)
    {
        uint8 slot = 0;
        switch (pSpellInfo->Effect[j])
        {
        case SPELL_EFFECT_SUMMON_OBJECT_SLOT1: slot = 0; break;
        case SPELL_EFFECT_SUMMON_OBJECT_SLOT2: slot = 1; break;
        case SPELL_EFFECT_SUMMON_OBJECT_SLOT3: slot = 2; break;
        case SPELL_EFFECT_SUMMON_OBJECT_SLOT4: slot = 3; break;
        default: continue;
        }

        if (ObjectGuid guid = bot->m_ObjectSlotGuid[slot])
        {
            if (GameObject* obj = bot ? bot->GetMap()->GetGameObject(guid) : nullptr)
            {
                //Object is not mine because I created an object with same guid on different map.
                //Make object mine, remove it from my list and give it back to the original owner.
                if (obj->GetOwnerGuid() != bot->getObjectGuid())
                {
                    ObjectGuid ownerGuid = obj->GetOwnerGuid();
                    obj->SetOwnerGuid(bot->getObjectGuid());
                    obj->SetLootState(GO_JUST_DEACTIVATED);

                    bot->RemoveGameObject(obj, false);
                    bot->m_ObjectSlotGuid[slot].Clear();

                    obj->SetOwnerGuid(ownerGuid);
                }
            }
        }
    }


    // M1 trail: attempt plus native preparation result. PREPARE ok means the
    // core accepted the cast, not that effects landed — no module-visible
    // completion signal exists yet (see M1 findings in docs/migration).
    botdiag::BotActionLog::LogCastStart(this, spellId, target->getObjectGuid(), (uint32)GetSpellCastTime(pSpellInfo, bot, spell));
    SpellCastResult spellSuccess = spell->prepare(targets);
    botdiag::BotActionLog::LogCastResult(this, spellId, spellSuccess, "PREPARE");

    if (pSpellInfo->Effect[0] == SPELL_EFFECT_OPEN_LOCK ||
        pSpellInfo->Effect[0] == SPELL_EFFECT_SKINNING)
    {
        if (!spell->m_targets.getItemTarget())
        {
            LootObject loot = *aiObjectContext->GetValue<LootObject>("loot target");
            if (!loot.IsLootPossible(bot))
            {
                spell->cancel();
                botdiag::BotActionLog::Write(this, "CAST_GATE", "spell=%u targetGuid=0x%llx reason=loot-impossible", spellId, (unsigned long long)target->getObjectGuid().GetRawValue());
                //delete spell;
                return false;
            }
        }
    }

    if (spellSuccess != SPELL_CAST_OK)
    {
        // The core's own refusal, kept for the caller (the remote commander's open order).
        NoteCastCheckFail(spellSuccess);
        return false;
    }
    NoteSpellGlobalCooldown(pSpellInfo);

    PlayAttackEmote(6);

    if(waitForSpell)
    {
        WaitForSpellCast(spell);
    }

    if(outSpellDuration)
    {
        *outSpellDuration = GetSpellCastDuration(spell);
    }

    if (spell->GetCastTime() || (IsChanneledSpell(pSpellInfo) && GetSpellDuration(pSpellInfo) > 0))
        aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get().Set(spellId, target->getObjectGuid(), time(0));

    aiObjectContext->GetValue<ai::PositionMap&>("position")->Get()["random"].Reset();

    if (oldSel)
        bot->SetSelectionGuid(oldSel);

    if (HasStrategy("debug spell", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "Casting " <<ChatHelper::formatSpell(pSpellInfo);
        TellPlayerNoFacing(GetMaster() ? GetMaster() : bot, out);
    }

    // Local raid diagnostics: one bot_events.csv row per
    // successful cast by a grouped warrior tank in combat, so live raid tests
    // show what the tanks actually spend rage on.
    if (bot->GetClass() == CLASS_WARRIOR && bot->GetGroup() && IsTank(bot) && bot->IsInCombat())
    {
        Aura* sunder = target ? GetAura("sunder armor", target) : nullptr;
        std::ostringstream info;
        info << "rage=" << bot->GetPower(POWER_RAGE) / 10
             << " sunder=" << (sunder ? sunder->GetStackAmount() : 0)
             << " target=" << (target ? target->GetEntry() : 0)
             << " targetsMe=" << (target && target->GetVictim() == bot ? 1 : 0);
        sPlayerbotAIConfig.logEvent(this, "TankCast", pSpellInfo->SpellName[0], info.str());
    }

    return true;
}

bool PlayerbotAI::CastSpell(uint32 spellId, GameObject* goTarget, Item* itemTarget, bool waitForSpell, uint32* outSpellDuration)
{
    if (!spellId)
        return false;

    aiObjectContext->GetValue<LastMovement&>("last movement")->Get().Set(NULL);
    aiObjectContext->GetValue<time_t>("stay time")->Set(0);

    MotionMaster& mm = *bot->GetMotionMaster();

    if (bot->IsFlying() || bot->IsTaxiFlying())
        return false;

    //bot->ClearUnitState(UNIT_STAT_CHASE);
    //bot->ClearUnitState(UNIT_STAT_FOLLOW);

    bool failWithDelay = false;
    if (bot->GetStandState() != UNIT_STAND_STATE_STAND)
    {
        bot->SetStandState(UNIT_STAND_STATE_STAND);
        failWithDelay = true;
    }

    WorldObject* faceTo = goTarget;
    if (!sServerFacade.isInFront(bot, faceTo, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))
    {
        sServerFacade.SetFacingTo(bot, faceTo);
        //failWithDelay = true;
    }

    if (failWithDelay)
    {
        if (waitForSpell)
        {
            SetAIInternalUpdateDelay(sPlayerbotAIConfig.globalCoolDown);
        }

        if (outSpellDuration)
        {
            *outSpellDuration = sPlayerbotAIConfig.globalCoolDown;
        }

        return false;
    }

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
    Spell* spell = new Spell(bot, pSpellInfo, false);

    SpellCastTargets targets;
    if (pSpellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
    {
        WorldLocation aoe = aiObjectContext->GetValue<WorldLocation>("aoe position")->Get();
        if (aoe.x != 0)
            targets.setDestination(aoe.x, aoe.y, aoe.z);
        else if (goTarget && goTarget->getObjectGuid() != bot->getObjectGuid())
            targets.setDestination(goTarget->getPositionX(), goTarget->getPositionY(), goTarget->getPositionZ());
    }
    else if (pSpellInfo->Targets & TARGET_FLAG_SOURCE_LOCATION)
    {
        targets.setDestination(bot->getPositionX(), bot->getPositionY(), bot->getPositionZ());
    }

    targets.setGOTarget(goTarget);
    spell->SetCastItem(itemTarget ? itemTarget : aiObjectContext->GetValue<Item*>("item for spell", spellId)->Get());
    targets.setItemTarget(spell->m_targets.getItemTarget());

    if (goTarget->GetGoType() == GAMEOBJECT_TYPE_CHEST)
    {
        auto context = GetAiObjectContext();
        AI_VALUE(LootObjectStack*, "available loot")->Add(goTarget->getObjectGuid());
    }

    if (spellId == 1953) // simulate blink coordinates
    {
        float angle = bot->getOrientation();
        float distance = 20.0f;
        float fx = bot->getPositionX() + cos(angle) * distance;
        float fy = bot->getPositionY() + sin(angle) * distance;
        float fz = bot->getPositionZ();

        bot->UpdateAllowedPositionZ(fx, fy, fz);
        targets.setDestination(fx, fy, fz);
    }

    // Fail the cast if the bot is moving and the spell is a casting/channeled spell.
    // A server spline still in flight counts as moving although the movement flags
    // are clear (MotionMaster::Clear leaves it running): the Opening cast of a
    // chest broke after 0.2 s on the bot's own last 0.9 yd.
    if ((sServerFacade.isMoving(bot) || !bot->movespline->Finalized()) && ((GetSpellCastTime(pSpellInfo, bot, spell) > 0) || (IsChanneledSpell(pSpellInfo) && (GetSpellDuration(pSpellInfo) > 0))))
    {
        // always fail when jumping
        if (IsJumping() || bot->IsFalling())
        {
            spell->cancel();
            return false;
        }

        StopMoving();

        // fail if not with real player to avoid movement glitches
        if (!HasActivePlayerMaster())
        {
            if (waitForSpell)
            {
                SetAIInternalUpdateDelay(sPlayerbotAIConfig.globalCoolDown);
            }

            spell->cancel();
            return false;
        }
    }

    botdiag::BotActionLog::LogCastStart(this, spellId, goTarget->getObjectGuid(), (uint32)GetSpellCastTime(pSpellInfo, bot, spell));
    SpellCastResult spellSuccess = spell->prepare(targets);
    botdiag::BotActionLog::LogCastResult(this, spellId, spellSuccess, "PREPARE-go");
    if (spellSuccess != SPELL_CAST_OK)
        return false;
    NoteSpellGlobalCooldown(pSpellInfo);

    PlayAttackEmote(6);

    if (waitForSpell)
    {
        WaitForSpellCast(spell);
    }

    if (outSpellDuration)
    {
        *outSpellDuration = GetSpellCastDuration(spell);
    }

    if (spell->GetCastTime() || (IsChanneledSpell(pSpellInfo) && GetSpellDuration(pSpellInfo) > 0))
        aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get().Set(spellId, goTarget->getObjectGuid(), time(0));

    aiObjectContext->GetValue<ai::PositionMap&>("position")->Get()["random"].Reset();

    if (HasStrategy("debug spell", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "Casting " << ChatHelper::formatSpell(pSpellInfo);
        TellPlayerNoFacing(GetMaster() ? GetMaster() : bot, out);
    }


    return true;
}

bool PlayerbotAI::CastSpell(uint32 spellId, float x, float y, float z, Item* itemTarget, bool waitForSpell, uint32* outSpellDuration)
{
    if (!spellId)
        return false;

    Pet* pet = bot->GetPet();
    if (pet && pet->HasSpell(spellId))
    {
        return CastPetSpell(spellId, nullptr);
    }

    aiObjectContext->GetValue<LastMovement&>("last movement")->Get().Set(NULL);
    aiObjectContext->GetValue<time_t>("stay time")->Set(0);

    MotionMaster& mm = *bot->GetMotionMaster();

    if (bot->IsFlying() || bot->IsTaxiFlying())
        return false;

    bot->ClearUnitState(UNIT_STAT_CHASE);
    bot->ClearUnitState(UNIT_STAT_FOLLOW);

    bool failWithDelay = false;
    if (bot->GetStandState() != UNIT_STAND_STATE_STAND)
    {
        bot->SetStandState(UNIT_STAND_STATE_STAND);
        failWithDelay = true;
    }

    ObjectGuid oldSel = bot->GetSelectionGuid();

    if (!sServerFacade.isMoving(bot)) sServerFacade.SetFacingTo(bot, std::atan2(y - bot->GetPositionY(), x - bot->GetPositionX()));

    if (failWithDelay)
    {
        if (waitForSpell)
        {
            SetAIInternalUpdateDelay(sPlayerbotAIConfig.globalCoolDown);
        }

        if (outSpellDuration)
        {
            *outSpellDuration = sPlayerbotAIConfig.globalCoolDown;
        }

        return false;
    }

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
    Spell* spell = new Spell(bot, pSpellInfo, false);

    SpellCastTargets targets;
    if ((pSpellInfo->Targets & TARGET_FLAG_ITEM) || spellId == 1804)
    {
        spell->SetCastItem(itemTarget ? itemTarget : aiObjectContext->GetValue<Item*>("item for spell", spellId)->Get());
        targets.setItemTarget(spell->m_targets.getItemTarget());

        if (bot->GetTradeData())
        {
            bot->GetTradeData()->SetSpell(spellId);
            delete spell;
            return true;
        }
    }
    else if (pSpellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
    {
        WorldLocation aoe = aiObjectContext->GetValue<WorldLocation>("aoe position")->Get();
        targets.setDestination(x, y, z);
    }
    else if (pSpellInfo->Targets & TARGET_FLAG_SOURCE_LOCATION)
    {
        targets.setDestination(bot->getPositionX(), bot->getPositionY(), bot->getPositionZ());
    }
    else
    {
        return false;
    }

    if (pSpellInfo->Effect[0] == SPELL_EFFECT_OPEN_LOCK ||
        pSpellInfo->Effect[0] == SPELL_EFFECT_SKINNING)
    {
        return false;
    }

    // Fail the cast if the bot is moving and the spell is a casting/channeled spell
    if (sServerFacade.isMoving(bot) && ((GetSpellCastTime(pSpellInfo, bot, spell) > 0) || (IsChanneledSpell(pSpellInfo) && (GetSpellDuration(pSpellInfo) > 0))))
    {
        // always fail when jumping
        if (IsJumping() || bot->IsFalling())
        {
            spell->cancel();
            return false;
        }

        StopMoving();

        // fail if not with real player to avoid movement glitches
        if (!HasActivePlayerMaster())
        {
            if (waitForSpell)
            {
                SetAIInternalUpdateDelay(sPlayerbotAIConfig.globalCoolDown);
            }

            spell->cancel();
            return false;
        }
    }
    // M3 outcome contract: a rejected ground cast is a failed action, matching
    // the unit/GO overloads. Callers (CastCustomSpellAction) fall through to
    // their fallbacks instead of announcing a phantom cast.
    botdiag::BotActionLog::LogCastStart(this, spellId, bot->getObjectGuid(), (uint32)GetSpellCastTime(pSpellInfo, bot, spell));
    SpellCastResult coordPrep = spell->prepare(targets);
    botdiag::BotActionLog::LogCastResult(this, spellId, coordPrep, "PREPARE-coord");
    if (coordPrep != SPELL_CAST_OK)
        return false;
    NoteSpellGlobalCooldown(pSpellInfo);

    if (pSpellInfo->Effect[0] == SPELL_EFFECT_OPEN_LOCK ||
        pSpellInfo->Effect[0] == SPELL_EFFECT_SKINNING)
    {
        if (!spell->m_targets.getItemTarget())
        {
            LootObject loot = *aiObjectContext->GetValue<LootObject>("loot target");
            if (!loot.IsLootPossible(bot))
            {
                spell->cancel();
                //delete spell;
                return false;
            }
        }
    }

    if (waitForSpell)
    {
        WaitForSpellCast(spell);
    }

    if (outSpellDuration)
    {
        *outSpellDuration = GetSpellCastDuration(spell);
    }

    aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get().Set(spellId, bot->getObjectGuid(), time(0));
    aiObjectContext->GetValue<ai::PositionMap&>("position")->Get()["random"].Reset();

    if (oldSel)
        bot->SetSelectionGuid(oldSel);

    if (HasStrategy("debug spell", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "Casting " << ChatHelper::formatSpell(pSpellInfo);
        TellPlayerNoFacing(GetMaster() ? GetMaster() : bot, out);
    }


    return true;
}

bool PlayerbotAI::CastPetSpell(uint32 spellId, Unit* target)
{
    Pet* pet = bot->GetPet();
    if (pet && spellId && pet->HasSpell(spellId))
    {
        auto IsAutocastActive = [&pet, &spellId]() -> bool
        {
            for (AutoSpellList::iterator i = pet->m_autospells.begin(); i != pet->m_autospells.end(); ++i)
            {
                if (*i == spellId)
                {
                    return true;
                }
            }

            return false;
        };

        // Send pet spell action packet
        uint8 flag = ACT_PASSIVE;
        if (IsAutocastable(spellId))
        {
            flag = IsAutocastActive() ? ACT_ENABLED : ACT_DISABLED;
        }

        uint32 command = (flag << 24) | spellId;

        WorldPacket data(CMSG_PET_ACTION);
        data << pet->getObjectGuid();
        data << command;
        data << (target ? target->getObjectGuid() : ObjectGuid());
        bot->GetSession()->HandlePetAction(data);
        return true;
    }

    return false;
}

void PlayerbotAI::WaitForSpellCast(Spell *spell)
{
    SetAIInternalUpdateDelay(GetSpellCastDuration(spell));
}

void PlayerbotAI::InterruptSpell(bool withMeleeAndAuto)
{
    for (int type = CURRENT_MELEE_SPELL; type < CURRENT_CHANNELED_SPELL; type++)
    {
        if (!withMeleeAndAuto)
        {
            if (type == CURRENT_MELEE_SPELL || type == CURRENT_AUTOREPEAT_SPELL)
                continue;
        }
        Spell* currentSpell = bot->GetCurrentSpell((CurrentSpellTypes)type);
        if (currentSpell && currentSpell->m_spellInfo && (currentSpell->m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT))
        {
            bot->InterruptSpell((CurrentSpellTypes)type);
            SpellInterrupted(currentSpell->m_spellInfo->Id);
        }
    }
}

bool PlayerbotAI::RemoveAura(const std::string& name)
{
    const Aura* aura = GetAura(name, bot);
    if (aura)
    {
        const uint32 spellId = aura->GetSpellProto()->Id;
        if (spellId > 0)
        {
            bot->RemoveAurasDueToSpell(spellId);
            return true;
        }
    }

    return false;
}

bool PlayerbotAI::IsInterruptableSpellCasting(Unit* target, std::string spell, uint8 effectMask)
{
    if (!target)
        return false;
    uint32 spellid = aiObjectContext->GetValue<uint32>("spell id", spell)->Get();
    if (!spellid)
        return false;

    SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(spellid);
    if (!spellInfo)
        return false;

    // The victim's cast, read off the core spell slots (ai/playerbot/
    // InterruptCastingPolicy.h owns the verdict): only a generic spell
    // still on its bar or channeling, or a channeled spell in flight,
    // counts. The old IsNonMeleeSpellCasted(true) probe also fired on
    // DELAYED instants (missile in flight, nothing on a bar) and the
    // autorepeat slot, so melee bosses read as "casting" and Pummel/Kick
    // fired on cooldown into thin air.
    auto ReadSlot = [target](CurrentSpellTypes type, bool channeledSlot, ai::InterruptTargetSpell& out) -> ai::InterruptTargetSpell const*
    {
        Spell* current = target->GetCurrentSpell(type);
        if (!current || !current->m_spellInfo)
            return nullptr;
        out.spell = current;
        out.channeledSlot = channeledSlot;
        out.state = current->getState();
        out.castTime = current->GetCastTime();
        out.preventionType = current->m_spellInfo->PreventionType;
        out.interruptFlags = current->m_spellInfo->InterruptFlags;
        out.channelInterruptFlags = current->m_spellInfo->ChannelInterruptFlags;
        return &out;
    };
    ai::InterruptTargetSpell generic{}, channeled{};
    ai::InterruptTargetSpell const* genericSlot = ReadSlot(CURRENT_GENERIC_SPELL, false, generic);
    ai::InterruptTargetSpell const* channeledSlot = ReadSlot(CURRENT_CHANNELED_SPELL, true, channeled);

    ai::InterruptSpellInfo interrupt;
    interrupt.interruptFlags = spellInfo->InterruptFlags;
    interrupt.preventionType = spellInfo->PreventionType;
    for (int32 i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2 && i < ai::InterruptSpellInfo::MAX_EFFECTS; ++i)
    {
        interrupt.effect[i] = spellInfo->Effect[i];
        interrupt.effectAura[i] = spellInfo->EffectApplyAuraName[i];
        interrupt.effectImmune[i] = target->IsImmuneToSpellEffect(spellInfo, (SpellEffectIndex)i, true);
    }
    (void)effectMask;
    return ai::ShouldInterruptTargetCasting(genericSlot, channeledSlot, interrupt);
}

bool PlayerbotAI::HasAuraToDispel(Unit* target, uint32 dispelType)
{
    if (ai::GetZulGurubProtectedDispelMask(this, target) & GetDispellMask(DispelType(dispelType)))
        return false;

    bool isFriend = sServerFacade.IsFriendlyTo(bot, target);
	for (uint32 type = SPELL_AURA_NONE; type < TOTAL_AURAS; ++type)
	{
		Unit::AuraList const& auras = target->GetAurasByType((AuraType)type);
		for (Unit::AuraList::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
		{
			const Aura* aura = *itr;
			const SpellEntry* entry = aura->GetSpellProto();
			uint32 spellId = entry->Id;

			bool isPositiveSpell = IsPositiveSpell(spellId);
			if (isPositiveSpell && isFriend)
				continue;

			if (!isPositiveSpell && !isFriend)
				continue;

			// A short aura (totem pulse, brief snare) is not worth a dispel,
			// but it must not blind the rest of the scan: skip it and keep
			// looking (donor `continue`s here; `return false` dropped every
			// real debuff behind one short aura).
			if (sPlayerbotAIConfig.dispelAuraDuration && aura->GetAuraDuration() && aura->GetAuraDuration() < (int32)sPlayerbotAIConfig.dispelAuraDuration)
			    continue;

			if (canDispel(entry, dispelType))
				return true;
		}
	}
	return false;
}

#ifndef WIN32
inline int strcmpi(const char* s1, const char* s2)
{
    for (; *s1 && *s2 && (toupper(*s1) == toupper(*s2)); ++s1, ++s2);
    return *s1 - *s2;
}
#endif

// Debuffs a healer must not cleanse (Turtle 1.18.1 spell ids). Mutating Injection (Grobbulus, Naxx, 28169) drops a
// poison cloud where its carrier stands when it is removed, by dispel or by expiry (core boss_grobbulus.cpp,
// spell_mutating_injection). A cleanse while the carrier is still in the raid puts the cloud in the raid; bomb runout
// moves the carrier clear first (BombRunoutPolicy.h). Ported from mod-playerbots dispel_whitelist.
// StoneformTrigger is held back by the same list: Stoneform removes every disease on the bot when it is cast.
static constexpr uint32 kNeverDispelSpellIds[] = { 28169 };

bool PlayerbotAI::IsNeverDispelSpell(uint32 spellId)
{
    for (uint32 neverDispelId : kNeverDispelSpellIds)
    {
        if (spellId == neverDispelId)
            return true;
    }
    return false;
}

bool PlayerbotAI::canDispel(const SpellEntry* entry, uint32 dispelType)
{
    if (entry->Dispel != dispelType)
        return false;

    if (IsNeverDispelSpell(entry->Id))
        return false;

    return entry->SpellName[0].empty() ||
        (strcmpi(entry->SpellName[0].c_str(), "demon skin") &&
        strcmpi(entry->SpellName[0].c_str(), "mage armor") &&
        strcmpi(entry->SpellName[0].c_str(), "frost armor") &&
        strcmpi(entry->SpellName[0].c_str(), "wavering will") &&
        strcmpi(entry->SpellName[0].c_str(), "chilled") &&
        strcmpi(entry->SpellName[0].c_str(), "mana tap") &&
        strcmpi(entry->SpellName[0].c_str(), "ice armor"));
}

bool PlayerbotAI::IsHealSpell(const SpellEntry* spell)
{
    // Holy Light/Flash of Light
    if (spell->SpellFamilyName == SPELLFAMILY_PALADIN)
    {
        if (spell->SpellIconID == 70 ||
            spell->SpellIconID == 242)
            return true;
    }

    for (uint8 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        switch (spell->Effect[i])
        {
            case SPELL_EFFECT_HEAL:
            case SPELL_EFFECT_HEAL_MAX_HEALTH:
                return true;
            case SPELL_EFFECT_APPLY_AURA:
            case SPELL_EFFECT_APPLY_AREA_AURA_FRIEND:
            case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
            case SPELL_EFFECT_APPLY_AREA_AURA_PET:
            {
                switch (spell->EffectApplyAuraName[i])
                {
                    case SPELL_AURA_PERIODIC_HEAL:
                        return true;
                }
                break;
            }
        }
    }
    return false;
}

bool PlayerbotAI::IsMiningNode(const GameObject* go)
{
    if (go)
    {
        const GameObjectInfo* goInfo = go->GetGOInfo();
        if (goInfo)
        {
            const LockEntry* lockInfo = sLockStore.LookupEntry(goInfo->GetLockId());
            if (lockInfo)
            {
                for (int i = 0; i < 8; ++i)
                {
                    if (lockInfo->Type[i] == LOCK_KEY_SKILL)
                    {
                        const uint32 skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                        if (skillId == SKILL_MINING)
                        {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

bool PlayerbotAI::IsHerb(const GameObject* go)
{
    if (go)
    {
        const GameObjectInfo* goInfo = go->GetGOInfo();
        if (goInfo)
        {
            const LockEntry* lockInfo = sLockStore.LookupEntry(goInfo->GetLockId());
            if (lockInfo)
            {
                for (int i = 0; i < 8; ++i)
                {
                    if (lockInfo->Type[i] == LOCK_KEY_SKILL)
                    {
                        const uint32 skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                        if (skillId == SKILL_HERBALISM)
                        {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

bool PlayerbotAI::HasSpellItems(uint32 spellId, const Item* castItem) const
{
    const SpellEntry* spellEntry = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    if (spellEntry)
    {
        if (HasCheat(BotCheatMask::item))
        {
            return true;
        }
        else
        {
            if (!false)
            {
                for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                {
                    if (spellEntry->Reagent[i] <= 0)
                    {
                        continue;
                    }

                    uint32 itemid = spellEntry->Reagent[i];
                    uint32 itemcount = spellEntry->ReagentCount[i];

                    // if CastItem is also spell reagent
                    if (castItem && castItem->GetEntry() == itemid)
                    {
                        const ItemPrototype* proto = castItem->GetProto();
                        if (!proto)
                        {
                            return false;
                        }

                        for (uint8 s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
                        {
                            // CastItem will be used up and does not count as reagent
                            int32 charges = castItem->GetSpellCharges(s);
                            if (proto->Spells[s].SpellCharges < 0 && abs(charges) < 2)
                            {
                                ++itemcount;
                                break;
                            }
                        }
                    }

                    if (!bot->HasItemCount(itemid, itemcount))
                    {
                        return false;
                    }
                }
            }

            // check totem-item requirements (items presence in inventory)
            uint32 totems = MAX_SPELL_TOTEMS;
            for (auto i : spellEntry->Totem)
            {
                if (i != 0)
                {
                    if (bot->HasItemCount(i, 1))
                    {
                        totems -= 1;
                    }
                }
                else
                {
                    totems -= 1;
                }
            }

            if (totems != 0)
            {
                return false;
            }
        }
    }

    return false;
}


uint32 PlayerbotAI::GetBuffedCount(Player* player, std::string spellname)
{
    Group* group = bot->GetGroup();
    uint32 bcount = 0;

    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || !member->IsInWorld() && member->GetMapId() != bot->GetMapId())
                continue;

            if (!IsInGroup_Helper(member, player, true))
                continue;

            if (HasAura(spellname, member))
                bcount++;
        }
    }
    return bcount;
}
