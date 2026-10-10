
#include "playerbot/playerbot.h"
#include "playerbot/GroupBuffPolicy.h"
#include "playerbot/ForceRebuffPolicy.h"
#include "playerbot/GroupMembers.h"
#include "GenericActions.h"
#include "UseItemAction.h"

#include <map>
#include <mutex>
#include <utility>

using namespace ai;

namespace
{
bool CanInterruptCurrentSpell(Spell const* spell)
{
    // The core has no CanBeInterrupted() helper, but its spell state is the
    // same lifecycle contract used by the mature implementation: preparing,
    // casting, and delayed spells can still be interrupted. Finished/idle
    // spells are not current interrupt targets.
    return spell && spell->getState() <= SPELL_STATE_DELAYED;
}

// Seconds between two out-of-combat cast attempts of the same upkeep buff on the
// same target (issue #359).
uint32 const BUFF_RETRY_COOLDOWN = 3;
// Issue #378: a group buff covers the whole (sub)group from one cast and costs a
// reagent, so a target that cannot receive it (another raid subgroup, too low
// level for the rank) must not be re-tried every few seconds - each retry burned
// the reagent and mana and still left the target unbuffed. One attempt a minute.
uint32 const GREATER_BUFF_RETRY_COOLDOWN = 60;
// Seconds between two "SelfBuff" telemetry rows for the same bot and spell.
uint32 const SELF_BUFF_EVENT_INTERVAL = 10;
// Mana floors live in ai::BuffManaFloor (GroupBuffPolicy.h): 40/70 pool,
// 20/40 with a real player master.

// Issue #T7: how long a buff claim keeps the other buffers of the same spell
// away. Must cover the cast plus the delay before the aura lands (the longest
// upkeep cast is 1.5 s), with margin; a claim that outlives a failed cast only
// delays the retry of whoever sees the missing aura afterwards.
time_t const BUFF_CLAIM_TTL = 4;

// Group scope marker. HighGuid values are <= 0xF140, so no real object guid raw
// value has 0xFFFF in its top 16 bits - a group scope can never collide with a
// target scope.
uint64 const BUFF_CLAIM_GROUP_SCOPE_TAG = 0xFFFF000000000000ULL;

struct BuffClaim
{
    uint64 caster;
    time_t expiry;
};

// One registry for the whole process. Bots on one map are updated on that map's
// thread, but MapManager runs the continent/instance updates on concurrent
// thread pools, so the map is mutex guarded. The lock is only ever held around
// the map itself (no AI/aura/world call), so it cannot deadlock.
std::mutex& BuffClaimMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::pair<uint64, std::string>, BuffClaim>& BuffClaims()
{
    static std::map<std::pair<uint64, std::string>, BuffClaim> claims;
    return claims;
}
}

void BuffClaimRegistry::Claim(ObjectGuid const& caster, ObjectGuid const& scope, std::string const& spell)
{
    time_t const now = time(0);
    std::lock_guard<std::mutex> lock(BuffClaimMutex());
    std::map<std::pair<uint64, std::string>, BuffClaim>& claims = BuffClaims();

    // No cleanup thread: prune the expired entries on every write, so the map
    // only ever holds casts of the last few seconds.
    for (std::map<std::pair<uint64, std::string>, BuffClaim>::iterator it = claims.begin(); it != claims.end();)
    {
        if (it->second.expiry <= now)
            it = claims.erase(it);
        else
            ++it;
    }

    BuffClaim claim;
    claim.caster = caster.GetRawValue();
    claim.expiry = now + BUFF_CLAIM_TTL;
    claims[std::make_pair(scope.GetRawValue(), spell)] = claim;
}

bool BuffClaimRegistry::IsClaimedByOther(ObjectGuid const& caster, ObjectGuid const& scope, std::string const& spell)
{
    time_t const now = time(0);
    std::lock_guard<std::mutex> lock(BuffClaimMutex());
    std::map<std::pair<uint64, std::string>, BuffClaim>& claims = BuffClaims();

    std::map<std::pair<uint64, std::string>, BuffClaim>::iterator it =
        claims.find(std::make_pair(scope.GetRawValue(), spell));
    if (it == claims.end())
        return false;

    if (it->second.expiry <= now)
    {
        claims.erase(it);
        return false;
    }

    // The caster's own claim must never block it: consecutive casts of one bot
    // stay governed by the retry cooldowns alone.
    return it->second.caster != caster.GetRawValue();
}

