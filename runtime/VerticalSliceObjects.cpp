#include "VerticalSliceObjects.h"

#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/generic/DeadStrategy.h"
#include "playerbot/strategy/generic/DpsAssistStrategy.h"
#include "playerbot/strategy/generic/FollowMasterStrategy.h"
#include "playerbot/strategy/generic/MeleeCombatStrategy.h"
#include "playerbot/strategy/generic/NonCombatStrategy.h"
#include "playerbot/strategy/warrior/ArmsWarriorStrategy.h"
#include "playerbot/strategy/warrior/FuryWarriorStrategy.h"
#include "playerbot/strategy/warrior/ProtectionWarriorStrategy.h"
#include "Log.h"
#include "Maps/Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Objects/Player.h"
#include "Spells/SpellMgr.h"

using namespace ai;

namespace
{
Unit* MasterTarget(PlayerbotAI* ai)
{
    Player* master = ai->GetMaster();
    if (!master || !master->IsInWorld()) return nullptr;
    if (Unit* victim = master->GetVictim()) return victim;
    ObjectGuid selected = master->GetSelectionGuid();
    return selected ? sObjectAccessor.GetUnit(*master, selected) : nullptr;
}

class MasterTargetValue final : public CalculatedValue<Unit*>
{
public:
    MasterTargetValue(PlayerbotAI* ai, std::string name) : CalculatedValue<Unit*>(ai, std::move(name), 1) {}
private:
    Unit* Calculate() override { return MasterTarget(ai); }
};

class PercentValue final : public Uint8CalculatedValue, public Qualified
{
public:
    PercentValue(PlayerbotAI* ai, bool rage) : Uint8CalculatedValue(ai, rage ? "rage" : "health"), rage_(rage) {}
private:
    uint8 Calculate() override
    {
        Unit* unit = qualifier == "current target" ? context->GetValue<Unit*>("current target")->Get() : bot;
        if (!unit) return 0;
        if (!rage_) return static_cast<uint8>(unit->GetHealthPercent());
        return unit == bot ? static_cast<uint8>(bot->GetPower(POWER_RAGE) / 10) : 0;
    }
    bool rage_;
};

class SpellIdValue final : public Uint32CalculatedValue, public Qualified
{
public:
    SpellIdValue(PlayerbotAI* ai) : Uint32CalculatedValue(ai, "spell id", 10) {}
private:
    uint32 Calculate() override
    {
        uint32 result = 0;
        for (auto const& known : bot->GetSpellMap())
        {
            if (known.second.state == PLAYERSPELL_REMOVED || known.second.disabled) continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(known.first);
            if (!spell || IsPassiveSpell(known.first)) continue;
            std::string name = spell->SpellName[0];
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name == qualifier && (!result || sSpellMgr.IsHighRankOfSpell(known.first, result))) result = known.first;
        }
        return result;
    }
};

class SpellUsefulValue final : public BoolCalculatedValue, public Qualified
{
public:
    SpellUsefulValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "spell cast useful", 1) {}
private:
    bool Calculate() override
    {
        Unit* target = context->GetValue<Unit*>("current target")->Get();
        return target && ai->CanCastSpell(qualifier, target, 0, nullptr, true);
    }
};

class UpdateFollowTrigger final : public Trigger
{
public:
    UpdateFollowTrigger(PlayerbotAI* ai, std::string name, float distance) : Trigger(ai, std::move(name), 1), distance_(distance) {}
    bool IsActive() override
    {
        Player* master = ai->GetMaster();
        return master && master->IsInWorld() && bot->GetMap() == master->GetMap() && bot->GetDistance2d(master) > distance_;
    }
private:
    float distance_;
};

class StopFollowTrigger final : public Trigger
{
public:
    StopFollowTrigger(PlayerbotAI* ai) : Trigger(ai, "stop follow", 1) {}
    bool IsActive() override { return !ai->GetMaster() || !ai->GetMaster()->IsInWorld(); }
};

class MasterTargetTrigger final : public Trigger
{
public:
    MasterTargetTrigger(PlayerbotAI* ai) : Trigger(ai, "master target active", 1) {}
    bool IsActive() override
    {
        Unit* target = MasterTarget(ai);
        return target && target->IsAlive() && ai->GetMaster()->IsInCombat();
    }
};

