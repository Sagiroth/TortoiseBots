
#include "playerbot/playerbot.h"
#include "PaladinActions.h"

using namespace ai;

bool CastJudgementAction::isUseful()
{
    if (!ai->HasAnyAuraOf(bot, "seal of justice", "seal of command", "seal of righteousness", "seal of light", "seal of wisdom", NULL))
        return false;

    return AI_VALUE2(uint8, "mana", "self target") >= sPlayerbotAIConfig.mediumMana;
}

bool CastConsecrationAction::isUseful()
{
    if (!CastSpellAction::isUseful())
        return false;

    uint8 const mana = AI_VALUE2(uint8, "mana", "self target");
    if (mana < sPlayerbotAIConfig.mediumMana)
        return false;

    // Enemies that would actually stand in it (8yd radius around the paladin).
    uint32 inReach = 0;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* const attacker = ai->GetUnit(guid);
        if (attacker && attacker->IsAlive() && bot->GetDistance(attacker) <= 8.0f)
            ++inReach;
    }

    if (inReach >= 3)
        return true;

    return inReach == 2 && mana >= 70;
}

bool CastPaladinAuraAction::Execute(Event& event)
{
    std::vector<std::string> altAuras;
    std::vector<std::string> haveAuras;
    altAuras.push_back("devotion aura");
    altAuras.push_back("retribution aura");
    altAuras.push_back("concentration aura");
    altAuras.push_back("sanctity aura");
    altAuras.push_back("shadow resistance aura");
    altAuras.push_back("fire resistance aura");
    altAuras.push_back("frost resistance aura");
    altAuras.push_back("crusader aura");

    for (auto aura : altAuras)
    {
        if (AI_VALUE2(uint32, "spell id", aura))
            haveAuras.push_back(aura);
    }

    if (haveAuras.empty())
    {
        return false;
    }

    for (auto aura : haveAuras)
    {
        if (!ai->HasAura(aura, bot))
        {
            uint32 spellDuration = sPlayerbotAIConfig.globalCoolDown;
            bool executed = ai->CastSpell(aura, bot, nullptr, false);
            if (executed)
            {
                SetDuration(1.0f);
            }

            return executed;
        }
    }

    return false;
}

Unit* CastBlessingAction::GetTarget()
{
    return bot;
}

bool CastBlessingAction::isPossible()
{
    Unit* target = GetTarget();
    if (target)
    {
        std::string blessing = GetBlessingForTarget(target);
        if (!blessing.empty())
        {
            SetSpellName(blessing);
            return CastSpellAction::isPossible();
        }
    }

    return false;
}

std::string CastBlessingAction::GetBlessingForTarget(Unit* target)
{
    std::string chosenBlessing = "";
    if (target)
    {
        std::vector<std::string> possibleBlessings = GetPossibleBlessingsForTarget(target);
        for (const std::string& blessing : possibleBlessings)
        {
            const std::string greaterBlessing = "greater " + blessing;
            if (!ai->HasAura(blessing, target) && !ai->HasAura(greaterBlessing, target))
            {
                if ((greater && ai->CanCastSpell(greaterBlessing, target, 0, nullptr, true)) ||
                    (!greater && ai->CanCastSpell(blessing, target, 0, nullptr, true)))
                {
                    chosenBlessing = greater ? greaterBlessing : blessing;
                    break;
                }
            }
        }
    }

    return chosenBlessing;
}

std::vector<std::string> CastPveBlessingAction::GetPossibleBlessingsForTarget(Unit* target) const
{
    std::vector<std::string> blessings;
    if (target && target->IsPlayer())
    {
        Player* player = (Player*)target;
        if (ai->IsTank(player))
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light" };
            }
            else
            {
                blessings = { "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light", "blessing of wisdom" };
            }
        }
        else if (ai->IsHeal(player))
        {
            blessings = { "blessing of wisdom", "blessing of kings", "blessing of light", "blessing of sanctuary", "blessing of might" };
        }
        else
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of might", "blessing of kings", "blessing of light", "blessing of sanctuary" };
            }
            else
            {
                blessings = { "blessing of might", "blessing of kings", "blessing of light", "blessing of wisdom", "blessing of sanctuary" };
            }
        }
    }

    return blessings;
}

std::vector<std::string> CastPvpBlessingAction::GetPossibleBlessingsForTarget(Unit* target) const
{
    std::vector<std::string> blessings;
    if (target && target->IsPlayer())
    {
        Player* player = (Player*)target;
        if (ai->IsTank(player))
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light" };
            }
            else
            {
                blessings = { "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light", "blessing of wisdom" };
            }
        }
        else if (ai->IsHeal(player))
        {
            blessings = { "blessing of wisdom", "blessing of kings", "blessing of light", "blessing of sanctuary", "blessing of might" };
        }
        else
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of might", "blessing of kings", "blessing of light", "blessing of sanctuary" };
            }
            else
            {
                blessings = { "blessing of might", "blessing of kings", "blessing of light", "blessing of wisdom", "blessing of sanctuary" };
            }
        }
    }

    return blessings;
}