ObjectGuid BuffClaimRegistry::GroupScope(Player* bot)
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return bot ? bot->GetObjectGuid() : ObjectGuid();

    return ObjectGuid(BUFF_CLAIM_GROUP_SCOPE_TAG | uint64(group->GetId()));
}

bool BuffClaimRegistry::IsTargetClaimedByOther(Player* caster, Unit* target, std::string const& spell)
{
    if (!caster || !target)
        return false;

    ObjectGuid const& casterGuid = caster->GetObjectGuid();
    if (IsClaimedByOther(casterGuid, target->GetObjectGuid(), spell))
        return true;

    // A greater (area) buff is claimed on the group, not on the member, so the
    // single-target fallback of the same family has to look there too.
    return IsClaimedByOther(casterGuid, GroupScope(caster), spell);
}

CastSpellAction::CastSpellAction(PlayerbotAI* ai, std::string spell)
: Action(ai, spell)
, range(ai->GetRange("spell"))
{
    SetSpellName(spell);

    float spellRange;
    if (ai->GetSpellRange(spell, &spellRange))
    {
        range = spellRange;
    }
}

bool CastSpellAction::Execute(Event& event)
{
    bool executed = false;
    uint32 spellDuration = sPlayerbotAIConfig.globalCoolDown;
    if (spellName == "conjure food" || spellName == "conjure water")
    {
        uint32 castId = 0;
        for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
        {
            uint32 spellId = itr->first;

            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
            if (!pSpellInfo)
                continue;

            std::string namepart = pSpellInfo->SpellName[0];
            strToLower(namepart);

            if (namepart.find(spellName) == std::string::npos)
                continue;

            if (pSpellInfo->Effect[0] != SPELL_EFFECT_CREATE_ITEM)
                continue;

            uint32 itemId = pSpellInfo->EffectItemType[0];
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            if (!proto)
                continue;

            if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
                continue;

            if (pSpellInfo->Id > castId)
                castId = pSpellInfo->Id;
        }

        executed = ai->CastSpell(castId, bot, nullptr, false, &spellDuration);
    }
    else
    {
        Unit* target = GetTarget();
        if (!target)
        {
            sLog.outDebug("%s: CastSpellAction::Execute aborting '%s' - target resolved to null at cast time", bot->GetName(), spellName.c_str());
            return false;
        }

        if (GetTargetName() == "current target" && (!bot->GetCurrentSpell(CURRENT_MELEE_SPELL) && !bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)))
        {
            if (bot->GetClass() == CLASS_HUNTER && spellName != "auto shot" && sServerFacade.getDistance2d(bot, target) > 5.0f)
                ai->CastSpell("auto shot", target);
        }

        executed = ai->CastSpell(spellName, target, nullptr, false, &spellDuration);
    }

    if (executed)
    {
        if (ai->HasCheat(BotCheatMask::attackspeed))
            spellDuration = 1;

        SetDuration(spellDuration);
    }

    return executed;
}

bool CastSpellAction::isPossible()
{
    if (spellName == "mount")
    {
        if (!bot->IsMounted() && !bot->IsInCombat())
        {
            return true;
        }
        if (bot->IsInCombat())
        {
            ai->Unmount();
            return false;
        }
    }

    Unit* spellTarget = GetTarget();
    if (!spellTarget)
        return false;

    bool canReach = false;
    if (spellTarget == bot)
    {
        canReach = true;
    }
    else
    {
        float dist = bot->GetDistance(spellTarget, ai->IsRanged(bot) ? SizeFactor::CombatReach : SizeFactor::CombatReachWithMelee);
        if (range == ATTACK_DISTANCE)
        {
            canReach = bot->CanReachWithMeleeAutoAttack(spellTarget);
        }
        else
        {
            canReach = dist <= (range + sPlayerbotAIConfig.contactDistance);
            if (!spellId)
                return false;

            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
            if (!pSpellInfo)
                return false;

            if (range != ATTACK_DISTANCE && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_COMBAT && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_SELF_ONLY && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_ANYWHERE)
            {
                float max_range, min_range;
                if (ai->GetSpellRange(GetSpellName(), &max_range, &min_range))
                {
                    canReach = dist < max_range && dist >= min_range;
                }
            }
        }
    }

    if(!canReach)
    {
        return false;
    }

    // Check if the spell can be casted
	return ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true);
}