class StateTrigger final : public Trigger
{
public:
    enum Kind { CombatStart, CombatEnd, Death, Resurrect };
    StateTrigger(PlayerbotAI* ai, std::string name, Kind kind) : Trigger(ai, std::move(name), 1), kind_(kind) {}
    bool IsActive() override
    {
        switch (kind_)
        {
            case CombatStart: return bot->IsAlive() && bot->IsInCombat() && ai->GetState() != BotState::BOT_STATE_COMBAT;
            case CombatEnd:
            {
                Unit* target = context->GetValue<Unit*>("current target")->Get();
                Player* master = ai->GetMaster();
                return bot->IsAlive() && !bot->IsInCombat() && (!master || !master->IsInCombat()) &&
                    (!target || !target->IsAlive()) && ai->GetState() == BotState::BOT_STATE_COMBAT;
            }
            case Death: return !bot->IsAlive() && ai->GetState() != BotState::BOT_STATE_DEAD;
            case Resurrect: return bot->IsAlive() && ai->GetState() == BotState::BOT_STATE_DEAD;
        }
        return false;
    }
private:
    Kind kind_;
};

class CurrentTargetTrigger final : public Trigger
{
public:
    CurrentTargetTrigger(PlayerbotAI* ai, std::string name, bool absent) : Trigger(ai, std::move(name), 1), absent_(absent) {}
    bool IsActive() override
    {
        Unit* target = context->GetValue<Unit*>("current target")->Get();
        return absent_ ? (!target || !target->IsAlive()) : (target && target->IsAlive() && !bot->CanReachWithMeleeAutoAttack(target));
    }
private:
    bool absent_;
};

class HealthTrigger final : public Trigger
{
public:
    HealthTrigger(PlayerbotAI* ai, std::string name, bool target) : Trigger(ai, std::move(name), 1), target_(target) {}
    bool IsActive() override
    {
        Unit* unit = target_ ? context->GetValue<Unit*>("current target")->Get() : bot;
        return unit && unit->GetHealthPercent() < 20.0f;
    }
private:
    bool target_;
};

class WarriorSpellTrigger final : public Trigger
{
public:
    WarriorSpellTrigger(PlayerbotAI* ai, std::string spell) : Trigger(ai, spell, 1), spell_(std::move(spell)) {}
    bool IsActive() override
    {
        Unit* target = context->GetValue<Unit*>("current target")->Get();
        return target && ai->CanCastSpell(spell_, target, 0, nullptr, true);
    }
private:
    std::string spell_;
};

class FollowAction final : public Action
{
public:
    FollowAction(PlayerbotAI* ai) : Action(ai, "follow") {}
    bool Execute(Event& event) override
    {
        Player* master = ai->GetMaster();
        if (!master || bot->GetMap() != master->GetMap() || !ai->CanMove()) return false;
        bot->GetMotionMaster()->MoveFollow(master, 1.5f, 0.0f);
        if (!logged_ || event.getSource() == "combat end")
        {
            sLog.outString("TortoiseBots AI: Strategy=follow Trigger=update follow Action=follow bot=%s master=%s", bot->GetName(), master->GetName());
            logged_ = true;
        }
        return true;
    }
    bool isUseful() override
    {
        Player* master = ai->GetMaster();
        bool useful = master && bot->GetMap() == master->GetMap() && bot->GetDistance2d(master) > 1.5f;
        if (!useful) logged_ = false;
        return useful;
    }
private:
    bool logged_ = false;
};

class StopFollowAction final : public Action
{
public:
    StopFollowAction(PlayerbotAI* ai) : Action(ai, "stop follow") {}
    bool Execute(Event&) override { bot->GetMotionMaster()->Clear(); return true; }
};

class AssistAction final : public Action
{
public:
    AssistAction(PlayerbotAI* ai) : Action(ai, "dps assist") {}
    bool Execute(Event&) override
    {
        Unit* target = MasterTarget(ai);
        if (!target || !target->IsAlive() || !bot->IsValidAttackTarget(target)) return false;
        context->GetValue<Unit*>("current target")->Set(target);
        bot->SetSelectionGuid(target->GetObjectGuid());
        bool attacked = bot->Attack(target, true);
        if (attacked)
        {
            bot->SetInCombatWith(target);
            target->SetInCombatWith(bot);
            ai->OnCombatStarted();
            sLog.outString("TortoiseBots AI: Strategy=dps assist Trigger=master target active Action=dps assist bot=%s target=%s", bot->GetName(), target->GetName());
        }
        return attacked;
    }
};

