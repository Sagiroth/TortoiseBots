
#include "playerbot/playerbot.h"
#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/strategy/actions/HunterRangedTelemetry.h"
#include "playerbot/ServerFacade.h"
#include "HunterActions.h"
#include "HunterValues.h"
#include "../../../../runtime/policy/class/HunterFeignDeathPolicy.h"

using namespace ai;

bool ai::HunterHasLoadedRangedWeapon(PlayerbotAI* playerbotAi)
{
    if (!playerbotAi)
        return false;

    Player* bot = playerbotAi->GetBot();
    if (!bot)
        return false;

    Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!ranged)
        return false;

    if (ranged->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_THROWN)
        return true;

    return HunterHasRangedAmmo(playerbotAi);
}

// Core timing (Unit::_UpdateAutoRepeatSpell, SpellEntry::GetCastTime): the
// ranged attack timer counts down the time to the next Auto Shot and keeps
// running while a cast pauses the repeat; on resume the core raises it to
// 500 ms if lower. The real cast is the base cast time scaled by ranged haste
// plus a flat 500 ms that GetCastTime adds to every SPELL_ATTR_RANGED spell.
// Without a Spell object GetCastTime adds the 500 but skips the haste, so the
// haste is applied here. Unit::Update runs spell events before
// _UpdateSpells, so the repeat resumes in the tick the cast ends; the margin
// is one world tick (50 ms) of slack, not a latency guess.
bool ai::HunterSteadyShotFitsAutoShotWindow(PlayerbotAI* playerbotAi, uint32 spellId)
{
    static constexpr uint32 kRangedCastExtraMs = 500;
    static constexpr uint32 kAutoShotWindupMs = 500;
    static constexpr uint32 kResumeMarginMs = 50;

    if (!playerbotAi || !spellId)
        return false;

    Player* bot = playerbotAi->GetBot();
    if (!bot)
        return false;

    // Cheapest check first: no ranged cast fits below this.
    uint32 const timer = bot->GetAttackTimer(RANGED_ATTACK);
    if (timer <= kRangedCastExtraMs + kAutoShotWindupMs + kResumeMarginMs)
        return false;

    SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!spellInfo)
        return false;

    uint32 castTime = GetSpellCastTime(spellInfo, bot);
    if ((spellInfo->Attributes & SPELL_ATTR_RANGED) && castTime >= kRangedCastExtraMs)
        castTime = uint32((castTime - kRangedCastExtraMs) * bot->m_modAttackSpeedPct[RANGED_ATTACK]) + kRangedCastExtraMs;

    return timer > castTime + kAutoShotWindupMs + kResumeMarginMs;
}

// Viper Sting leeches the target's mana to the hunter, so it is only worth the global when the hunter is
// the one short on mana (mod-playerbots: own mana < 50) and the target has mana to give. Serpent Sting is the
// default sting otherwise. An explicit 'sting viper' order bypasses the own-mana gate; an explicit 'sting serpent' order always allows Serpent Sting.
static bool HunterWantsViperSting(PlayerbotAI* ai, const std::string& targetName)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    if (!ai->HasSpell("viper sting") || AI_VALUE2(uint8, "mana", targetName) < 10)
        return false;

    return ai->HasStrategy("sting viper", BotState::BOT_STATE_COMBAT) || AI_VALUE2(uint8, "mana", "self target") < 50;
}

bool CastSerpentStingAction::isUseful()
{
    return HunterHasRangedAmmo(ai) && CastRangedDebuffSpellAction::isUseful() && AI_VALUE2(uint8, "health", GetTargetName()) > 50 &&
           (ai->HasStrategy("sting serpent", BotState::BOT_STATE_COMBAT) || !HunterWantsViperSting(ai, GetTargetName()));
}

bool CastViperStingAction::isUseful()
{
    return HunterHasRangedAmmo(ai) && CastRangedDebuffSpellAction::isUseful() && HunterWantsViperSting(ai, GetTargetName());
}

bool FeedPetAction::Execute(Event& event)
{
    Pet* pet = bot->GetPet();
    if (pet && pet->getPetType() == HUNTER_PET && pet->GetHappinessState() != HAPPY)
        pet->SetPower(POWER_HAPPINESS, pet->GetMaxPower(POWER_HAPPINESS));

    return true;
}

bool CastAutoShotAction::isUseful()
{
    if (!ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT))
        return false;
    if (!HunterHasRangedAmmo(ai))
        return false;
    return AI_VALUE(uint32, "active spell") != AI_VALUE2(uint32, "spell id", getName());
}

bool HunterEquipAmmoAction::Execute(Event& event)
{
    // Get ranged weapon
    Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!ranged)
        return false;

    uint32 ammoClass = ITEM_CLASS_PROJECTILE;
    uint32 subClass = 0;

    switch (ranged->GetProto()->SubClass)
    {
    case ITEM_SUBCLASS_WEAPON_GUN:
        subClass = ITEM_SUBCLASS_BULLET;
        break;
    case ITEM_SUBCLASS_WEAPON_BOW:
    case ITEM_SUBCLASS_WEAPON_CROSSBOW:
        subClass = ITEM_SUBCLASS_ARROW;
        break;
    case ITEM_SUBCLASS_WEAPON_THROWN:
        ammoClass = ITEM_CLASS_WEAPON;
        subClass = ITEM_SUBCLASS_WEAPON_THROWN;
        break;
    }

    uint32 currentAmmoId = bot->GetUInt32Value(PLAYER_AMMO_ID);
    const ItemPrototype* bestAmmoProto = nullptr;
    auto checkAmmo = [&](Item* item)
    {
        if (!item)
            return;
        const ItemPrototype* proto = item->GetProto();
        if (!proto)
            return;
        if (proto->Class == ammoClass && proto->SubClass == subClass)
            if (!bestAmmoProto || proto->ItemLevel > bestAmmoProto->ItemLevel)
                bestAmmoProto = proto;
    };

    // Main 16-slot backpack (bag 0, slots 23..38): InitAmmo stores fresh
    // stacks here, so ignoring it left the action blind (Issue #219).
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        checkAmmo(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    // Equipped bags (slots 19..22)
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                checkAmmo(bag->GetItemByPos(j));
        }
    }

    // Equip best ammo if not already equipped
    if (bestAmmoProto && currentAmmoId != bestAmmoProto->ItemId)
    {
        bot->SetUInt32Value(PLAYER_AMMO_ID, bestAmmoProto->ItemId);
        bot->UpdateDamagePhysical(RANGED_ATTACK);
        return true;
    }

    return false;
}

