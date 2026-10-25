#include "playerbot/playerbot.h"
#include "KurinnaxxActions.h"
#include "AttackAction.h"
#include "GenericSpellActions.h"
#include "playerbot/EbonrocSwapPolicy.h"
#include "playerbot/GarrAoePolicy.h"
#include "playerbot/strategy/KurinnaxxSwapPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <map>
#include <mutex>

using namespace ai;
namespace
{
bool HoldOrder(PlayerbotAI* ai)
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

bool ManualOrder(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    if (target && PossibleAttackTargetsValue::IsValid(target, ai->GetBot(),
        sPlayerbotAIConfig.sightDistance, false, false))
        return true;
    target = AI_VALUE(Unit*, "rti target");
    return target && target != boss && PossibleAttackTargetsValue::IsValid(target, ai->GetBot(),
        sPlayerbotAIConfig.sightDistance, false, false);
}

uint32 ReadyTaunt(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    // The volunteer must already be fighting this boss, not just another add.
    if (AI_VALUE(Unit*, "current target") != boss)
        return 0;
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

unsigned WoundStacks(Player* tank)
{
    Aura* wound = tank ? tank->GetAura(25646, EFFECT_INDEX_0) : nullptr;
    return wound ? wound->GetStackAmount() : 0;
}

bool Participates(PlayerbotAI* ai, Unit* boss)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    return bot && bot->GetMapId() == 509 && bot->IsInWorld() && bot->IsAlive() &&
        !bot->IsBeingTeleported() && bot->IsInCombat() && bot->IsInMap(boss) &&
        ai->IsTank(bot) && ai->HasStrategy("kurinnaxx swap", BotState::BOT_STATE_COMBAT) &&
        !HoldOrder(ai) && !ManualOrder(ai, boss);
}

Unit* ActiveBoss(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 509 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !bot->IsInCombat() || !ai->IsTank(bot) ||
        !ai->HasStrategy("kurinnaxx swap", BotState::BOT_STATE_COMBAT))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 15348 && boss->IsAlive() && boss->IsInCombat() &&
            boss->IsInMap(bot) && Participates(ai, boss))
            return boss;
    }
    return nullptr;
}

Player* ParticipatingVictim(PlayerbotAI* ai, Unit* boss)
{
    Player* victim = boss ? dynamic_cast<Player*>(boss->GetVictim()) : nullptr;
    Group* group = ai->GetBot()->GetGroup();
    if (!victim || !group || victim->GetGroup() != group)
        return nullptr;
    return Participates(PlayerbotAIStorage::Instance().GetAI(victim), boss) ? victim : nullptr;
}

struct SwapRecord { EbonrocSwapWindow window; uint32 lastSeen = 0; };
std::mutex swapMutex;
std::map<std::pair<uint32, uint64>, SwapRecord> swaps;
struct SwapRequest { Unit* boss = nullptr; uint32 spell = 0; };

SwapRequest RequestSwap(PlayerbotAI* ai, bool claim)
{
    Unit* boss = ActiveBoss(ai);
    Player* victim = ParticipatingVictim(ai, boss);
    if (!victim)
        return {};
    unsigned const stacks = WoundStacks(victim);
    if (!KurinnaxxNeedsSwap(stacks, WoundStacks(ai->GetBot())) || victim == ai->GetBot())
        return {};
    Aura* wound = victim->GetAura(25646, EFFECT_INDEX_0);
    Player* chosen = nullptr;
    uint32 chosenSpell = 0;
    for (Player* member : LiveGroupMembers(ai->GetBot()->GetGroup()))
    {
        if (!member || member == victim || !KurinnaxxNeedsSwap(stacks, WoundStacks(member)))
            continue;
        PlayerbotAI* candidate = PlayerbotAIStorage::Instance().GetAI(member);
        if (!Participates(candidate, boss))
            continue;
        uint32 const spell = ReadyTaunt(candidate, boss);
        if (spell && (!chosen || member->GetObjectGuid() < chosen->GetObjectGuid()))
        {
            chosen = member;
            chosenSpell = spell;
        }
    }
    uint32 const now = WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(swapMutex);
    for (auto it = swaps.begin(); it != swaps.end();)
        if (now - it->second.lastSeen > 60000)
            it = swaps.erase(it);
        else
            ++it;
    auto& record = swaps[{boss->GetInstanceId(), boss->GetObjectGuid().GetRawValue()}];
    record.lastSeen = now;
    record.window.Begin(victim->GetObjectGuid().GetRawValue(), wound->GetAuraApplyTime());
    record.window.ObserveVictim(victim->GetObjectGuid().GetRawValue());
    if (!chosen || chosen != ai->GetBot())
        return {};
    bool const ready = claim ? record.window.Claim(chosen->GetObjectGuid().GetRawValue(),
        chosen->GetObjectGuid().GetRawValue(), now) : record.window.Ready(now);
    return ready ? SwapRequest{boss, chosenSpell} : SwapRequest{};
}

