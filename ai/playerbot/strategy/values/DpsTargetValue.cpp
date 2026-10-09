
#include "playerbot/playerbot.h"
#include "DpsTargetValue.h"
#include "LeastHpTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "playerbot/DpsTargetPolicy.h"
#include "playerbot/GroupMembers.h"

using namespace ai;

namespace
{
    // mod-playerbots DPS target tournament (LD-1/LD-4): lifetime buckets per
    // bot type instead of flat least-HP, CC-moon skip, skull snap. The
    // group-tank follow above stays: vanilla threat punishes independent
    // picks pulling aggro (LD-2 deliberately not ported).
    class DpsTournamentStrategy : public FindNonCcTargetStrategy
    {
    public:
        explicit DpsTournamentStrategy(PlayerbotAI* ai, float groupDps)
            : FindNonCcTargetStrategy(ai), groupDps(groupDps), foundHighPriority(false) {}
        using FindNonCcTargetStrategy::IsCcTarget;

    protected:
        // LD-4 skull snap (donor FindTargetStrategy::IsHighPriority,
        // TargetValue.cpp:124-133): a skull marked mid-fight pulls DPS off
        // the current mob at once; the sticky flag holds it for the rest of
        // the tournament. The `prioritized targets` half has no equivalent
        // here — skull only.
        bool IsHighPriority(Unit* attacker)
        {
            Group* group = ai->GetBot()->GetGroup();
            if (!group)
                return false;
            ObjectGuid guid = group->GetTargetIcon(7);
            return guid && attacker->getObjectGuid() == ObjectGuid(guid);
        }

        bool CheckSkull(Unit* attacker)
        {
            if (foundHighPriority)
                return true;
            if (IsHighPriority(attacker))
            {
                result = attacker;
                foundHighPriority = true;
                return true;
            }
            return false;
        }

        bool IsMoon(Unit* attacker)
        {
            Group* group = ai->GetBot()->GetGroup();
            if (!group)
                return false;
            ObjectGuid guid = group->GetTargetIcon(4);
            return guid && attacker->getObjectGuid() == ObjectGuid(guid);
        }

        float Lifetime(Unit* unit) const
        {
            return groupDps > 0.0f ? (float)unit->GetHealth() / groupDps : 1000000.0f;
        }

        bool InRange(Unit* unit) const
        {
            float range = ai->IsRanged(ai->GetBot())
                ? sPlayerbotAIConfig.spellDistance
                : sPlayerbotAIConfig.meleeDistance;
            return ai->GetBot()->GetDistance(unit) < range + 5.0f;
        }

        Unit* CurrentTarget() const
        {
            return ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        }

        float groupDps;
        bool foundHighPriority;
    };

    // Caster: skip CC-moon, skip <5 s overkill, prefer 5-30 s in range, don't
    // switch when everything is nearly dead (donor lines 53-155).
    class CasterDpsStrategy : public DpsTournamentStrategy
    {
    public:
        CasterDpsStrategy(PlayerbotAI* ai, float groupDps) : DpsTournamentStrategy(ai, groupDps) {}

        void CheckAttacker(Unit* attacker, ThreatManager*) override
        {
            if (!attacker || !attacker->IsAlive() || IsMoon(attacker) || IsCcTarget(attacker))
                return;
            if (CheckSkull(attacker))
                return;
            if (CasterSkipsOverkill(Lifetime(attacker)))
                return;
            if (!result || IsBetter(attacker, result))
                result = attacker;
        }

        bool IsBetter(Unit* newUnit, Unit* oldUnit)
        {
            int newLevel = CasterTargetBucket(Lifetime(newUnit), InRange(newUnit));
            int oldLevel = CasterTargetBucket(Lifetime(oldUnit), InRange(oldUnit));
            if (newLevel != oldLevel)
                return newLevel > oldLevel;

            float newTime = Lifetime(newUnit);
            float oldTime = Lifetime(oldUnit);
            if (newLevel % 10 == 2 || newLevel % 10 == 0)
                return newTime < oldTime;

            Unit* current = CurrentTarget();
            if (current == newUnit)
                return true;
            if (current == oldUnit)
                return false;
            return newTime > oldTime;
        }
    };

    // General: in range + lowest lifetime, else closest (donor lines 158-215).
    class GeneralDpsStrategy : public DpsTournamentStrategy
    {
    public:
        GeneralDpsStrategy(PlayerbotAI* ai, float groupDps) : DpsTournamentStrategy(ai, groupDps) {}

        void CheckAttacker(Unit* attacker, ThreatManager*) override
        {
            if (!attacker || !attacker->IsAlive() || IsMoon(attacker) || IsCcTarget(attacker))
                return;
            if (CheckSkull(attacker))
                return;
            if (!result || IsBetter(attacker, result))
                result = attacker;
        }

        bool IsBetter(Unit* newUnit, Unit* oldUnit)
        {
            int newLevel = GeneralTargetBucket(InRange(newUnit));
            int oldLevel = GeneralTargetBucket(InRange(oldUnit));
            if (newLevel != oldLevel)
                return newLevel > oldLevel;
            if (newLevel == 10)
                return Lifetime(newUnit) < Lifetime(oldUnit);
            return ai->GetBot()->GetDistance(newUnit) < ai->GetBot()->GetDistance(oldUnit);
        }
    };

    // Combo (rogue / cat): stick the combo target, else lowest in range
    // (donor lines 218-279).
    class ComboDpsStrategy : public DpsTournamentStrategy
    {
    public:
        ComboDpsStrategy(PlayerbotAI* ai, float groupDps) : DpsTournamentStrategy(ai, groupDps) {}

