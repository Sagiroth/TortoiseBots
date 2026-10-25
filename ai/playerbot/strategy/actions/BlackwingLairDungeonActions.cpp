#include "playerbot/playerbot.h"
#include "BlackwingLairDungeonActions.h"
#include "playerbot/EbonrocSwapPolicy.h"
#include "playerbot/VaelTankHandoverPolicy.h"
#include "playerbot/NefarianAddAssignmentPolicy.h"
#include "playerbot/strategy/values/NefarianAddAssignmentValue.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/strategy/values/TargetValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <algorithm>
#include <map>
#include <mutex>

using namespace ai;

namespace
{
bool EbonrocHoldOrder(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    return ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet());
}

class NefarianCcReservation : public FindNonCcTargetStrategy
{
public:
    explicit NefarianCcReservation(PlayerbotAI* ai) : FindNonCcTargetStrategy(ai) {}
    using FindNonCcTargetStrategy::IsCcTarget;
    void CheckAttacker(Unit*, ThreatManager*) override {}
};

bool IsNefarianAddCcReserved(PlayerbotAI* ai, Unit* target)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    NefarianCcReservation reservations(ai);
    return AI_VALUE(Unit*, "cc target") == target || reservations.IsCcTarget(target);
}

Player* GetNefarianTankVictim(PlayerbotAI* ai, Unit* add)
{
    Player* victim = dynamic_cast<Player*>(add->GetVictim());
    return victim && victim->IsAlive() && ai->IsTank(victim) ? victim : nullptr;
}

bool EbonrocOtherTargetOrder(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    if (!target || !PossibleAttackTargetsValue::IsValid(target, ai->GetBot(),
        sPlayerbotAIConfig.sightDistance, false, false))
        target = AI_VALUE(Unit*, "rti target");
    return target && target != boss && PossibleAttackTargetsValue::IsValid(target, ai->GetBot(),
        sPlayerbotAIConfig.sightDistance, false, false);
}

uint32 ReadyEbonrocTaunt(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    for (char const* name : { "taunt", "growl", "hand of reckoning" })
    {
        uint32 const id = AI_VALUE2(uint32, "spell id", name);
        SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(id);
        if (!id || !spell || !ai->GetBot()->HasSpell(id))
            continue;
        bool taunt = false;
        bool immune = false;
        for (int effect = EFFECT_INDEX_0; effect <= EFFECT_INDEX_2; ++effect)
            if (spell->Effect[effect] == SPELL_EFFECT_ATTACK_ME ||
                (spell->Effect[effect] == SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[effect] == SPELL_AURA_MOD_TAUNT))
            {
                taunt = true;
                immune = immune || boss->IsImmuneToSpellEffect(spell, SpellEffectIndex(effect), false);
            }
        if (taunt && !immune && ai->CanCastSpell(id, boss))
            return id;
    }
    return 0;
}

struct EbonrocSwapRecord
{
    EbonrocSwapWindow window;
    uint32 lastSeen = 0;
};
std::mutex swapMutex;
std::map<std::pair<uint32, uint64>, EbonrocSwapRecord> swaps;
std::mutex vaelSwapMutex;
std::map<std::pair<uint32, uint64>, EbonrocSwapRecord> vaelSwaps;

struct EbonrocSwapRequest
{
    Unit* boss = nullptr;
    uint32 spell = 0;
};

