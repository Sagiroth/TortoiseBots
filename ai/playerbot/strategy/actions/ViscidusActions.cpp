#include "playerbot/playerbot.h"
#include "playerbot/CastCancelLog.h"
#include "ViscidusActions.h"
#include "playerbot/ViscidusPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "Objects/DynamicObject.h"
#include "Spell.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <mutex>

using namespace ai;
namespace
{
    ViscidusCombatAction* State(PlayerbotAI* ai)
    {
        return ViscidusEnabled(ai) ? dynamic_cast<ViscidusCombatAction*>(
            ai->GetAiObjectContext()->GetAction("viscidus combat")) : nullptr;
    }
    bool Live(Player* bot, Unit* unit)
    {
        return unit && unit->IsInWorld() && unit->IsAlive() && unit->IsInMap(bot) &&
            (!unit->IsCreature() || !static_cast<Creature*>(unit)->IsInEvadeMode());
    }
    bool Engaged(Player* bot, Unit* boss)
    {
        if (!Live(bot, boss) || !boss->IsInCombat()) return false;
        if (boss->GetVictim() == bot || bot->GetAttackers().count(boss)) return true;
        if (bot->GetGroup())
            for (Player* member : LiveGroupMembers(bot->GetGroup()))
                if (member->IsInMap(bot) && (boss->GetVictim() == member || member->GetAttackers().count(boss)))
                    return true;
        return false;
    }
    bool Frozen(Unit* boss) { return boss && boss->HasAura(25937); }
    bool WaitingCaster(PlayerbotAI* ai)
    {
        return ai->GetBot()->GetClass() == CLASS_MAGE && ai->IsRanged(ai->GetBot());
    }
}

bool ai::ViscidusEnabled(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    return bot && bot->IsInWorld() && bot->IsAlive() && !bot->IsBeingTeleported() && bot->GetMapId() == 531 &&
        ai->HasStrategy("viscidus glob focus", BotState::BOT_STATE_COMBAT);
}

bool ai::IsViscidusShatterTarget(Player* player, Unit* target)
{
    if (!player || !target || !target->IsInMap(player)) return false;
    return ViscidusShatterWindow(player->GetMapId(), target->GetEntry(),
        ViscidusEnabled(GET_PLAYERBOT_AI(player)), target->IsAlive(), target->IsInCombat(), Frozen(target));
}

bool ai::ViscidusBlocksSpell(PlayerbotAI* ai, uint32 spellId, Unit* target)
{
    if (!ViscidusEnabled(ai) || !target || target->GetEntry() != 15299) return false;
    SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    return spell && ViscidusHoldDamage(IsViscidusShatterTarget(ai->GetBot(), target),
        !IsPositiveSpell(spell), spell->School == SPELL_SCHOOL_FROST, WaitingCaster(ai));
}