        void CheckAttacker(Unit* attacker, ThreatManager*) override
        {
            if (!attacker || !attacker->IsAlive() || IsMoon(attacker) || IsCcTarget(attacker))
                return;
            if (CheckSkull(attacker))
                return;
            if (!result || IsBetter(attacker, result))
                result = attacker;
        }

        bool IsBetter(Unit* newUnit, Unit* oldUnit)
        {
            int newLevel = GeneralTargetBucket(InRange(newUnit));
            int oldLevel = GeneralTargetBucket(InRange(oldUnit));
            if (newLevel != oldLevel)
                return newLevel > oldLevel;

            Player* bot = ai->GetBot();
            if (newLevel == 10)
            {
                Unit* comboUnit = ai->GetUnit(bot->GetComboTargetGuid());
                if (newUnit == comboUnit)
                    return true;
                return Lifetime(newUnit) < Lifetime(oldUnit);
            }
            return bot->GetDistance(newUnit) < bot->GetDistance(oldUnit);
        }
    };

    bool IsComboBot(PlayerbotAI* ai, Player* bot)
    {
        if (bot->GetClass() == CLASS_ROGUE)
            return true;
        return bot->GetClass() == CLASS_DRUID && ai->HasAura("cat form", bot);
    }

    bool IsCasterBot(PlayerbotAI* ai, Player* bot)
    {
        return ai->IsRanged(bot) && bot->GetClass() != CLASS_HUNTER;
    }

    unsigned NearGroupCount(PlayerbotAI* ai, Player* bot)
    {
        unsigned count = 1;
        if (Group* group = bot->GetGroup())
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (!member || member == bot || !member->IsInWorld())
                    continue;
                if (member->GetMapId() != bot->GetMapId())
                    continue;
                if (member->GetDistance(bot) > sPlayerbotAIConfig.sightDistance)
                    continue;
                ++count;
            }
        }
        return count;
    }

    bool IsAttackedByParty(Unit* target, Group* group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (member->GetVictim() == target)
                return true;

            Unit* pet = member->GetPet();
            if (pet && pet->GetVictim() == target)
                return true;
        }

        return false;
    }

    Unit* GetGroupTankTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        Group* group = bot ? bot->GetGroup() : nullptr;
        if (!bot || !group)
            return nullptr;

        PlayerbotAIStorage& storage = PlayerbotAIStorage::Instance();
        Player* master = ai->GetMaster();
        bool useHumanMasterTarget = master && master != bot && master->GetGroup() == group &&
            !storage.GetAI(master) && PlayerbotAI::IsTank(master);
        Player* tank = useHumanMasterTarget ? master : nullptr;

        if (!tank)
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (!storage.GetAI(member) || !PlayerbotAI::IsTank(member))
                    continue;

                tank = member;
                break;
            }
        }

        if (!tank)
            return nullptr;

        Unit* target = useHumanMasterTarget
            ? ai->GetUnit(master->GetSelectionGuid())
            : tank->GetVictim();
        if (!target || !sServerFacade.IsAlive(target) ||
            !PossibleAttackTargetsValue::IsValid(target, bot, sPlayerbotAIConfig.sightDistance, false, false) ||
            !bot->IsWithinLOSInMap(target) ||
            (!target->IsInCombat() && !IsAttackedByParty(target, group)))
            return nullptr;

        return target;
    }
}



Unit* DpsTargetValue::Calculate()
{
    // Explicit orders and raid marks remain ahead of tank assistance.
    if (Unit* explicitTarget = GetExplicitAttackTarget())
        return explicitTarget;

    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    GeneralDpsStrategy ccProbe(ai, 0.0f);
    if (Unit* tankTarget = GetGroupTankTarget(ai))
    {
        if (tankTarget != AI_VALUE(Unit*, "cc target") && !ccProbe.IsCcTarget(tankTarget))
            return tankTarget;
    }

    // mod-playerbots picks the tournament by bot type in groups of 4+
    // (DpsTargetValue.cpp:281-295); small groups always run the general pick.
    float groupDps = AI_VALUE(float, "estimated group dps");
    if (NearGroupCount(ai, bot) > 3 && IsCasterBot(ai, bot))
    {
        CasterDpsStrategy strategy(ai, groupDps);
        return TargetValue::FindTarget(&strategy);
    }
    if (NearGroupCount(ai, bot) > 3 && IsComboBot(ai, bot))
    {
        ComboDpsStrategy strategy(ai, groupDps);
        return TargetValue::FindTarget(&strategy);
    }

    GeneralDpsStrategy strategy(ai, groupDps);
    return TargetValue::FindTarget(&strategy);
}

class FindMaxHpTargetStrategy : public FindTargetStrategy
{
public:
    FindMaxHpTargetStrategy(PlayerbotAI* ai) : FindTargetStrategy(ai)
    {
        maxHealth = 0;
    }

public:
    virtual void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
    {
        Group* group = ai->GetBot()->GetGroup();
        if (group)
        {
            uint64 guid = group->GetTargetIcon(4);
            if (guid && attacker->getObjectGuid() == ObjectGuid(guid))
                return;
        }
        if (!result || result->GetHealth() < attacker->GetHealth())
            result = attacker;
    }

protected:
    float maxHealth;
};

Unit* DpsAoeTargetValue::Calculate()
{
    if (Unit* explicitTarget = GetExplicitAttackTarget())
        return explicitTarget;

    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    FindMaxHpTargetStrategy strategy(ai);
    return TargetValue::FindTarget(&strategy);
}
