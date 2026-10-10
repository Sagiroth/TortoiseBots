#include "playerbot/playerbot.h"
#include "RazuviousDungeonActions.h"

using namespace ai;

namespace
{
    constexpr uint32 kRazuviousEntry = 16061;
    constexpr uint32 kUnderstudyEntry = 16803;
    constexpr uint32 kUnderstudyTaunt = 29060;
    constexpr uint32 kBoneBarrier = 29061;
    constexpr uint32 kMindExhaustion = 29051;
    constexpr float kMindControlRange = 20.0f;
}

bool RazuviousMindControlAction::Execute(Event& event)
{
    Unit* boss = nullptr;
    Unit* free = nullptr;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;
        if (unit->GetEntry() == kRazuviousEntry)
            boss = unit;
        else if (unit->GetEntry() == kUnderstudyEntry && !unit->IsCharmed() &&
            !unit->HasAura(kMindExhaustion) &&
            (!free || bot->GetDistance(unit) < bot->GetDistance(free)))
            free = unit;
    }

    if (Unit* charm = bot->GetCharm())
    {
        if (!boss)
            return false;
        if (charm->GetVictim() != boss)
        {
            charm->Attack(boss, true);
            charm->GetMotionMaster()->MoveChase(boss);
        }
        if (charm->GetDistance(boss) > ATTACK_DISTANCE)
            return true;

        uint32 const now = WorldTimer::getMSTime();
        if (WorldTimer::getMSTimeDiff(m_barrierAt, now) >= 30000 || !m_barrierAt)
        {
            charm->CastSpell(charm, kBoneBarrier, true);
            m_barrierAt = now;
        }
        if (boss->GetVictim() != charm &&
            (WorldTimer::getMSTimeDiff(m_tauntAt, now) >= 20000 || !m_tauntAt))
        {
            charm->CastSpell(boss, kUnderstudyTaunt, true);
            m_tauntAt = now;
        }
        return true;
    }

    if (!free || bot->IsNonMeleeSpellCasted(true))
        return false;
    m_tauntAt = m_barrierAt = 0;
    if (bot->GetDistance(free) > kMindControlRange - 2.0f)
        return MoveNear(free, kMindControlRange - 4.0f);
    return ai->CastSpell("mind control", free);
}

float RazuviousFightMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    std::string const name = action->getName();
    if (bot->GetCharm())
        return name == "razuvious mind control" ? 1.0f : 0.0f;
    if (name == "taunt" || name == "growl" || name == "mocking blow" ||
        name == "challenging shout" || name == "challenging roar")
        return 0.0f;
    return 1.0f;
}