std::vector<std::string> CastRaidBlessingAction::GetPossibleBlessingsForTarget(Unit* target) const
{
    std::vector<std::string> blessings;
    if (target && target->IsPlayer())
    {
        Player* player = (Player*)target;
        if (ai->IsTank(player))
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light" };
            }
            else
            {
                blessings = { "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light", "blessing of wisdom" };
            }
        }
        else if (ai->IsHeal(player))
        {
            blessings = { "blessing of wisdom", "blessing of kings", "blessing of light", "blessing of sanctuary", "blessing of might" };
        }
        else
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of might", "blessing of kings", "blessing of light", "blessing of sanctuary" };
            }
            else
            {
                blessings = { "blessing of might", "blessing of kings", "blessing of light", "blessing of wisdom", "blessing of sanctuary" };
            }
        }
    }

    return blessings;
}

Unit* CastBlessingOnPartyAction::GetTarget()
{
    std::vector<std::string> altBlessings;
    std::vector<std::string> haveBlessings;
    altBlessings.push_back("blessing of might");
    altBlessings.push_back("blessing of wisdom");
    altBlessings.push_back("blessing of kings");
    altBlessings.push_back("blessing of sanctuary");
    altBlessings.push_back("blessing of salvation");
    altBlessings.push_back("blessing of light");

    for (auto blessing : altBlessings)
    {
        if (AI_VALUE2(uint32, "spell id", blessing))
        {
            haveBlessings.push_back(blessing);
            haveBlessings.push_back("greater " + blessing);
        }
    }

    if (haveBlessings.empty())
    {
        return nullptr;
    }

    std::string blessList = "";
    for (auto blessing : haveBlessings)
    {
        blessList += blessing;
        if (blessing != haveBlessings[haveBlessings.size() - 1])
        {
            blessList += ",";
        }
    }

    return AI_VALUE2(Unit*, "party member without my aura", blessList);
}

bool CastBlessingOnPartyAction::isUseful()
{
    // Two paladins pick through the same shared "party member without my aura"
    // value: neither sees the other's in-flight cast, so both can resolve the
    // same member the same tick and overwrite each other's blessing. Stand
    // down while another bot holds a live claim on this member for the
    // blessing this bot would cast (BuffClaimRegistry, 4 s TTL).
    Unit* target = GetTarget();
    if (!target)
        return false;
    std::string const blessing = GetBlessingForTarget(target);
    if (!blessing.empty() && BuffClaimRegistry::IsTargetClaimedByOther(bot, target, blessing))
        return false;
    return CastSpellAction::isUseful();
}

bool CastBlessingOnPartyAction::isPossible()
{
    Unit* target = GetTarget();
    if (target)
    {
        std::string blessing = GetBlessingForTarget(target);
        if (!blessing.empty())
        {
            SetSpellName(blessing);
            return CastSpellAction::isPossible();
        }
    }

    return false;
}

bool CastBlessingOnPartyAction::Execute(Event& event)
{
    Unit* const target = GetTarget();
    if (!CastSpellAction::Execute(event))
        return false;
    // Claim only on a cast that actually started: a whiff (range/LOS at cast
    // time) must not stand the other paladin down for the TTL.
    if (target)
        BuffClaimRegistry::Claim(bot->GetObjectGuid(), target->GetObjectGuid(), GetSpellName());
    return true;
}

std::string CastBlessingOnPartyAction::GetBlessingForTarget(Unit* target)
{
    std::string chosenBlessing = "";
    if (target)
    {
        std::vector<std::string> possibleBlessings = GetPossibleBlessingsForTarget(target);
        for (const std::string& blessing : possibleBlessings)
        {
            // Don't cast greater salvation on possible tank classes
            if (greater && blessing == "blessing of salvation" && target->IsPlayer())
            {
                const uint8 playerClass = ((Player*)target)->GetClass();
                if (playerClass == CLASS_PALADIN || playerClass == CLASS_WARRIOR || playerClass == CLASS_DRUID)
                {
                    break;
                }
            }

            const std::string greaterBlessing = "greater " + blessing;
            if (!ai->HasAura(blessing, target) && !ai->HasAura(greaterBlessing, target))
            {
                if ((greater && ai->CanCastSpell(greaterBlessing, target, 0, nullptr, true)) ||
                    (!greater && ai->CanCastSpell(blessing, target, 0, nullptr, true)))
                {
                    chosenBlessing = greater ? greaterBlessing : blessing;
                    break;
                }
            }
        }
    }

    return chosenBlessing;
}