bool CastAutoShotAction::Execute(Event& event)
{
    if (!bot->IsStopped())
    {
        ai->StopMoving();
    }

    bool const executed = CastSpellAction::Execute(event);

    // Hunter ranged telemetry: one row per (re)start of the ranged auto-attack
    // (the core keeps the shot repeating on its own); see HunterRangedTelemetry.h.
    LogHunterRangedEvent(ai, "AutoShot", std::string("cast=") + (executed ? "1" : "0"));

    return executed;
}

bool HunterStepOutAction::Execute(Event& event)
{
    (void)event;
    Unit* target = AI_VALUE(Unit*, targetName);
    HunterKitState* state = GetHunterKitState(ai);
    if (!target || !state)
        return false;

    uint64 const targetRaw = target->GetObjectGuid().GetRawValue();
    uint32 const nowMs = WorldTimer::getMSTime();

    // A step is already under way for this mob: let it finish. A finished
    // step that left the hunter inside is retried farther, up to
    // kHunterStepOutMaxAttempts per episode (HunterSwitchPolicy.h, #1018).
    bool const moving = !bot->IsStopped() || sServerFacade.isMoving(bot);
    if (HunterStepOutEpisodeOpen(*state, targetRaw, nowMs))
    {
        if (moving && nowMs - state->stepOutAttemptMs < kHunterStepOutWindowMs)
            return true;
        if (state->stepOutAttempt >= kHunterStepOutMaxAttempts)
            return false;
    }

    float maxRange = 0.0f;
    float minRange = 0.0f;
    if (!ai->GetSpellRange("auto shot", &maxRange, &minRange) || minRange <= 0.0f)
        return false;

    // Stamp the attempt before trying: a step that finds no spot counts as
    // an attempt, and the next one aims farther.
    BeginHunterStepOut(*state, targetRaw, nowMs);

    float const edge = bot->GetDistance(target, SizeFactor::CombatReach);
    float const center = sServerFacade.getDistance2d(bot, target);
    // Reach terms between centre and edge distance, constant for the step.
    float const reach = std::max(0.0f, center - edge);
    float const step = HunterStepOutLength(edge, minRange, state->stepOutAttempt);
    float const away = target->GetAngle(bot);
    WorldPosition const botPos(bot);

    // Straight back first, then fanned out to either side (donor MoveAway).
    static float const offsets[] = { 0.0f, M_PI_F / 8, -M_PI_F / 8, M_PI_F / 4, -M_PI_F / 4,
        3 * M_PI_F / 8, -3 * M_PI_F / 8, M_PI_F / 2, -M_PI_F / 2 };
    bool moved = false;
    for (float const offset : offsets)
    {
        float const heading = away + offset;
        WorldPosition cand = botPos + WorldPosition(0, step * cos(heading), step * sin(heading), 1.0f);
        cand.setZ(cand.GetHeight());

        // A fanned step must still land outside the dead zone.
        float const landingCenter = sqrtf((cand.getX() - target->GetPositionX()) * (cand.getX() - target->GetPositionX()) +
            (cand.getY() - target->GetPositionY()) * (cand.getY() - target->GetPositionY()));
        if (landingCenter - reach < minRange + 1.0f)
            continue;
        if (!bot->IsWithinLOS(cand.getX(), cand.getY(), cand.getZ() + bot->GetCollisionHeight()))
            continue;
        if (!botPos.canPathTo(cand, bot))
            continue;
        if (!MoveTo(bot->GetMapId(), cand.getX(), cand.getY(), cand.getZ(), false, IsReaction(), false, true))
            continue;

        moved = true;
        break;
    }

    LogHunterRangedEvent(ai, "StepOut", std::string("moved=") + (moved ? "1" : "0") +
        " attempt=" + std::to_string(state->stepOutAttempt));
    return moved;
}

bool CastFeignDeathAction::isUseful()
{
    if (!CastBuffSpellAction::isUseful())
        return false;
    // The mobs whose victim is the hunter (its own attacker set, no scan):
    // the others on each one's threat list.
    std::vector<uint32_t> others;
    for (Unit* attacker : bot->GetAttackers())
    {
        if (!attacker || !attacker->IsAlive() || attacker->GetVictim() != bot)
            continue;
        uint32_t n = 0;
        for (HostileReference* ref : attacker->GetThreatManager().getThreatList())
        {
            Unit* target = ref ? ref->getTarget() : nullptr;
            if (target && target != bot && target->IsAlive())
                ++n;
        }
        others.push_back(n);
    }
    // Raid groups: medium threat on a tank-held mob is not a feign (it
    // drops combat while the raid fights). Only a mob on the hunter or
    // near-max threat feigns; dungeons keep the old rule.
    Group* group = bot->GetGroup();
    uint8 threat = AI_VALUE2(uint8, "threat", "current target");
    return TortoiseBots::HunterFeignDeathRaidAllowed(group && group->isRaidGroup(), others, threat);
}