bool CastSpellAction::isUseful()
{
    if(!AI_VALUE2(bool, "spell cast useful", spellName))
        return false;

    Unit* spellTarget = GetTarget();
    if (!spellTarget)
        return false;

    if (!spellTarget->IsInWorld() || spellTarget->GetMapId() != bot->GetMapId())
        return false;

    return true;
}

NextAction** CastSpellAction::getPrerequisites()
{
    // Set the reach action as the cast spell prerequisite when needed
    const std::string reachAction = GetReachActionName();
    if (!reachAction.empty())
    {
        const std::string targetName = GetTargetName();

        // No need for a reach action when target is self
        if (targetName != "self target")
        {
            const std::string spellName = GetSpellName();
            const std::string targetQualifier = GetTargetQualifier();

            // Generate the reach action with qualifiers
            std::vector<std::string> qualifiers = { spellName, targetName };
            if (!targetQualifier.empty())
            {
                qualifiers.push_back(targetQualifier);
            }

            const std::string qualifiersStr = Qualified::MultiQualify(qualifiers, "::");
            return NextAction::merge(NextAction::array(0, new NextAction(reachAction + "::" + qualifiersStr), NULL), Action::getPrerequisites());
        }
    }

    return Action::getPrerequisites();
}

void CastSpellAction::SetSpellName(const std::string& name, std::string spellIDContextName /*= "spell id"*/, bool force)
{
    if (force || spellName != name)
    {
        spellName = name;
        spellId = ai->GetAiObjectContext()->GetValue<uint32>(spellIDContextName, name)->Get();

        float spellRange;
        if (ai->GetSpellRange(spellName, &spellRange))
        {
            range = spellRange;
        }
    }
}

Value<Unit*>* CastSpellAction::GetTargetValue()
{
    std::string targetName = GetTargetName();
    std::string targetNameQualifier = GetTargetQualifier();
    return targetNameQualifier.empty()
        ? context->GetValue<Unit*>(targetName)
        : context->GetValue<Unit*>(targetName, targetNameQualifier);
}

Unit* CastSpellAction::GetTarget()
{
    Value<Unit*>* targetValue = GetTargetValue();
    return targetValue ? targetValue->Get() : nullptr;
}

bool CastPetSpellAction::isPossible()
{
    Unit* spellTarget = GetTarget();
    if (!spellTarget)
        return false;

    Unit* pet = AI_VALUE(Unit*, "pet target");
    if (pet && ai->IsSafe(pet))
    {
        const uint32& spellId = GetSpellID();
        if (pet->HasSpell(spellId) && !pet->HasSpellCooldown(spellId))
        {
            // Check if the pet is not too far from the owner
            if (bot->GetDistance(pet) <= sPlayerbotAIConfig.sightDistance)
            {
                bool canReach = false;
                const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
                if (pSpellInfo)
                {
                    const float dist = pet->GetDistance(spellTarget, SizeFactor::CombatReach);
                    canReach = dist <= (range + sPlayerbotAIConfig.contactDistance);

                    if (pSpellInfo->rangeIndex != SPELL_RANGE_IDX_COMBAT && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_SELF_ONLY && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_ANYWHERE)
                    {
                        float max_range, min_range;
                        if (ai->GetSpellRange(GetSpellName(), &max_range, &min_range))
                        {
                            canReach = dist < max_range&& dist >= min_range;
                        }
                    }
                }

                if (canReach)
                {
                    return ai->CanCastSpell(spellId, spellTarget, 0, true);
                }
            }
        }
    }

    return false;
}