std::vector<std::string> CastPveBlessingOnPartyAction::GetPossibleBlessingsForTarget(Unit* target) const
{
    std::vector<std::string> blessings;
    if (target && target->IsPlayer())
    {
        Player* player = (Player*)target;
        if (ai->IsTank(player))
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light" };
            }
            else
            {
                blessings = { "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light", "blessing of wisdom" };
            }
        }
        else if (ai->IsHeal(player))
        {
            blessings = { "blessing of wisdom", "blessing of kings", "blessing of light", "blessing of sanctuary", "blessing of might" };
        }
        else if (ai->IsRanged(player))
        {
            if (player->GetClass() == CLASS_HUNTER)
            {
                blessings = { "blessing of wisdom", "blessing of kings", "blessing of might", "blessing of light", "blessing of sanctuary" };
            }
            else
            {
                blessings = { "blessing of kings", "blessing of wisdom", "blessing of light", "blessing of sanctuary", "blessing of might" };
            }
        }
        else
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of might", "blessing of kings", "blessing of light", "blessing of sanctuary" };
            }
            else
            {
                blessings = { "blessing of might", "blessing of kings", "blessing of light", "blessing of wisdom", "blessing of sanctuary" };
            }
        }
    }
    else
    {
        // Blessings for pets
        blessings = { "blessing of might", "blessing of kings", "blessing of light", "blessing of sanctuary" };
    }

    return blessings;
}

std::vector<std::string> CastPvpBlessingOnPartyAction::GetPossibleBlessingsForTarget(Unit* target) const
{
    std::vector<std::string> blessings;
    if (target && target->IsPlayer())
    {
        Player* player = (Player*)target;
        if (ai->IsTank(player))
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light" };
            }
            else
            {
                blessings = { "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light", "blessing of wisdom" };
            }
        }
        else if (ai->IsHeal(player))
        {
            blessings = { "blessing of wisdom", "blessing of kings", "blessing of light", "blessing of sanctuary", "blessing of might" };
        }
        else if (ai->IsRanged(player))
        {
            if (player->GetClass() == CLASS_HUNTER)
            {
                blessings = { "blessing of wisdom", "blessing of kings", "blessing of might", "blessing of light", "blessing of sanctuary" };
            }
            else
            {
                blessings = { "blessing of kings", "blessing of wisdom", "blessing of light", "blessing of sanctuary", "blessing of might" };
            }
        }
        else
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of might", "blessing of kings", "blessing of light", "blessing of sanctuary" };
            }
            else
            {
                blessings = { "blessing of might", "blessing of kings", "blessing of light", "blessing of wisdom", "blessing of sanctuary" };
            }
        }
    }
    else
    {
        // Blessings for pets
        blessings = { "blessing of might", "blessing of kings", "blessing of light", "blessing of sanctuary" };
    }

    return blessings;
}

std::vector<std::string> CastRaidBlessingOnPartyAction::GetPossibleBlessingsForTarget(Unit* target) const
{
    std::vector<std::string> blessings;
    if (target && target->IsPlayer())
    {
        Player* player = (Player*)target;
        if (ai->IsTank(player))
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light" };
            }
            else
            {
                blessings = { "blessing of kings", "blessing of might", "blessing of sanctuary", "blessing of light", "blessing of wisdom" };
            }
        }
        else if (ai->IsHeal(player))
        {
            blessings = { "blessing of wisdom", "blessing of kings", "blessing of sanctuary", "blessing of light", "blessing of might" };
        }
        else if (ai->IsRanged(player))
        {
            if (player->GetClass() == CLASS_HUNTER)
            {
                blessings = { "blessing of wisdom", "blessing of kings", "blessing of might", "blessing of light", "blessing of sanctuary" };
            }
            else
            {
                blessings = { "blessing of kings", "blessing of wisdom", "blessing of light", "blessing of sanctuary", "blessing of might" };
            }
        }
        else
        {
            if (player->GetClass() == CLASS_PALADIN)
            {
                blessings = { "blessing of wisdom", "blessing of might", "blessing of kings", "blessing of light", "blessing of sanctuary" };
            }
            else
            {
                blessings = { "blessing of might", "blessing of kings", "blessing of light", "blessing of wisdom", "blessing of sanctuary" };
            }
        }
    }
    else
    {
        // Blessings for pets
        blessings = { "blessing of might", "blessing of kings", "blessing of sanctuary", "blessing of light" };
    }

    return blessings;
}