EbonrocSwapRequest GetEbonrocSwapRequest(PlayerbotAI* ai, bool claim)
{
    Unit* boss = GetActiveEbonrocSwapBoss(ai);
    Unit* victim = boss ? boss->GetVictim() : nullptr;
    if (!boss || !victim || !victim->IsAlive())
        return {};
    Aura* shadow = ai->GetAura(23340, victim);
    Player* chosen = nullptr;
    uint32 chosenSpell = 0;
    if (shadow)
        for (Player* member : LiveGroupMembers(ai->GetBot()->GetGroup()))
        {
            if (!member || member == victim || !member->IsAlive() || !member->IsInWorld() ||
                member->IsBeingTeleported() || !member->IsInMap(boss) || !member->IsInCombat() || member->HasAura(23340))
                continue;
            PlayerbotAI* candidate = PlayerbotAIStorage::Instance().GetAI(member);
            if (!candidate || !candidate->IsTank(member) ||
                !candidate->HasStrategy("ebonroc swap", BotState::BOT_STATE_COMBAT) ||
                EbonrocHoldOrder(candidate) || EbonrocOtherTargetOrder(candidate, boss))
                continue;
            uint32 spell = ReadyEbonrocTaunt(candidate, boss);
            if (spell && (!chosen || member->GetObjectGuid() < chosen->GetObjectGuid()))
            {
                chosen = member;
                chosenSpell = spell;
            }
        }
    uint32 const now = WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(swapMutex);
    // One record per live instance/boss, not per bot or debuff application.
    // Expired encounter records are pruned on subsequent coordinator calls.
    for (auto it = swaps.begin(); it != swaps.end();)
        if (now - it->second.lastSeen > 60000)
            it = swaps.erase(it);
        else
            ++it;
    auto& record = swaps[{ boss->GetInstanceId(), boss->GetObjectGuid().GetRawValue() }];
    record.lastSeen = now;
    record.window.ObserveVictim(victim->GetObjectGuid().GetRawValue());
    if (!shadow)
        return {};
    record.window.Begin(victim->GetObjectGuid().GetRawValue(), shadow->GetAuraApplyTime());
    if (!chosen || chosen != ai->GetBot())
        return {};
    bool const ready = claim ? record.window.Claim(ai->GetBot()->GetObjectGuid().GetRawValue(),
        chosen->GetObjectGuid().GetRawValue(), now) : record.window.Ready(now);
    return ready ? EbonrocSwapRequest{ boss, chosenSpell } : EbonrocSwapRequest{};
}

EbonrocSwapRequest GetVaelHandoverRequest(PlayerbotAI* ai, bool claim)
{
    Unit* boss = GetActiveVaelTankHandoverBoss(ai);
    Unit* victim = boss ? boss->GetVictim() : nullptr;
    Player* victimTank = dynamic_cast<Player*>(victim);
    bool const hasAdrenaline = victim && (victim->HasAura(23620) || victim->HasAura(18173));
    if (!boss || !victimTank || !victim->IsAlive() || !ai->IsTank(victimTank) || !hasAdrenaline ||
        !VaelTankHandoverActive(true, true, ai->GetBot()->IsAlive(), ai->GetBot()->IsInCombat(), true, hasAdrenaline))
        return {};

    Player* chosen = nullptr;
    uint32 chosenSpell = 0;
    Group* group = ai->GetBot()->GetGroup();
    if (!group)
        return {};
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == victimTank || !member->IsAlive() || !member->IsInWorld() ||
            member->IsBeingTeleported() || !member->IsInMap(boss) || !member->IsInCombat())
            continue;
        PlayerbotAI* candidate = PlayerbotAIStorage::Instance().GetAI(member);
        if (!candidate || !candidate->IsTank(member) ||
            !candidate->HasStrategy("vael tank handover", BotState::BOT_STATE_COMBAT) ||
            EbonrocHoldOrder(candidate) || EbonrocOtherTargetOrder(candidate, boss))
            continue;
        uint32 const spell = ReadyEbonrocTaunt(candidate, boss);
        if (spell && (!chosen || member->GetObjectGuid() < chosen->GetObjectGuid()))
        {
            chosen = member;
            chosenSpell = spell;
        }
    }

    uint32 const now = WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(vaelSwapMutex);
    for (auto it = vaelSwaps.begin(); it != vaelSwaps.end();)
        if (now - it->second.lastSeen > 60000)
            it = vaelSwaps.erase(it);
        else
            ++it;
    auto& record = vaelSwaps[{ boss->GetInstanceId(), boss->GetObjectGuid().GetRawValue() }];
    record.lastSeen = now;
    record.window.ObserveVictim(victim->GetObjectGuid().GetRawValue());
    Aura* adrenaline = ai->GetAura(23620, victim);
    if (!adrenaline)
        adrenaline = ai->GetAura(18173, victim);
    if (!adrenaline)
        return {};
    record.window.Begin(victim->GetObjectGuid().GetRawValue(), adrenaline->GetAuraApplyTime());
    if (!chosen || chosen != ai->GetBot())
        return {};
    bool const ready = claim ? record.window.Claim(ai->GetBot()->GetObjectGuid().GetRawValue(),
        chosen->GetObjectGuid().GetRawValue(), now) : record.window.Ready(now);
    return ready ? EbonrocSwapRequest{ boss, chosenSpell } : EbonrocSwapRequest{};
}
}