bool CastAuraSpellAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target)
        return false;
    // Issue #468 (donor BuffBelowRefreshTarget): re-arm while a LONG aura is
    // still up but expiring, so the recast lands before the buff drops.
    // Short combat buffs (max < 5 min) only re-arm on fall-off, as before.
    Aura* aura = ai->GetAura(GetSpellName(), target, isOwner);
    // Force-rebuff pass (donor BuffBelowRefreshTarget action site): while a
    // rebuff window is pending OOC, a LONG buff below the margin rule counts
    // as useful so the expiring cast actually starts. Outside the window
    // the normal refresh rule applies.
    if (aura && !bot->IsInCombat())
    {
        AiObjectContext* rebuffContext = ai->GetAiObjectContext();
        uint32 beginMs = rebuffContext ? rebuffContext->GetValue<int32>("manual int", "force rebuff begin ms")->Get() : 0;
        uint32 nowMs = WorldTimer::getMSTime();
        if (beginMs && ai::ForceRebuffPending(beginMs, nowMs) &&
            ai::ForceRebuffBelowTarget(true, aura->GetAuraDuration(),
                aura->GetAuraMaxDuration(), beginMs, nowMs))
            return CastSpellAction::isUseful();
    }
    return CastSpellAction::isUseful() && ai::BuffNeedsRefresh(aura != nullptr,
        aura ? aura->GetAuraDuration() : 0, aura ? aura->GetAuraMaxDuration() : 0);
}

bool CastBuffSpellAction::isUseful()
{
    Unit* target = GetTarget();

    // Issue #T7: another bot already has this spell in flight for the target (or
    // for its whole group, for the area buffs) - stand down this tick so the
    // queued basket is dropped instead of duplicating the cast.
    if (BuffClaimRegistry::IsTargetClaimedByOther(bot, target, GetSpellName()))
        return false;

    // Issue #378: the mana floor and the retry window are upkeep rules. Combat
    // casts of the same spells (seals, totems, Mana Shield, Earth Shield, an
    // Inner Fire re-apply) must never be held back by them.
    if (!bot->IsInCombat())
    {
        if (target && lastAttemptTime && target->getObjectGuid() == lastAttemptTarget &&
            time(0) - lastAttemptTime < (time_t)GetBuffRetryCooldown())
            return false;

        if (!HasManaForBuff())
            return false;
    }

    return CastAuraSpellAction::isUseful();
}

void CastBuffSpellAction::ClaimBuffCast(Unit* target)
{
    if (target)
        BuffClaimRegistry::Claim(bot->GetObjectGuid(), target->GetObjectGuid(), GetSpellName());
}

bool CastBuffSpellAction::HasManaForBuff()
{
    const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(GetSpellID());
    // Only mana upkeep buffs are held back. Spells on a recovery timer are
    // deliberate cooldown abilities (Ice Block, Feign Death, Dash...) that must
    // stay usable whatever the mana pool looks like.
    if (!spellInfo || spellInfo->powerType != POWER_MANA || spellInfo->GetRecoveryTime())
        return true;

    if (bot->GetPowerType() != POWER_MANA)
        return true;

    // Shapeshift forms are movement/combat modes, not upkeep buffs. They are
    // paid for with a percentage of base mana (Bear/Cat 35%, Travel/Aquatic 13%)
    // but must stay castable at any mana level, or the druid is stuck in caster
    // form until it regenerates.
    for (uint8 i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_SHAPESHIFT)
            return true;

    // Aspects are permanent, mutually exclusive mode toggles rather than upkeep
    // buffs - a hunter must stay able to switch them at any mana level.
    if (GetSpellName().find("aspect of ") == 0)
        return true;

    // Stances, stealth and the other zero-cost toggles are free. Percentage-cost
    // spells (Blessing of Salvation 8%, Dampen/Amplify Magic 6%) store manaCost 0
    // and their price in ManaCostPercentage; the core charges
    // ManaCostPercentage * GetCreateMana() / 100 for them (Spell::CalculateManaCost),
    // so they must not slip past the floor as "free".
    uint32 const manaCost = spellInfo->manaCost
        ? spellInfo->manaCost
        : uint32(spellInfo->ManaCostPercentage) * bot->GetCreateMana() / 100;
    if (!manaCost)
        return true;

    uint8 const minMana = ai::BuffManaFloor(spellInfo->procCharges != 0, ai->HasRealPlayerMaster());
    return ai->GetManaPercent() >= minMana;
}