void ViscidusCombatAction::Refresh()
{
    uint32 now = WorldTimer::getMSTime();
    if (initialized && instance == bot->GetInstanceId() && uint32(now - scanned) < 500) return;
    initialized = true; instance = bot->GetInstanceId(); scanned = now;
    bossGuid.Clear(); globs.clear(); clouds.clear();
    for (uint32 entry : {15299u, 15667u, 15922u})
    {
        std::list<Creature*> found;
        bot->GetCreatureListWithEntryInGrid(found, entry, entry == 15922 ? 45.0f : 120.0f);
        for (Creature* unit : found)
            if (Live(bot, unit))
            {
                if (entry == 15299) bossGuid = unit->GetObjectGuid();
                else if (entry == 15667) globs.push_back(unit->GetObjectGuid());
                else clouds.push_back(unit->GetObjectGuid());
            }
    }
}
Unit* ViscidusCombatAction::Boss()
{
    Refresh();
    Unit* boss = ai->GetUnit(bossGuid);
    if (!Live(bot, boss) || !boss->IsInCombat() || encounterGuid != bossGuid)
    { engaged = false; wasSplit = false; retakePending = false; encounterGuid = bossGuid; }
    if (Engaged(bot, boss)) engaged = true;
    // The core briefly has no victim while rebuilding the wiped threat list.
    return engaged && Live(bot, boss) && boss->IsInCombat() ? boss : nullptr;
}
Unit* ViscidusCombatAction::Target()
{
    Unit* boss = Boss();
    if (!boss || ai->IsHeal(bot)) return nullptr;
    Unit* best = nullptr;
    Unit* retained = nullptr;
    Unit* held = AI_VALUE(Unit*, "current target");
    for (ObjectGuid guid : globs)
    {
        Unit* glob = ai->GetUnit(guid);
        if (!ViscidusDpsGlob(true, true, ai->IsTank(bot), false, Live(bot, glob),
            glob && bot->IsValidAttackTarget(glob) && bot->GetDistance(glob) <= sPlayerbotAIConfig.sightDistance)) continue;
        if (glob == held) retained = glob;
        // Urgency comes from distance to the merge point. Within one short
        // tier, prefer the bot's local lane instead of sending the entire ring
        // across the room after a tiny difference in spawn coordinates.
        if (!best || ViscidusGlobBefore(glob->GetDistance(boss), bot->GetDistance(glob), glob->GetObjectGuid().GetRawValue(),
            best->GetDistance(boss), bot->GetDistance(best), best->GetObjectGuid().GetRawValue())) best = glob;
    }
    if (best && retained && ViscidusMergeTier(retained->GetDistance(boss)) == ViscidusMergeTier(best->GetDistance(boss))) return retained;
    if (best) return best;
    if (boss->GetVisibility() == VISIBILITY_OFF || boss->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING) ||
        !bot->IsValidAttackTarget(boss)) return nullptr;
    return boss;
}
Unit* ai::GetViscidusCombatTarget(PlayerbotAI* ai)
{
    auto* state = State(ai);
    return state ? state->Target() : nullptr;
}
std::list<HazardPosition> ViscidusCombatAction::Clouds()
{
    Refresh();
    std::list<HazardPosition> hazards;
    for (ObjectGuid guid : clouds)
    {
        Unit* trigger = ai->GetUnit(guid);
        if (!Live(bot, trigger)) continue;
        DynamicObject* cloud = trigger->GetDynObject(25989);
        if (cloud && cloud->IsInWorld() && cloud->IsInMap(bot) && cloud->GetDuration() && cloud->GetRadius() > 0)
            hazards.emplace_back(WorldPosition(cloud), cloud->GetRadius() + 2.0f);
        else
        {
            // Three-second warning before the first persistent-area cast, and
            // the brief gap before its ten-second refresh. Never infer expiry
            // from the initial cast; the trigger survives for three minutes.
            SpellEntry const* toxin = sSpellTemplate.LookupEntry<SpellEntry>(25989);
            SpellRadiusEntry const* radius = toxin ? sSpellRadiusStore.LookupEntry(toxin->EffectRadiusIndex[EFFECT_INDEX_0]) : nullptr;
            if (radius && radius->Radius > 0) hazards.emplace_back(WorldPosition(trigger), radius->Radius + 2.0f);
        }
    }
    return hazards;
}
std::list<HazardPosition> ai::GetViscidusToxinHazards(PlayerbotAI* ai)
{
    auto* state = State(ai);
    return state ? state->Clouds() : std::list<HazardPosition>{};
}
bool ai::InsideViscidusToxin(PlayerbotAI* ai)
{
    if (!ViscidusEnabled(ai)) return false;
    WorldPosition start(ai->GetBot());
    for (auto const& hazard : GetViscidusToxinHazards(ai))
        if (start.distance(hazard.first) <= hazard.second) return true;
    return false;
}
bool ViscidusToxinEscapeAction::Execute(Event& event)
{
    if (!InsideViscidusToxin(ai)) return false;
    RESET_AI_VALUE(std::list<HazardPosition>, "hazards");
    bool moved = MoveAwayFromHazard::Execute(event);
    sPlayerbotAIConfig.logEvent(ai, moved ? "ViscidusToxinEscape" : "ViscidusToxinBlocked",
        moved ? "leaving toxin" : "no checked escape", "instance=" + std::to_string(bot->GetInstanceId()));
    return moved;
}
bool ViscidusCombatAction::RingPoint(WorldPosition& point)
{
    Unit* boss = Boss();
    if (!boss || ai->IsHeal(bot) || ai->IsTank(bot) || ai->IsRanged(bot) ||
        !(boss->HasAura(26036) || Frozen(boss)) || Target() != boss) { fallbackValid = false; return false; }
    if (fallbackValid && fallbackBoss.distance(WorldPosition(boss)) < 1.0f &&
        !IsHazardNearPosition(fallbackPoint) && bot->CanReachWithMeleeAutoAttackAtPosition(boss,
            fallbackPoint.getX(), fallbackPoint.getY(), fallbackPoint.getZ()))
    { point = fallbackPoint; return true; }
    fallbackValid = false;
    std::vector<uint64> members;
    if (bot->GetGroup())
        for (Player* player : LiveGroupMembers(bot->GetGroup()))
        {
            PlayerbotAI* memberAi = GET_PLAYERBOT_AI(player);
            if (memberAi && player->IsAlive() && player->IsInMap(bot) && ViscidusEnabled(memberAi) &&
                !memberAi->IsHeal(player) && !memberAi->IsTank(player) && !memberAi->IsRanged(player))
                members.push_back(player->GetObjectGuid().GetRawValue());
        }
    if (members.empty()) members.push_back(bot->GetObjectGuid().GetRawValue());
    std::sort(members.begin(), members.end());
    auto slot = std::find(members.begin(), members.end(), bot->GetObjectGuid().GetRawValue());
    if (slot == members.end()) return false;
    float angle = ViscidusRingAngle(unsigned(slot - members.begin()), unsigned(members.size()));
    // Keep swings in reach throughout the 150-hit frozen window. The ring
    // follows the shrinking boss's real combat reach, not its initial size.
    float radius = std::max(1.0f, std::min(12.0f, bot->GetCombatReach(boss, false, 0.0f) - 1.0f));
    point = WorldPosition(boss) + WorldPosition(0, radius * std::cos(angle), radius * std::sin(angle), 0);
    point.setZ(point.GetHeight());
    return std::isfinite(point.getZ()) && bot->CanReachWithMeleeAutoAttackAtPosition(boss,
        point.getX(), point.getY(), point.getZ());
}
bool ViscidusCombatAction::CheckedRoute(WorldPosition const& point)
{
    WorldPosition start(bot);
    bool safe = std::isfinite(point.getZ()) && std::fabs(point.getZ() - start.getZ()) < 3.0f &&
        !IsHazardNearPosition(point) && bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight());
    auto path = safe ? start.GetPathTo(point, bot) : std::vector<WorldPosition>{};
    safe = safe && point.isPathTo(path);
    WorldPosition previous = start;
    float length = 0;
    for (WorldPosition const& next : path)
    {
        float segment = previous.distance(next); length += segment;
        if (!std::isfinite(segment) || length > 60.0f || std::fabs(next.getZ() - start.getZ()) > 3.0f) { safe = false; break; }
        unsigned steps = std::max(1u, unsigned(std::ceil(std::min(segment, 60.0f))));
        for (unsigned i = 1; i <= steps && safe; ++i)
        {
            float t = float(i) / steps;
            WorldPosition sample(531, previous.getX() + t * (next.getX() - previous.getX()),
                previous.getY() + t * (next.getY() - previous.getY()), previous.getZ() + t * (next.getZ() - previous.getZ()));
            if (IsHazardNearPosition(sample)) safe = false;
        }
        previous = next;
    }
    return safe;
}
bool ViscidusCombatAction::RingFallback(WorldPosition& point)
{
    Unit* boss = Boss();
    if (!boss) return false;
    float radius = std::max(1.0f, std::min(12.0f, bot->GetCombatReach(boss, false, 0.0f) - 1.0f));
    std::vector<WorldPosition> candidates;
    for (unsigned i = 0; i < 24; ++i)
    {
        float angle = ViscidusRingAngle(i, 24);
        WorldPosition candidate = WorldPosition(boss) + WorldPosition(0, radius * std::cos(angle), radius * std::sin(angle), 0);
        candidate.setZ(candidate.GetHeight());
        if (std::isfinite(candidate.getZ()) && bot->CanReachWithMeleeAutoAttackAtPosition(boss,
            candidate.getX(), candidate.getY(), candidate.getZ())) candidates.push_back(candidate);
    }
    std::sort(candidates.begin(), candidates.end(), [&](WorldPosition const& a, WorldPosition const& b)
        { return WorldPosition(bot).distance(a) < WorldPosition(bot).distance(b); });
    for (WorldPosition const& candidate : candidates)
        if (CheckedRoute(candidate))
        { point = candidate; fallbackPoint = candidate; fallbackBoss = WorldPosition(boss); fallbackValid = true; return true; }
    return false;
}
bool ViscidusCombatAction::MoveRing()
{
    WorldPosition point;
    if (!RingPoint(point) || WorldPosition(bot).distance(point) < 1.0f) return false;
    bool fallback = false;
    bool safe = CheckedRoute(point);
    if (!safe) { fallback = true; safe = RingFallback(point); }
    bool moved = safe && (WorldPosition(bot).distance(point) < 1.0f || MoveTo(point, false, false, false, false, "viscidus_ring"));
    uint32 now = WorldTimer::getMSTime();
    if (!ringReported || uint32(now - ringReported) > 3000)
    {
        ringReported = now;
        sPlayerbotAIConfig.logEvent(ai, moved ? "ViscidusRing" : "ViscidusRingBlocked",
            moved ? (fallback ? "nearest checked fallback" : "checked melee ring move") : "no checked ring route",
            "instance=" + std::to_string(bot->GetInstanceId()));
    }
    return moved;
}
bool ViscidusCombatAction::KeepDistance()
{
    Unit* boss = Boss();
    if (!boss || ai->IsTank(bot) || !(ai->IsHeal(bot) || ai->IsRanged(bot)) ||
        boss->GetVisibility() == VISIBILITY_OFF || boss->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING)) return false;
    return WorldPosition(bot).distance(WorldPosition(boss)) < boss->GetCombatReach(bot, false, 0.0f) + 5.0f;
}
bool ai::NeedsViscidusDistance(PlayerbotAI* ai)
{
    auto* state = State(ai);
    return state && ai->CanMove() && state->KeepDistance();
}
bool ViscidusDistanceAction::Execute(Event&)
{
    auto* state = State(ai);
    return state && state->Retreat();
}
bool ViscidusCombatAction::Retreat()
{
    Unit* boss = Boss();
    if (!boss || !KeepDistance()) return false;
    float radius = std::max(20.0f, boss->GetCombatReach(bot, false, 0.0f) + 8.0f);
    float angle = std::atan2(bot->GetPositionY() - boss->GetPositionY(), bot->GetPositionX() - boss->GetPositionX());
    bool moved = false;
    for (float offset : {0.0f, 0.52f, -0.52f, 1.04f, -1.04f})
    {
        WorldPosition point = WorldPosition(boss) + WorldPosition(0, radius * std::cos(angle + offset), radius * std::sin(angle + offset), 0);
        point.setZ(point.GetHeight());
        if (!CheckedRoute(point)) continue;
        ai::LogCastCancelNonMelee(ai, "viscidus_retreat", false);
        bot->InterruptNonMeleeSpells(false);
        moved = MoveTo(point, false, false, false, false, "viscidus_retreat");
        if (moved) break;
    }
    uint32 now = WorldTimer::getMSTime();
    if (!distanceReported || uint32(now - distanceReported) > 3000)
    {
        distanceReported = now;
        sPlayerbotAIConfig.logEvent(ai, moved ? "ViscidusDistance" : "ViscidusDistanceBlocked",
            moved ? "leaving boss melee reach" : "no checked retreat", "instance=" + std::to_string(bot->GetInstanceId()));
    }
    return moved;
}
namespace
{
    bool TankVictim(PlayerbotAI* ai, Unit* boss)
    {
        Unit* victim = boss->GetVictim();
        return victim && victim->IsAlive() && victim->IsPlayer() &&
            static_cast<Player*>(victim)->GetGroup() == ai->GetBot()->GetGroup() && ai->IsTank(static_cast<Player*>(victim));
    }
    uint32 ViscidusTaunt(PlayerbotAI* ai, Unit* boss)
    {
        char const* name = ai->GetBot()->GetClass() == CLASS_WARRIOR ? "taunt" :
            ai->GetBot()->GetClass() == CLASS_DRUID ? "growl" : nullptr;
        if (!name) return 0;
        AiObjectContext* context = ai->GetAiObjectContext();
        uint32 id = AI_VALUE2(uint32, "spell id", name);
        SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(id);
        if (!spell || !ai->GetBot()->HasSpell(id) || boss->IsImmuneToSpell(spell, false)) return 0;
        for (unsigned i = 0; i < MAX_EFFECT_INDEX; ++i)
            if ((spell->Effect[i] == SPELL_EFFECT_ATTACK_ME || spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_TAUNT) &&
                boss->IsImmuneToSpellEffect(spell, SpellEffectIndex(i), false)) return 0;
        return ai->CanCastSpell(id, boss) ? id : 0;
    }
    std::mutex retakeMutex;
    std::map<std::pair<uint32, uint64>, uint32> retakeClaims;
}
bool ViscidusCombatAction::TankRecovery(Unit* boss)
{
    if (!boss || !ai->IsTank(bot) || boss->GetVisibility() == VISIBILITY_OFF ||
        boss->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING) || TankVictim(ai, boss)) return false;
    Player* chosen = nullptr;
    uint32 spell = 0;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        PlayerbotAI* memberAi = GET_PLAYERBOT_AI(member);
        if (!ViscidusEnabled(memberAi) || !member->IsInMap(bot) || !memberAi->IsTank(member) ||
            member->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL)) continue;
        uint32 ready = ViscidusTaunt(memberAi, boss);
        if (!chosen || (ready && !spell) || (bool(ready) == bool(spell) && member->GetObjectGuid() < chosen->GetObjectGuid()))
        { chosen = member; spell = ready; }
    }
    if (chosen != bot) return false;
    if (spell)
    {
        uint32 now = WorldTimer::getMSTime();
        {
            std::lock_guard<std::mutex> lock(retakeMutex);
            for (auto it = retakeClaims.begin(); it != retakeClaims.end();)
                if (uint32(now - it->second) > 60000) it = retakeClaims.erase(it); else ++it;
            auto key = std::make_pair(bot->GetInstanceId(), boss->GetObjectGuid().GetRawValue());
            auto previous = retakeClaims.find(key);
            if (previous != retakeClaims.end() && uint32(now - previous->second) < 2000) return false;
            retakeClaims[key] = now;
        }
        bool cast = ai->CastSpell(spell, boss);
        sPlayerbotAIConfig.logEvent(ai, "ViscidusRetakeAttempt", cast ? "taunt cast" : "taunt refused",
            "instance=" + std::to_string(bot->GetInstanceId()) + "|spell=" + std::to_string(spell));
        return cast;
    }
    char const* stance = nullptr;
    if (bot->GetClass() == CLASS_WARRIOR && !bot->HasAura(71)) stance = "defensive stance";
    if (bot->GetClass() == CLASS_DRUID && !bot->HasAura(5487) && !bot->HasAura(9634))
        stance = bot->HasSpell(9634) ? "dire bear form" : "bear form";
    if (stance)
    {
        uint32 id = AI_VALUE2(uint32, "spell id", stance);
        if (id && ai->CanCastSpell(id, bot) && ai->CastSpell(id, bot)) return true;
    }
    return !bot->CanReachWithMeleeAutoAttack(boss) && MoveNear(boss);
}
bool ViscidusCombatAction::GlobBurst(Unit* target)
{
    if (!target || target->GetEntry() != 15667 || ai->IsTank(bot) || ai->IsHeal(bot)) return false;
    auto cast = [&](char const* name, Unit* recipient)
    {
        uint32 id = AI_VALUE2(uint32, "spell id", name);
        SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(id);
        if (!spell || IsChanneledSpell(spell) || GetSpellCastTime(spell, bot) ||
            !ai->CanCastSpell(id, recipient) || !ai->CastSpell(id, recipient)) return false;
        sPlayerbotAIConfig.logEvent(ai, "ViscidusGlobBurst", name,
            "instance=" + std::to_string(bot->GetInstanceId()) + "|spell=" + std::to_string(id) +
            "|glob=" + std::to_string(target->GetObjectGuid().GetRawValue()));
        return true;
    };
    if (bot->GetClass() == CLASS_MAGE)
    {
        uint32 explosion = AI_VALUE2(uint32, "spell id", "arcane explosion");
        SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(explosion);
        float radius = 0;
        if (spell) for (unsigned i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (SpellRadiusEntry const* entry = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                radius = std::max(radius, entry->Radius);
        if (radius <= 0) return false;
        if (bot->GetDistance(target) > radius - 1.0f || !bot->IsWithinLOSInMap(target, true))
            return MoveNear(target, std::max(1.0f, radius - 3.0f));
        Unit* boss = Boss();
        if (boss && target->GetDistance(boss) < 20.0f && !target->HasAuraType(SPELL_AURA_MOD_ROOT))
        {
            uint32 nova = AI_VALUE2(uint32, "spell id", "frost nova");
            SpellEntry const* root = sSpellTemplate.LookupEntry<SpellEntry>(nova);
            float range = 0;
            if (root) for (unsigned i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (SpellRadiusEntry const* entry = sSpellRadiusStore.LookupEntry(root->EffectRadiusIndex[i])) range = std::max(range, entry->Radius);
            if (root && bot->GetDistance(target) < range - 0.5f && !target->IsImmuneToSpell(root, false) && cast("frost nova", bot)) return true;
        }
        return cast("arcane explosion", bot);
    }
    if (bot->GetClass() == CLASS_WARRIOR)
    {
        if (!bot->CanReachWithMeleeAutoAttack(target)) return MoveNear(target);
        if (!bot->HasAura(2458) && cast("berserker stance", bot)) return true;
        if (cast("whirlwind", bot)) return true;
        Unit* boss = Boss();
        if (boss && target->GetDistance(boss) < 20.0f && !target->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) &&
            cast("hamstring", target)) return true;
        // Do not replace an already queued Cleave or spend every tick queuing it.
        if (!bot->GetCurrentSpell(CURRENT_MELEE_SPELL) && cast("cleave", target)) return true;
    }
    return false;
}
bool ViscidusCombatAction::isUseful()
{
    Unit* target = Target();
    if (!target || InsideViscidusToxin(ai)) return false;
    WorldPosition ring;
    if (RingPoint(ring) && WorldPosition(bot).distance(ring) >= 1.0f) return true;
    if (target->GetEntry() == 15667 && (bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARRIOR)) return true;
    if (target->GetEntry() == 15299 && ai->IsTank(bot) && !TankVictim(ai, target)) return true;
    if (target->GetEntry() == 15299 && Frozen(target) && WaitingCaster(ai)) return false;
    if (AI_VALUE(Unit*, "current target") != target || bot->GetVictim() != target) return true;
    if (!ai->IsRanged(bot)) return !bot->CanReachWithMeleeAutoAttack(target) || !bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING);
    return target->GetEntry() == 15299 && !Frozen(target) && bot->GetClass() == CLASS_MAGE && bot->HasSpell(116) &&
        !bot->IsNonMeleeSpellCasted(true);
}
bool ViscidusCombatAction::Execute(Event&)
{
    Unit* target = Target();
    if (!target || !isPossible() || InsideViscidusToxin(ai)) return false;
    if (target->GetEntry() == 15299 && Frozen(target) && WaitingCaster(ai)) return false;
    bool selected = AI_VALUE(Unit*, "current target") != target || bot->GetVictim() != target;
    if (selected)
    {
        ai::LogCastCancelNonMelee(ai, "viscidus_target", false);
        bot->InterruptNonMeleeSpells(false);
        if (!Attack(GetMaster(), target)) return false;
        RESET_AI_VALUE(Unit*, "dps target");
        sPlayerbotAIConfig.logEvent(ai, "ViscidusTarget", "encounter handoff",
            "instance=" + std::to_string(bot->GetInstanceId()) + "|entry=" + std::to_string(target->GetEntry()) +
            "|guid=" + std::to_string(target->GetObjectGuid().GetRawValue()));
    }
    if (target->GetEntry() == 15299 && TankRecovery(target)) return true;
    if (target->GetEntry() == 15667 && GlobBurst(target)) return true;
    if (!ai->IsRanged(bot))
    {
        bot->Attack(target, true);
        if (target->GetEntry() == 15299 && MoveRing()) return true;
        if (!bot->CanReachWithMeleeAutoAttack(target)) return MoveNear(target);
        return selected;
    }
    if (target->GetEntry() == 15299 && bot->GetClass() == CLASS_MAGE && bot->HasSpell(116))
    {
        if (bot->IsNonMeleeSpellCasted(true)) return selected;
        if (bot->GetDistance(target) > 28.0f || !bot->IsWithinLOSInMap(target, true)) return MoveNear(target, 25.0f);
        return ai->CastSpell(116, target); // Hits, not rank damage, advance the core frost counter.
    }
    return selected;
}
void ViscidusCombatAction::Update()
{
    Unit* boss = Boss();
    if (!boss) return;
    bool split = boss->GetVisibility() == VISIBILITY_OFF || boss->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
    if (split) wasSplit = true;
    else if (wasSplit)
    {
        wasSplit = false; retakePending = true; reformAt = WorldTimer::getMSTime();
        ai->SetActionDuration(uint32(0));
        if (ai->IsTank(bot)) sPlayerbotAIConfig.logEvent(ai, "ViscidusReform", "threat reset observed",
            "instance=" + std::to_string(bot->GetInstanceId()));
    }
    if (retakePending && boss->GetVictim() == bot && ai->IsTank(bot))
    {
        retakePending = false;
        sPlayerbotAIConfig.logEvent(ai, "ViscidusRetaken", "tank is actual victim",
            "instance=" + std::to_string(bot->GetInstanceId()) + "|retake_ms=" + std::to_string(uint32(WorldTimer::getMSTime() - reformAt)));
    }
    if (KeepDistance() && ai->CanMove())
    {
        ai::LogCastCancelNonMelee(ai, "viscidus_keep_distance", false);
        bot->InterruptNonMeleeSpells(false);
        ai->SetActionDuration(uint32(0));
        return;
    }
    // Cancel an in-flight frost cast at freeze, but never interrupt a heal.
    for (CurrentSpellTypes type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL})
        if (Spell* spell = bot->GetCurrentSpell(type))
            if ((spell->m_targets.getUnitTargetGuid() == boss->GetObjectGuid() ||
                 AI_VALUE(Unit*, "current target") == boss) &&
                ViscidusBlocksSpell(ai, spell->m_spellInfo->Id, boss))
            {
                // The core's InterruptSpell(type, false) leaves a delayed cast alone: only a real cancel writes a row.
                if (spell->getState() != SPELL_STATE_DELAYED)
                    ai::LogCastCancel(ai, "viscidus_freeze", type);
                bot->InterruptSpell(type, false);
                ai->SetActionDuration(uint32(0));
            }
    Unit* target = Target();
    if (!target || ai->IsHeal(bot) || !isPossible() || InsideViscidusToxin(ai)) return;
    if (target->GetEntry() == 15667 && bot->GetClass() == CLASS_MAGE)
        for (CurrentSpellTypes type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
            if (Spell* spell = bot->GetCurrentSpell(type))
                if (!IsPositiveSpell(spell->m_spellInfo) && (IsChanneledSpell(spell->m_spellInfo) || GetSpellCastTime(spell->m_spellInfo, bot)))
                {
                    // The core's InterruptSpell(type, false) leaves a delayed cast alone: only a real cancel writes a row.
                    if (spell->getState() != SPELL_STATE_DELAYED)
                        ai::LogCastCancel(ai, "viscidus_handoff", type);
                    bot->InterruptSpell(type, false);
                    ai->SetActionDuration(uint32(0));
                }
    bool change = AI_VALUE(Unit*, "current target") != target || bot->GetVictim() != target;
    if (!change) return;
    // Prompt target selection only: movement/casting remain engine actions.
    // An ongoing cast at the boss must not postpone a mage's glob handoff.
    if (target->GetEntry() == 15299 && Frozen(target) && WaitingCaster(ai)) return;
    ai::LogCastCancelNonMelee(ai, "viscidus_handoff_walk", false);
    bot->InterruptNonMeleeSpells(false);
    if (Attack(GetMaster(), target))
    {
        RESET_AI_VALUE(Unit*, "dps target");
        ai->SetActionDuration(uint32(0));
        sPlayerbotAIConfig.logEvent(ai, "ViscidusTarget", "prompt encounter handoff",
            "instance=" + std::to_string(bot->GetInstanceId()) + "|entry=" + std::to_string(target->GetEntry()) +
            "|guid=" + std::to_string(target->GetObjectGuid().GetRawValue()));
    }
}
void ai::UpdateViscidus(PlayerbotAI* ai)
{
    if (auto* state = State(ai)) state->Update();
}
