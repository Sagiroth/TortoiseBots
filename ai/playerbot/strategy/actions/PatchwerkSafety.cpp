#include "playerbot/playerbot.h"
#include "PatchwerkSafety.h"
#include "PatchwerkSoakerActions.h"
#include "playerbot/PatchwerkSafetyPolicy.h"
#include "playerbot/PatchwerkSoakerPolicy.h"
#include "playerbot/RaidTankHealing.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Trigger.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <sstream>

using namespace ai;
namespace {
bool HealerSupport(PlayerbotAI* ai)
{
    // The preventive heal loop runs before the ordinary engine. Preserve its
    // existing mana and threat-survival checks between casts, not at OOM.
    Event event("patchwerk healer support");
    struct Support { const char* trigger; const char* action; bool potion; };
    for (auto const& support : {
        Support{"fade ready and target of attacker", "fade", false},
        Support{"fade ready and priest fade threat", "fade", false},
        Support{"inner focus for heal", "inner focus", false},
        Support{"high mana", "mana potion", true},
        Support{"medium mana", "dark rune", true}})
    {
        if (support.potion && !ai->HasStrategy("potions", BotState::BOT_STATE_COMBAT)) continue;
        Trigger* trigger = ai->GetAiObjectContext()->GetTrigger(support.trigger);
        if (trigger && trigger->IsActive() && ai->DoSpecificAction(support.action, event, true)) return true;
    }
    return false;
}
bool Assigned(Player* player)
{
    auto* peer = PlayerbotAIStorage::Instance().GetAI(player);
    return peer && peer->IsTank(player) &&
        (peer->HasStrategy("patchwerk main", BotState::BOT_STATE_COMBAT) ||
         peer->HasStrategy("patchwerk soak", BotState::BOT_STATE_COMBAT));
}
bool Live(Unit* boss, Player* bot)
{
    return boss && boss->GetEntry() == 16028 && boss->IsInWorld() && boss->IsAlive() &&
        boss->IsInCombat() && boss->IsInMap(bot) && boss->IsCreature() &&
        !static_cast<Creature*>(boss)->IsInEvadeMode();
}
std::vector<Player*> Tanks(PlayerbotAI* ai, Unit* boss)
{
    std::vector<Player*> tanks;
    for (Player* member : LiveGroupMembers(ai->GetBot()->GetGroup()))
        if (member->IsAlive() && member->IsInWorld() && member->IsInMap(boss) && Assigned(member))
            tanks.push_back(member);
    std::sort(tanks.begin(), tanks.end(), [](Player* a, Player* b) { return a->GetObjectGuid() < b->GetObjectGuid(); });
    return tanks;
}
struct Opening { ObjectGuid boss, main; uint32_t started = 0; };
using Key = std::pair<uint32_t,uint32_t>;
std::mutex openingMutex;
std::map<Key, Opening> openings;
Key KeyFor(Player* bot) { return {bot->GetInstanceId(), bot->GetGroup()->GetId()}; }
uint32_t Started(Player* bot)
{
    std::lock_guard<std::mutex> lock(openingMutex);
    auto it = openings.find(KeyFor(bot));
    return it == openings.end() ? WorldTimer::getMSTime() : it->second.started;
}
void LogPatchwerk(PlayerbotAI* ai, char const* event, Unit* target, std::string const& text)
{
    if (sPlayerbotAIConfig.hasLog("bot_events.csv"))
        sPlayerbotAIConfig.logEvent(ai, event, target ? target->GetObjectGuid() : ObjectGuid(), text);
}
}

Unit* ai::PatchwerkSafetyBoss(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 533 || !bot->IsInWorld() || !bot->GetGroup()) return nullptr;
    Unit* boss = bot->GetVictim();
    bool armed = false;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member->IsInWorld() || !member->IsInMap(bot)) continue;
        armed = armed || Assigned(member);
        if (Live(boss, bot)) continue;
        if (Live(member->GetVictim(), bot)) boss = member->GetVictim();
        for (Unit* attacker : member->GetAttackers())
            if (Live(attacker, bot)) { boss = attacker; break; }
    }
    return armed && Live(boss, bot) ? boss : nullptr;
}

Player* ai::PatchwerkSafetyMain(PlayerbotAI* ai, Unit* boss)
{
    if (!boss) return nullptr;
    auto tanks = Tanks(ai, boss);
    Player* selected = nullptr;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(openingMutex);
        auto& state = openings[KeyFor(ai->GetBot())];
        if (state.boss != boss->GetObjectGuid())
            state = {boss->GetObjectGuid(), ObjectGuid(), WorldTimer::getMSTime()};
        std::vector<PatchwerkTankState> candidates;
        for (Player* tank : tanks) candidates.push_back({tank->GetObjectGuid().GetRawValue(), boss->GetThreatManager().getThreat(tank)});
        uint64_t next = PatchwerkChooseMain(state.main.GetRawValue(),
            boss->GetVictim() ? boss->GetVictim()->GetObjectGuid().GetRawValue() : 0, candidates);
        for (Player* tank : tanks) if (tank->GetObjectGuid().GetRawValue() == next) selected = tank;
        ObjectGuid guid = selected ? selected->GetObjectGuid() : ObjectGuid();
        changed = guid != state.main;
        state.main = guid;
    }
    if (changed) LogPatchwerk(ai, "PatchwerkOwner", selected, "first tank holder or surviving tank takeover");
    return selected;
}