bool ShouldHold(PlayerbotAI* ai, Unit* boss)
{
    Player* victim = ParticipatingVictim(ai, boss);
    return KurinnaxxNeedsHold(WoundStacks(ai->GetBot()), victim && victim != ai->GetBot());
}

bool AttackingBoss(Player* bot, Unit* boss)
{
    if (bot->GetVictim() == boss)
        return true;
    for (int type = CURRENT_MELEE_SPELL; type < CURRENT_MAX_SPELL; ++type)
        if (Spell* spell = bot->GetCurrentSpell(CurrentSpellTypes(type)))
            if (spell->m_targets.getUnitTargetGuid() == boss->GetObjectGuid())
                return true;
    return false;
}

class KurinnaxxMultiplier : public Multiplier
{
public:
    KurinnaxxMultiplier(PlayerbotAI* ai) : Multiplier(ai, "kurinnaxx handoff") {}
    float GetValue(Action* action) override
    {
        if (!action || dynamic_cast<CastHealingSpellAction*>(action))
            return 1.0f;
        bool const taunt = EbonrocTauntAction(action->getName());
        bool const offensive = dynamic_cast<AttackAction*>(action) ||
            dynamic_cast<CastSpellAction*>(action) || action->getThreatType() == ActionThreatType::ACTION_THREAT_AOE;
        if (!taunt && !offensive)
            return 1.0f;
        Unit* boss = ActiveBoss(ai);
        if (!boss)
            return 1.0f;
        Player* victim = ParticipatingVictim(ai, boss);
        bool const other = victim && victim != ai->GetBot();
        bool const aoe = GarrIncidentalDamageAction(action->getName(),
            action->getThreatType() == ActionThreatType::ACTION_THREAT_AOE, false, false) ||
            action->getName() == "swipe" || action->getName() == "swipe (bear)" ||
            action->getName() == "challenging shout" || action->getName() == "challenging roar";
        bool const targetsBoss = action->GetTarget() == boss ||
            (action->getName() == "righteous defense" && action->GetTarget() == victim);
        return KurinnaxxBlockAction(taunt, offensive, aoe, targetsBoss,
            KurinnaxxNeedsHold(WoundStacks(ai->GetBot()), other), other) ? 0.0f : 1.0f;
    }
};
}

bool KurinnaxxSwapAction::isUseful() { return RequestSwap(ai, false).boss != nullptr; }
bool KurinnaxxSwapAction::Execute(Event& event)
{
    auto request = RequestSwap(ai, true);
    // Claim before casting; failed/resisted casts are bounded by the shared window.
    return request.boss && request.spell && ai->CastSpell(request.spell, request.boss);
}
bool KurinnaxxHoldAction::isUseful()
{
    Unit* boss = ActiveBoss(ai);
    return boss && ShouldHold(ai, boss) && AttackingBoss(ai->GetBot(), boss);
}
bool KurinnaxxHoldAction::Execute(Event& event)
{
    Unit* boss = ActiveBoss(ai);
    if (!boss || !ShouldHold(ai, boss))
        return false;
    Player* bot = ai->GetBot();
    for (int type = CURRENT_MELEE_SPELL; type < CURRENT_MAX_SPELL; ++type)
        if (Spell* spell = bot->GetCurrentSpell(CurrentSpellTypes(type)))
            if (spell->m_targets.getUnitTargetGuid() == boss->GetObjectGuid())
                bot->InterruptSpell(CurrentSpellTypes(type));
    if (bot->GetVictim() == boss)
        bot->AttackStop();
    return true;
}
bool KurinnaxxSwapTrigger::IsActive() { return KurinnaxxSwapAction(ai).isUseful(); }
bool KurinnaxxHoldTrigger::IsActive() { return KurinnaxxHoldAction(ai).isUseful(); }
void KurinnaxxSwapStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("kurinnaxx hold needed",
        NextAction::array(0, new NextAction("kurinnaxx hold attacks", ACTION_EMERGENCY + 5), NULL)));
    triggers.push_back(new TriggerNode("kurinnaxx swap needed",
        NextAction::array(0, new NextAction("kurinnaxx swap", ACTION_EMERGENCY + 4), NULL)));
}
void KurinnaxxSwapStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new KurinnaxxMultiplier(ai));
}
void KurinnaxxSwapStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}