uint32 CastBuffSpellAction::GetBuffRetryCooldown() const
{
    return BUFF_RETRY_COOLDOWN;
}

uint32 GreaterBuffOnPartyAction::GetBuffRetryCooldown() const
{
    return GREATER_BUFF_RETRY_COOLDOWN;
}

void GreaterBuffOnPartyAction::ClaimBuffCast(Unit* /*target*/)
{
    // Issue #T7: the area buff covers the whole (sub)group from one cast, so the
    // claim is on the group - and under the lower single-target name as well, so
    // another bot's Power Word: Fortitude fallback on a member stands down too.
    ObjectGuid const scope = BuffClaimRegistry::GroupScope(bot);
    BuffClaimRegistry::Claim(bot->GetObjectGuid(), scope, GetSpellName());
    if (!lowerSpell.empty())
        BuffClaimRegistry::Claim(bot->GetObjectGuid(), scope, lowerSpell);
}

uint32 BuffOnPartyAction::CountGroupMembersMissingBoth(std::string const& groupName) const
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return 0;
    uint32 missing = 0;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || !member->IsInWorld() || member->GetMapId() != bot->GetMapId() || !sServerFacade.IsAlive(member))
            continue;
        Aura* single = ai->GetAura(GetSpellName(), member);
        if (!ai::BuffNeedsRefresh(single != nullptr, single ? single->GetAuraDuration() : 0,
            single ? single->GetAuraMaxDuration() : 0))
            continue;
        Aura* grouped = ai->GetAura(groupName, member);
        if (!ai::BuffNeedsRefresh(grouped != nullptr, grouped ? grouped->GetAuraDuration() : 0,
            grouped ? grouped->GetAuraMaxDuration() : 0))
            continue;
        ++missing;
    }
    return missing;
}

bool BuffOnPartyAction::isUseful()
{
    if (!CastBuffSpellAction::isUseful())
        return false;
    // Scoped to recognized upgrade pairs (IsGroupBuffUpgradePair): paladin
    // blessings are not in the variant map, so their single-target party
    // action keeps its existing behavior untouched.
    std::string const groupName = ai::GroupBuffVariantFor(GetSpellName());
    if (groupName.empty() || !ai::IsGroupBuffUpgradePair(groupName, GetSpellName()))
        return true;
    if (!ai->HasSpell(groupName))
        return true;
    if (AI_VALUE2(uint32, "has reagents for", AI_VALUE2(uint32, "spell id", groupName)) == 0)
        return true;
    return !ai::ShouldUpgradeToGroupBuff(true, true, CountGroupMembersMissingBoth(groupName));
}

bool CastBuffSpellAction::Execute(Event& event)
{
    // The retry stamp and the claim are written only once the cast actually
    // starts: a whiffed attempt (target out of range or LOS at cast time,
    // reagents just spent, master sprinting away) used to silence the spell
    // party-wide for the whole retry window (3 s single, 60 s group) plus
    // the 4 s claim, while the trigger only re-checks every 2-4 s on top.
    // The issue-#359 loop this replaced (retrying a missing aura every tick)
    // cannot come back: a failed Execute runs no cast, spends no mana and
    // lands no aura, so the next tick re-evaluates from the same state and
    // either casts or fails its gates. The claim still covers the cast plus
    // the aura-in-flight delay for successful casts (issue #T7).
    Unit* const target = GetTarget();

    if (!CastSpellAction::Execute(event))
        return false;

    // Force-rebuff pass (donor NoteBuffWork, window-gated like the donor):
    // a buff cast that actually starts marks work this cycle so the
    // ready-check gate holds the confirm while casts are still landing.
    // Failed casts mark nothing, and casts with no window open mark nothing
    // (chained instant OOC buffs must not delay a held confirm tick by tick
    // outside any pass).
    if (!bot->IsInCombat())
    {
        uint32 beginMs = context->GetValue<int32>("manual int", "force rebuff begin ms")->Get();
        if (beginMs && ai::ForceRebuffPending(beginMs, WorldTimer::getMSTime()))
            context->GetValue<bool>("manual bool", "force rebuff buff pending")->Set(true);
    }
    if (target)
    {
        lastAttemptTarget = target->getObjectGuid();
        ClaimBuffCast(target);
    }
    lastAttemptTime = time(0);

    if (target && target == bot)
    {
        time_t const now = time(0);
        if (now - lastSelfBuffEventTime >= (time_t)SELF_BUFF_EVENT_INTERVAL)
        {
            lastSelfBuffEventTime = now;
            sPlayerbotAIConfig.logEvent(ai, "SelfBuff", GetSpellName(), std::to_string(bot->GetLevel()));
        }
    }

    return true;
}