Player* ai::PatchwerkHealingTarget(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!ai->IsHeal(bot) || !bot->IsAlive()) return nullptr;
    Unit* boss = PatchwerkSafetyBoss(ai);
    if (!boss) return nullptr;
    auto tanks = Tanks(ai, boss);
    if (tanks.empty()) return nullptr;
    if (Player* main = PatchwerkSafetyMain(ai, boss))
        std::stable_sort(tanks.begin(), tanks.end(), [main](Player* a, Player* b) { return a == main && b != main; });
    std::vector<Player*> healers;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
        if (member->IsAlive() && member->IsInWorld() && member->IsInMap(bot) && ai->IsHeal(member)) healers.push_back(member);
    std::sort(healers.begin(), healers.end(), [](Player* a, Player* b) { return a->GetObjectGuid() < b->GetObjectGuid(); });
    auto pos = std::find(healers.begin(), healers.end(), bot);
    return pos == healers.end() ? nullptr : tanks[PatchwerkHealerSeat(pos - healers.begin(), tanks.size())];
}

bool ai::PatchwerkSafetyHold(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!bot->IsAlive() || ai->IsHeal(bot)) return false;
    Unit* boss = PatchwerkSafetyBoss(ai);
    if (!boss) return false;
    Player* main = PatchwerkSafetyMain(ai, boss);
    if (!main) return !ai->IsTank(bot);
    if (!ai->IsTank(bot) && boss->GetVictim() != main) return true;
    if (ai->IsTank(bot))
        return Assigned(bot) && bot != main && boss->GetThreatManager().getThreat(main) > 1000 &&
            boss->CanReachWithMeleeSpellAttack(bot) &&
            boss->GetThreatManager().getThreat(bot) >= 0.90f * boss->GetThreatManager().getThreat(main);
    auto tanks = Tanks(ai, boss);
    float low = boss->GetThreatManager().getThreat(main);
    unsigned present = 0;
    for (Player* tank : tanks)
        if (boss->CanReachWithMeleeSpellAttack(tank))
        { ++present; low = std::min(low, boss->GetThreatManager().getThreat(tank)); }
    auto* latch = ai->GetAiObjectContext()->GetValue<bool>("manual bool", "patchwerk threat held");
    bool hold = !PatchwerkDamageBudget(boss->GetThreatManager().getThreat(bot),
        boss->GetThreatManager().getThreat(main), low, present,
        WorldTimer::getMSTime() - Started(bot), latch->Get(), ai->IsRanged(bot) ? 1u : 4u);
    if (hold != latch->Get())
    {
        std::ostringstream detail;
        detail << "hold=" << hold << "|mine=" << boss->GetThreatManager().getThreat(bot)
            << "|holder=" << boss->GetThreatManager().getThreat(main) << "|lowest_tank=" << low << "|tanks=" << present;
        LogPatchwerk(ai, "PatchwerkThreatGate", boss, detail.str());
        latch->Set(hold);
    }
    return hold;
}

bool ai::PatchwerkSafetyMeleeHold(PlayerbotAI* ai, Unit* boss)
{
    if (!boss || ai->IsTank(ai->GetBot()) || ai->IsHeal(ai->GetBot()) || ai->IsRanged(ai->GetBot()) || !PatchwerkSafetyBoss(ai)) return false;
    auto tanks = Tanks(ai, boss);
    Player* main = PatchwerkSafetyMain(ai, boss);
    if (tanks.size() != 4 || !main || boss->GetVictim() != main) return true;
    std::vector<uint64_t> soakers;
    SpellEntry const* strike = sServerFacade.LookupSpellInfo(28308);
    if (!strike) return true;
    for (Player* tank : tanks)
    {
        if (!boss->CanReachWithMeleeSpellAttack(tank) || tank->IsImmuneToSpell(strike, false)) return true;
        if (tank != main) soakers.push_back(tank->GetObjectGuid().GetRawValue());
    }
    std::vector<PatchwerkThreatPlayer> players;
    for (HostileReference const* ref : boss->GetThreatManager().getThreatList())
    {
        if (!ref->getUnitGuid().IsPlayer()) continue;
        Player* player = boss->GetMap()->GetPlayer(ref->getUnitGuid());
        if (!player || !player->IsInMap(boss)) continue;
        bool eligible = player->IsAlive() && boss->CanReachWithMeleeSpellAttack(player) &&
            !player->IsImmuneToSpell(strike, false);
        players.push_back({player->GetObjectGuid().GetRawValue(), eligible});
    }
    return !PatchwerkMeleeSlotsSafe(main->GetObjectGuid().GetRawValue(), soakers, players,
        ai->GetBot()->GetObjectGuid().GetRawValue());
}