Unit* ai::GetActiveEbonrocSwapBoss(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 469 || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        !bot->IsInCombat() || !ai->IsTank(bot) || !ai->HasStrategy("ebonroc swap", BotState::BOT_STATE_COMBAT) ||
        EbonrocHoldOrder(ai))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 14601 && boss->IsAlive() && boss->IsInCombat() && boss->IsInMap(bot) &&
            !EbonrocOtherTargetOrder(ai, boss))
            return boss;
    }
    return nullptr;
}

Unit* ai::GetActiveVaelTankHandoverBoss(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 469 || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        !bot->IsInCombat() || !ai->HasStrategy("vael tank handover", BotState::BOT_STATE_COMBAT) ||
        EbonrocHoldOrder(ai))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 13020 && boss->IsAlive() && boss->IsInCombat() && boss->IsInMap(bot))
            return boss;
    }
    return nullptr;
}

bool ai::IsNefarianAddTankOptedIn(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    return bot && bot->IsAlive() && bot->IsInWorld() && !bot->IsBeingTeleported() &&
        bot->IsInCombat() && bot->GetMapId() == 469 && ai->IsTank(bot) &&
        ai->HasStrategy("nefarian add tank", BotState::BOT_STATE_COMBAT) && !EbonrocHoldOrder(ai);
}

NefarianAddAssignment NefarianAddAssignmentValue::Calculate()
{
    NefarianAddAssignment assignment;
    if (!IsNefarianAddTankOptedIn(ai))
        return assignment;
    Player* bot = ai->GetBot();

    std::vector<Player*> members;
    if (bot->GetGroup())
    {
        for (Player* member : LiveGroupMembers(bot->GetGroup()))
            if (member->IsAlive() && member->IsInWorld() && !member->IsBeingTeleported() && member->IsInMap(bot))
                members.push_back(member);
    }
    else
        members.push_back(bot);

    // Reuse the lowest-GUID participant's cached plan. Only that bot reads
    // participant target lists and runs the planner; followers select their
    // own targets from its owner map. No persistent Player/Unit pointers.
    PlayerbotAI* planner = ai;
    std::vector<PlayerbotAI*> participants;
    for (Player* member : members)
    {
        PlayerbotAI* memberAI = PlayerbotAIStorage::Instance().GetAI(member);
        if (!IsNefarianAddTankOptedIn(memberAI))
            continue;
        participants.push_back(memberAI);
        if (member->GetObjectGuid() < planner->GetBot()->GetObjectGuid())
            planner = memberAI;
    }
    if (planner != ai)
    {
        assignment = planner->GetAiObjectContext()->GetValue<NefarianAddAssignment>("nefarian add assignment")->Get();
        assignment.targets.clear();
        for (auto const& item : assignment.owners)
            if (item.second == bot->GetObjectGuid())
                assignment.targets.push_back(item.first);
        return assignment;
    }

    Unit* boss = nullptr;
    auto considerBoss = [&](Unit* unit)
    {
        if (unit && unit->GetEntry() == 11583 && unit->IsAlive() && unit->IsInCombat() && unit->IsInMap(bot))
            boss = unit;
    };
    for (Player* member : members)
    {
        considerBoss(member->GetVictim());
        if (PlayerbotAI* memberAI = PlayerbotAIStorage::Instance().GetAI(member))
            considerBoss(memberAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get());
    }

    std::vector<NefarianAddTankCandidate> tanks;
    std::map<std::uint64_t, Unit*> discovered;
    for (PlayerbotAI* memberAI : participants)
    {
        Player* member = memberAI->GetBot();
        auto const possible = memberAI->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
        NefarianAddTankCandidate candidate;
        candidate.guid = member->GetObjectGuid().GetRawValue();
        for (ObjectGuid const& guid : possible)
        {
            Unit* target = memberAI->GetUnit(guid);
            considerBoss(target);
            if (!target || (target->GetEntry() != 14605 && target->GetEntry() != 14668) ||
                !target->IsAlive() || !target->IsInCombat() ||
                !PossibleAttackTargetsValue::IsValid(target, member, sPlayerbotAIConfig.sightDistance, false, false) ||
                IsNefarianAddCcReserved(memberAI, target))
                continue;
            candidate.possibleAdds.push_back(guid.GetRawValue());
            discovered[guid.GetRawValue()] = target;
        }

        Unit* orderedTarget = nullptr;
        ObjectGuid explicitGuid = memberAI->GetAiObjectContext()->GetValue<ObjectGuid>("explicit attack target")->Get();
        if (!explicitGuid.IsEmpty())
        {
            Unit* explicitTarget = memberAI->GetUnit(explicitGuid);
            if (explicitTarget && PossibleAttackTargetsValue::IsValid(explicitTarget, member,
                sPlayerbotAIConfig.sightDistance, false, false))
                orderedTarget = explicitTarget;
        }
        if (!orderedTarget)
            orderedTarget = memberAI->GetAiObjectContext()->GetValue<Unit*>("rti target")->Get();
        if (orderedTarget)
            candidate.lockedTarget = orderedTarget->GetObjectGuid().GetRawValue();
        tanks.push_back(candidate);
    }
    if (!boss || discovered.empty())
        return assignment;

    std::vector<std::uint64_t> adds;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> victimLocks;
    for (auto const& item : discovered)
    {
        adds.push_back(item.first);
        if (Player* victim = GetNefarianTankVictim(ai, item.second))
            victimLocks.push_back({item.first, victim->GetObjectGuid().GetRawValue()});
    }
    auto plan = PlanNefarianAdds(tanks, adds, victimLocks);
    assignment.active = !plan.empty();
    assignment.boss = boss->GetObjectGuid();
    for (auto const& item : plan)
    {
        ObjectGuid const add(item.first);
        ObjectGuid const owner(item.second);
        assignment.owners.push_back({add, owner});
        if (owner == bot->GetObjectGuid())
            assignment.targets.push_back(add);
    }
    return assignment;
}