bool CastMeleeAoeSpellAction::isUseful()
{
    return CastSpellAction::isUseful() && sServerFacade.IsDistanceLessOrEqualThan(AI_VALUE2(float, "distance", GetTargetName()), radius);
}

bool CastEnchantItemAction::isPossible()
{
    if (!CastSpellAction::isPossible())
        return false;

    return GetSpellID() && AI_VALUE2(Item*, "item for spell", GetSpellID());
}

bool CastAoeHealSpellAction::isUseful()
{
    return CastSpellAction::isUseful();
}

// Night2 gaps 1+2 (donor HealerAutoSaveManaMultiplier): refuse to START a
// single-target direct heal whose expected amount dwarfs the target's
// missing health, and refuse mana-hungry heals while the healer runs low.
// Never vetoes a target in danger (at/below lowHealth): a vetoed big heal
// falls through to the cheaper alternative in the same trigger row
// (flash -> greater -> heal -> lesser via the action-node fallback chain).
bool HealPartyMemberAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target)
        return false;
    HealerManaState heal{};
    heal.targetHealth = target->GetHealthPercent() > 100.0f ? 100 : (std::uint8_t)target->GetHealthPercent();
    heal.healerMana = AI_VALUE2(uint8, "mana", "self target");
    heal.estAmount = estAmount;
    heal.efficiency = manaEfficiency;
    heal.targetIsTank = target->IsPlayer() && ai->IsTank((Player*)target, false);
    heal.lowHealth = sPlayerbotAIConfig.lowHealth;
    heal.mediumHealth = sPlayerbotAIConfig.mediumHealth;
    heal.mediumMana = sPlayerbotAIConfig.mediumMana;
    if (!ShouldStartHeal(heal))
        return false;
    return CastHealingSpellAction::isUseful();
}

bool HealHotPartyMemberAction::isUseful()
{
    return HealPartyMemberAction::isUseful() && !ai->HasAura(GetSpellName(), GetTarget());
}

bool CastShootAction::isPossible()
{
    // Check if the bot has a ranged weapon equipped and has ammo
    UpdateWeaponInfo();
    if (rangedWeapon && !needsAmmo)
    {
        // Check if the target exist and it can be shot
        Unit* target = GetTarget();
        if (target && sServerFacade.IsWithinLOSInMap(bot, target))
        {
            return CastSpellAction::isPossible();
        }
    }

    return false;
}

bool CastShootAction::Execute(Event& event)
{
    bool succeeded = false;

    UpdateWeaponInfo();
    if (rangedWeapon && !needsAmmo)
    {
        // Prevent calling the shoot spell when already active
        Spell* autoRepeatSpell = ai->GetBot()->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL);
        if (autoRepeatSpell && (autoRepeatSpell->m_spellInfo->Id == GetSpellID()))
        {
            succeeded = true;
        }
        else if (CastSpellAction::Execute(event))
        {
            succeeded = true;
        }

        if (succeeded)
        {
            SetDuration(weaponDelay);
        }
    }

    return succeeded;
}