bool ai::PatchwerkSafetyBlocksSpell(PlayerbotAI* ai, unsigned spellId, Unit* target)
{
    Unit* boss = PatchwerkSafetyBoss(ai);
    if (!boss || (target && target != boss && target != ai->GetBot() && target != boss->GetVictim())) return false;
    SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
    if (!spell) return false;
    for (unsigned i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (spell->Effect[i] == SPELL_EFFECT_ATTACK_ME || spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_TAUNT) return true;
    if (Assigned(ai->GetBot()))
    {
        Player* main = PatchwerkSafetyMain(ai, boss);
        if (main && main != ai->GetBot() && !IsPositiveSpell(spell))
            return PatchwerkSafetyHold(ai);
        return false;
    }
    return !IsPositiveSpell(spell) && (PatchwerkSafetyHold(ai) || PatchwerkSafetyMeleeHold(ai, boss));
}

bool ai::UpdatePatchwerkSafety(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 533 || !bot->GetGroup()) return false;
    Unit* boss = PatchwerkSafetyBoss(ai);
    if (!boss)
    {
        std::lock_guard<std::mutex> lock(openingMutex);
        openings.erase(KeyFor(bot));
        return false;
    }
    Player* main = PatchwerkSafetyMain(ai, boss);
    if (!bot->IsAlive() || bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL)) return false;
    if (Player* patient = PatchwerkHealingTarget(ai))
    {
        StopUnsafeRaidHealerFiller(ai);
        auto* previous = ai->GetAiObjectContext()->GetValue<Unit*>("party member to heal");
        previous->Set(patient);
        if (bot->IsNonMeleeSpellCasted(true, false, true)) return true;
        if (HealerSupport(ai)) return true;
        if (!bot->IsWithinLOSInMap(patient, true) || bot->GetDistance(patient) > ai->GetRange("heal"))
        {
            Event event("patchwerk tank healing reach");
            return ai->DoSpecificAction("reach party member to heal", event, true);
        }
        if (!boss->CanReachWithMeleeSpellAttack(patient)) return false;
        std::size_t ordinal = 0;
        for (Player* member : LiveGroupMembers(bot->GetGroup()))
            if (member->IsAlive() && member->IsInMap(bot) && ai->IsHeal(member) &&
                member->GetObjectGuid() < bot->GetObjectGuid()) ++ordinal;
        bool const primary = ordinal < Tanks(ai, boss).size();
        if (patient->GetHealthPercent() >= 98 && !primary) return false;
        // One HoT is enough; normal cast validation still checks mana, GCD,
        // spell ownership, range and LOS. Direct heals may start before a hit.
        if (primary && bot->GetClass() == CLASS_PRIEST && !ai->HasAura("renew", patient) &&
            patient->GetHealthPercent() >= 90 && ai->CanCastSpell("renew", patient) && ai->CastSpell("renew", patient))
        { LogPatchwerk(ai, "PatchwerkTankHeal", patient, "preventive renew"); return true; }
        std::string spell = bot->GetClass() == CLASS_PRIEST ? (patient->GetHealthPercent() < 75 ? "flash heal" : "greater heal") :
            bot->GetClass() == CLASS_SHAMAN ? "healing wave" : bot->GetClass() == CLASS_PALADIN ? "holy light" : "healing touch";
        if (ai->CanCastSpell(spell, patient) && ai->CastSpell(spell, patient))
        {
            uint32 highest = 0;
            for (Player* member : LiveGroupMembers(bot->GetGroup()))
                if (member->IsAlive() && member->IsInMap(boss) && !ai->IsTank(member) &&
                    boss->CanReachWithMeleeSpellAttack(member)) highest = std::max(highest, member->GetHealth());
            std::ostringstream detail;
            detail << "spell=" << spell << "|health=" << patient->GetHealth() << "|melee_dps_health=" << highest
                << "|above_dps_margin=" << PatchwerkSoakerHealthSafe(patient->GetHealth(), highest)
                << "|primary=" << primary;
            LogPatchwerk(ai, "PatchwerkTankHeal", patient, detail.str());
            return true;
        }
        return false;
    }
    if (Assigned(bot))
    {
        if (PatchwerkSafetyHold(ai))
            bot->AttackStop();
        return false;
    }
    if (ai->IsTank(bot) || ai->IsHeal(bot)) return false;
    if (PatchwerkSafetyMeleeHold(ai, boss))
    {
        bot->AttackStop();
        Event event("patchwerk soaker health safety");
        ai->DoSpecificAction("patchwerk melee retreat", event, true);
    }
    else if (!ai->IsRanged(bot) && bot->GetVictim() == boss && !bot->IsBehindTarget(boss, false))
    {
        Event event("patchwerk rear melee");
        ai->DoSpecificAction("set behind", event, true);
    }
    return false;
}