Unit* ai::GetAssignedNefarianAddTarget(PlayerbotAI* ai, NefarianAddAssignment const& assignment)
{
    if (!assignment.active || !IsNefarianAddTankOptedIn(ai))
        return nullptr;
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current && std::find(assignment.targets.begin(), assignment.targets.end(), current->GetObjectGuid()) != assignment.targets.end() &&
        PossibleAttackTargetsValue::IsValid(current, bot, sPlayerbotAIConfig.sightDistance, false, false) &&
        !IsNefarianAddCcReserved(ai, current) &&
        !PossibleAttackTargetsValue::HasBreakableCC(current, bot) && !PossibleAttackTargetsValue::HasUnBreakableCC(current, bot))
        return current;

    Unit* selected = nullptr;
    for (ObjectGuid const& guid : assignment.targets)
    {
        Unit* target = ai->GetUnit(guid);
        if (!target || !target->IsAlive() || !PossibleAttackTargetsValue::IsValid(target, bot,
            sPlayerbotAIConfig.sightDistance, false, false) ||
            IsNefarianAddCcReserved(ai, target) ||
            PossibleAttackTargetsValue::HasBreakableCC(target, bot) || PossibleAttackTargetsValue::HasUnBreakableCC(target, bot))
            continue;
        if (!selected || bot->GetDistance(target) < bot->GetDistance(selected))
            selected = target;
    }
    return selected;
}

bool ai::IsNefarianAddTargetExcluded(PlayerbotAI* ai, Unit* target)
{
    if (!target || (target->GetEntry() != 14605 && target->GetEntry() != 14668))
        return false;
    if (!IsNefarianAddTankOptedIn(ai))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    auto const assignment = AI_VALUE(NefarianAddAssignment, "nefarian add assignment");
    if (!assignment.active)
        return false;
    auto owner = std::find_if(assignment.owners.begin(), assignment.owners.end(),
        [target](auto const& item) { return item.first == target->GetObjectGuid(); });
    return owner != assignment.owners.end() && !owner->second.IsEmpty() &&
        owner->second != ai->GetBot()->GetObjectGuid();
}

bool EbonrocShadowSwapAction::isUseful()
{
    return GetEbonrocSwapRequest(ai, false).boss != nullptr;
}

bool EbonrocShadowSwapAction::Execute(Event& event)
{
    auto const request = GetEbonrocSwapRequest(ai, true);
    if (!request.boss || !request.spell)
        return false;
    // Claim before casting: another bot cannot race this unconfirmed attempt.
    // A cast return value is not confirmation; the next observed victim is.
    return ai->CastSpell(request.spell, request.boss);
}

bool VaelastraszTankHandoverAction::isUseful()
{
    return GetVaelHandoverRequest(ai, false).boss != nullptr;
}

bool VaelastraszTankHandoverAction::Execute(Event& event)
{
    auto const request = GetVaelHandoverRequest(ai, true);
    if (!request.boss || !request.spell)
        return false;
    return ai->CastSpell(request.spell, request.boss);
}