class MeleeAction final : public Action
{
public:
    MeleeAction(PlayerbotAI* ai) : Action(ai, "melee") {}
    bool Execute(Event&) override
    {
        Unit* target = context->GetValue<Unit*>("current target")->Get();
        if (!target || !target->IsAlive()) return false;
        if (!bot->CanReachWithMeleeAutoAttack(target)) bot->GetMotionMaster()->MoveChase(target);
        bool attacked = bot->Attack(target, true);
        sLog.outString("TortoiseBots AI: Strategy=arms Action=melee bot=%s target=%s distance=%.1f result=%u",
            bot->GetName(), target->GetName(), bot->GetDistance2d(target), attacked);
        return attacked;
    }
};

class ReachMeleeAction final : public Action
{
public:
    ReachMeleeAction(PlayerbotAI* ai) : Action(ai, "reach melee") {}
    bool Execute(Event&) override
    {
        Unit* target = context->GetValue<Unit*>("current target")->Get();
        if (!target) return false;
        bot->GetMotionMaster()->MoveChase(target);
        sLog.outString("TortoiseBots AI: Strategy=close Trigger=enemy out of melee Action=reach melee bot=%s target=%s distance=%.1f",
            bot->GetName(), target->GetName(), bot->GetDistance2d(target));
        return true;
    }
};

class StateAction final : public Action
{
public:
    StateAction(PlayerbotAI* ai, std::string name, BotState state) : Action(ai, std::move(name)), state_(state) {}
    bool Execute(Event&) override
    {
        if (state_ == BotState::BOT_STATE_COMBAT) ai->OnCombatStarted();
        else if (state_ == BotState::BOT_STATE_NON_COMBAT) ai->OnCombatEnded();
        else ai->OnDeath();
        return true;
    }
private:
    BotState state_;
};

class RealArmsWarriorStrategy final : public ArmsWarriorStrategy
{
public:
    RealArmsWarriorStrategy(PlayerbotAI* ai) : ArmsWarriorStrategy(ai) {}
    std::string getName() override { return "arms"; }
};

class RealFuryWarriorStrategy final : public FuryWarriorStrategy
{
public:
    RealFuryWarriorStrategy(PlayerbotAI* ai) : FuryWarriorStrategy(ai) {}
    std::string getName() override { return "fury"; }
};

class RealProtectionWarriorStrategy final : public ProtectionWarriorStrategy
{
public:
    RealProtectionWarriorStrategy(PlayerbotAI* ai) : ProtectionWarriorStrategy(ai) {}
    std::string getName() override { return "protection"; }
};

class WarriorSpellAction final : public Action
{
public:
    WarriorSpellAction(PlayerbotAI* ai, std::string spell) : Action(ai, spell), spell_(std::move(spell)) {}
    bool Execute(Event&) override
    {
        Unit* target = context->GetValue<Unit*>("current target")->Get();
        uint32 spellId = context->GetValue<uint32>("spell id", spell_)->Get();
        if (target) bot->SetInFront(target);
        bool cast = target && ai->CastSpell(spellId, target);
        if (cast) SetDuration(sPlayerbotAIConfig.globalCoolDown);
        sLog.outString("TortoiseBots AI: Warrior Action=%s spell=%u bot=%s target=%s hp=%u result=%u", spell_.c_str(), spellId,
            bot->GetName(), target ? target->GetName() : "none", target ? target->GetHealth() : 0, cast);
        return cast;
    }
    bool isPossible() override
    {
        Unit* target = context->GetValue<Unit*>("current target")->Get();
        return target && ai->CanCastSpell(spell_, target, 0, nullptr, true);
    }
private:
    std::string spell_;
};
}

VerticalStrategyContext::VerticalStrategyContext()
{
    creators["follow"] = [](PlayerbotAI* ai) { return new FollowMasterStrategy(ai); };
    creators["close"] = [](PlayerbotAI* ai) { return new MeleeCombatStrategy(ai); };
    creators["nc"] = [](PlayerbotAI* ai) { return new NonCombatStrategy(ai); };
    creators["dps assist"] = [](PlayerbotAI* ai) { return new DpsAssistStrategy(ai); };
    creators["dead"] = [](PlayerbotAI* ai) { return new DeadStrategy(ai); };
}