void CastShootAction::UpdateWeaponInfo()
{
    // Check if we have a new ranged weapon equipped
    const Item* equippedWeapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (equippedWeapon)
    {
        if (equippedWeapon != rangedWeapon)
        {
            std::string spellName = "shoot";
            bool isRangedWeapon = false;

            needsAmmo = true;

            const ItemPrototype* itemPrototype = equippedWeapon->GetProto();
            switch (itemPrototype->SubClass)
            {
                case ITEM_SUBCLASS_WEAPON_GUN:
                {
                    isRangedWeapon = true;
                    spellName += " gun";
                    break;
                }
                case ITEM_SUBCLASS_WEAPON_BOW:
                {
                    isRangedWeapon = true;
                    spellName += " bow";
                    break;
                }
                case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                {
                    isRangedWeapon = true;
                    spellName += " crossbow";
                    break;
                }
                case ITEM_SUBCLASS_WEAPON_WAND:
                {
                    isRangedWeapon = true;
                    needsAmmo = false;
                    break;
                }
                case ITEM_SUBCLASS_WEAPON_THROWN:
                {
                    isRangedWeapon = true;
                    spellName = "throw";
                    break;
                }

                default: break;
            }

            // Set the new weapon parameters
            if (isRangedWeapon)
            {
                SetSpellName(spellName);
                rangedWeapon = equippedWeapon;
                weaponDelay = itemPrototype->Delay + sPlayerbotAIConfig.globalCoolDown;
            }
        }

        // Check the ammunition
        needsAmmo = (GetSpellName() != "shoot") ? (AI_VALUE2(uint32, "item count", "ammo") <= 0) : false;
        if (!needsAmmo && bot->GetUInt32Value(PLAYER_AMMO_ID) == 0)
        {
            std::list<Item*> ammo = AI_VALUE2(std::list<Item*>, "inventory items", "ammo");
            if (!ammo.empty() && ammo.front())
                bot->SetAmmo(ammo.front()->GetEntry());
        }
    }
    else
    {
        rangedWeapon = nullptr;
    }
}

bool RemoveBuffAction::isUseful()
{
    return ai->HasAura(name, bot);
}

bool RemoveBuffAction::Execute(Event& event)
{
    ai->RemoveAura(name);
    return !ai->HasAura(name, bot);
}

bool InterruptCurrentSpellAction::isUseful()
{
    for (int type = CURRENT_MELEE_SPELL; type < CURRENT_CHANNELED_SPELL; type++)
    {
        Spell* currentSpell = bot->GetCurrentSpell((CurrentSpellTypes)type);
        if (CanInterruptCurrentSpell(currentSpell))
            return true;
    }
    return false;
}

bool InterruptCurrentSpellAction::Execute(Event& event)
{
    bool interrupted = false;
    for (int type = CURRENT_MELEE_SPELL; type < CURRENT_CHANNELED_SPELL; type++)
    {
        Spell* currentSpell = bot->GetCurrentSpell((CurrentSpellTypes)type);
        if (CanInterruptCurrentSpell(currentSpell))
        {
            bot->InterruptSpell((CurrentSpellTypes)type);
            ai->SpellInterrupted(currentSpell->m_spellInfo->Id);
            interrupted = true;
        }
    }
    return interrupted;
}

Unit* CastSpellTargetAction::GetTarget()
{
    // Check for assigned targets
    const std::list<ObjectGuid>& possibleTargets = AI_VALUE(std::list<ObjectGuid>, targetsValue);
    if (!possibleTargets.empty())
    {
        for (const ObjectGuid& possibleTargetGuid : possibleTargets)
        {
            Unit* possibleTarget = ai->GetUnit(possibleTargetGuid);
            if (IsTargetValid(possibleTarget))
            {
                return possibleTarget;
            }
        }
    }
    else
    {
        // Check for the default target
        Unit* possibleTarget = CastSpellAction::GetTarget();
        if (IsTargetValid(possibleTarget))
        {
            return possibleTarget;
        }
    }

    return nullptr;
}

bool CastSpellTargetAction::IsTargetValid(Unit* target)
{
    Player* targetPlayer = dynamic_cast<Player*>(target);
    return target &&
           ai->IsSafe(target) &&
           (bot == target || sServerFacade.getDistance2d(bot, target) < sPlayerbotAIConfig.sightDistance) &&
           (targetPlayer && IsInGroup_Helper(bot, targetPlayer)) &&
           (!aliveCheck || !target->IsDead()) &&
           (!auraCheck || !ai->HasAura(GetSpellID(), target));
}