VerticalActionContext::VerticalActionContext()
{
    creators["follow"] = [](PlayerbotAI* ai) { return new FollowAction(ai); };
    creators["stop follow"] = [](PlayerbotAI* ai) { return new StopFollowAction(ai); };
    creators["dps assist"] = [](PlayerbotAI* ai) { return new AssistAction(ai); };
    creators["melee"] = [](PlayerbotAI* ai) { return new MeleeAction(ai); };
    creators["reach melee"] = [](PlayerbotAI* ai) { return new ReachMeleeAction(ai); };
    creators["set combat state"] = [](PlayerbotAI* ai) { return new StateAction(ai, "set combat state", BotState::BOT_STATE_COMBAT); };
    creators["set non combat state"] = [](PlayerbotAI* ai) { return new StateAction(ai, "set non combat state", BotState::BOT_STATE_NON_COMBAT); };
    creators["set dead state"] = [](PlayerbotAI* ai) { return new StateAction(ai, "set dead state", BotState::BOT_STATE_DEAD); };
}

VerticalTriggerContext::VerticalTriggerContext()
{
    creators["master target active"] = [](PlayerbotAI* ai) { return new MasterTargetTrigger(ai); };
    creators["update follow"] = [](PlayerbotAI* ai) { return new UpdateFollowTrigger(ai, "update follow", 1.5f); };
    creators["out of free move range"] = [](PlayerbotAI* ai) { return new UpdateFollowTrigger(ai, "out of free move range", 8.0f); };
    creators["stop follow"] = [](PlayerbotAI* ai) { return new StopFollowTrigger(ai); };
    creators["combat start"] = [](PlayerbotAI* ai) { return new StateTrigger(ai, "combat start", StateTrigger::CombatStart); };
    creators["combat end"] = [](PlayerbotAI* ai) { return new StateTrigger(ai, "combat end", StateTrigger::CombatEnd); };
    creators["death"] = [](PlayerbotAI* ai) { return new StateTrigger(ai, "death", StateTrigger::Death); };
    creators["resurrect"] = [](PlayerbotAI* ai) { return new StateTrigger(ai, "resurrect", StateTrigger::Resurrect); };
    creators["not dps target active"] = [](PlayerbotAI* ai) { return new CurrentTargetTrigger(ai, "not dps target active", true); };
    creators["enemy out of melee"] = [](PlayerbotAI* ai) { return new CurrentTargetTrigger(ai, "enemy out of melee", false); };
    creators["critical health"] = [](PlayerbotAI* ai) { return new HealthTrigger(ai, "critical health", false); };
    creators["target critical health"] = [](PlayerbotAI* ai) { return new HealthTrigger(ai, "target critical health", true); };
}

VerticalValueContext::VerticalValueContext()
{
    creators["current target"] = [](PlayerbotAI* ai) { return new UnitManualSetValue(ai, nullptr, "current target"); };
    creators["master target"] = [](PlayerbotAI* ai) { return new MasterTargetValue(ai, "master target"); };
    creators["follow target"] = [](PlayerbotAI* ai) { return new MasterTargetValue(ai, "follow target"); };
    creators["health"] = [](PlayerbotAI* ai) { return new PercentValue(ai, false); };
    creators["rage"] = [](PlayerbotAI* ai) { return new PercentValue(ai, true); };
    creators["spell id"] = [](PlayerbotAI* ai) { return new SpellIdValue(ai); };
    creators["spell cast useful"] = [](PlayerbotAI* ai) { return new SpellUsefulValue(ai); };
    creators["combat start time"] = [](PlayerbotAI* ai) { return new TimeManualSetValue(ai, 0, "combat start time"); };
}

WarriorVerticalStrategyContext::WarriorVerticalStrategyContext() : NamedObjectContext<Strategy>(false, true)
{
    creators["arms"] = [](PlayerbotAI* ai) { return new RealArmsWarriorStrategy(ai); };
    creators["fury"] = [](PlayerbotAI* ai) { return new RealFuryWarriorStrategy(ai); };
    creators["protection"] = [](PlayerbotAI* ai) { return new RealProtectionWarriorStrategy(ai); };
}

WarriorVerticalActionContext::WarriorVerticalActionContext()
{
    static char const* spells[] = {"heroic strike", "rend", "execute", "overpower", "mortal strike", "whirlwind", "bloodthirst", "slam", "revenge", "shield slam", "charge"};
    for (char const* spell : spells)
        creators[spell] = [spell](PlayerbotAI* ai) { return new WarriorSpellAction(ai, spell); };
}

WarriorVerticalTriggerContext::WarriorVerticalTriggerContext()
{
    static char const* spells[] = {"heroic strike", "rend", "execute", "overpower", "mortal strike", "whirlwind", "bloodthirst", "slam", "revenge", "shield slam", "charge"};
    for (char const* spell : spells)
        creators[spell] = [spell](PlayerbotAI* ai) { return new WarriorSpellTrigger(ai, spell); };
}