bool CastItemTargetAction::IsTargetValid(Unit* target)
{
    if (CastSpellTargetAction::IsTargetValid(target))
    {
        if (itemAuraCheck)
        {
            const uint32 itemId = GetItemId();
            const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
            if (proto)
            {
                for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
                {
                    if (proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE || proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
                    {
                        if (proto->Spells[i].SpellId > 0 && ai->HasAura(proto->Spells[i].SpellId, target))
                        {
                            return false;
                        }
                    }
                }

                return true;
            }
        }
        else
        {
            return true;
        }
    }

    return false;
}

bool CastItemTargetAction::isUseful()
{
    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(GetItemId());
    if (proto)
    {
        std::set<uint32>& skipSpells = AI_VALUE(std::set<uint32>&, "skip spells list");
        if (!skipSpells.empty())
        {
            for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                const _Spell& spellData = proto->Spells[i];
                if (spellData.SpellId)
                {
                    if (skipSpells.find(spellData.SpellId) != skipSpells.end())
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    return false;
}

bool CastItemTargetAction::isPossible()
{
    uint32 itemId = GetItemId();
    if (!itemId)
        return false;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);

    if (!proto)
        return false;

    if (HasSpellCooldown(itemId))
        return false;

    if (!ai->HasCheat(BotCheatMask::item) && !bot->HasItemCount(itemId, 1))
        return false;

    uint32 spellCount = 0;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        _Spell const& spellData = proto->Spells[i];

        // no spell
        if (!spellData.SpellId)
            continue;

        // wrong triggering type
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE && spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
            continue;

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellData.SpellId);
        if (!spellInfo)
        {
            continue;
        }

        spellCount++;
    }

    return spellCount;
}

bool CastItemTargetAction::Execute(Event& event)
{
    uint32 itemId = GetItemId();
    Unit* target = GetTarget();
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);

    if (!proto)
        return false;

    Item* item = nullptr;

    if (!ai->HasCheat(BotCheatMask::item)) //If bot has no item cheat it needs an item to cast.
    {
        std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", chat->formatQItem(itemId));

        if (items.empty())
            return false;

        item = items.front();
    }

    SpellCastTargets targets;
    if (target)
    {
        targets.setUnitTarget(target);
        targets.setDestination(target->getPositionX(), target->getPositionY(), target->getPositionZ());
    }
    else
        targets.m_targetMask = TARGET_FLAG_SELF;

    // use triggered flag only for items with many spell casts and for not first cast
    int count = 0;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        _Spell const& spellData = proto->Spells[i];

        // no spell
        if (!spellData.SpellId)
            continue;

        // wrong triggering type
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE && spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
            continue;

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellData.SpellId);
        if (!spellInfo)
        {
            continue;
        }

        if (spellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
            targets.m_targetMask = TARGET_FLAG_DEST_LOCATION;

        BotUseItemSpell* spell = new BotUseItemSpell(bot, spellInfo, (count > 0) ? TRIGGERED_OLD_TRIGGERED : TRIGGERED_NONE);

        Item* tItem = nullptr;

        if (item)
        {
            spell->SetCastItem(item);
        }

        bool result = (spell->ForceSpellStart(&targets) == SPELL_CAST_OK);

        if (!result)
            return false;

        if (ai->HasCheat(BotCheatMask::item))
        {
            if (!HasSpellCooldown(itemId))
            {
                bot->RemoveSpellCooldown(spellInfo->Id, false);
                bot->AddSpellAndCategoryCooldowns(spellInfo, proto->ItemId);
            }
        }

        ++count;
    }

    return count;
}

bool CastItemTargetAction::HasSpellCooldown(uint32 itemId)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);

    if (!proto)
        return false;

    uint32 spellId = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
        {
            continue;
        }

        if (proto->Spells[i].SpellId > 0)
        {
            if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId))
                return true;

            if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId, itemId))
                return true;
        }
    }

    return false;
}
