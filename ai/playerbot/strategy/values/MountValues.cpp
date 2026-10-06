#include "MountValues.h"
#include "MountManager.hpp"
#include "playerbot/ChatHelper.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "BudgetValues.h"
#include "SharedValueContext.h"

using namespace ai;

uint32 MountValue::GetSpeed(uint32 spellId)
{
    const SpellEntry* const spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);

    if (!spellInfo)
        return 0;

    switch (spellInfo->Id) //Aura's hard coded in spell.cpp
    {
    case 783:  // travel form
    case 2645: // ghost wolf
        return 39;
    case 26656: //Black AQ mount
        return 99;
    }

    bool isMount = false;
    for (int i = 0; i < 3; i++)
    {
        if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOUNTED)
        {
            isMount = true;
            break;
        }
    }

    if(!isMount)
        return 0;


    uint32 effect = SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED;

    if (isMount)
    {
        for (int i = 0; i < 3; i++)
        {
            if (spellInfo->EffectApplyAuraName[i] == effect)
            {
                return spellInfo->EffectBasePoints[i];
            }
        }
    }

    return 0;
}

// Mirror of the core dynamic-speed rule for mount auras
// (HandleAuraModIncreaseMountedSpeed): SPELL_CUSTOM_MOUNT_SPEED_100 -> 100,
// SPELL_CUSTOM_IGNORE_RIDING_SKILL_MOUNT_SPEED -> static DBC speed, else the
// rider ladder below. Uses the bot's live skill/level; called on the world
// thread from selection/speed values only.
uint32 MountValue::GetDynamicMountSpeed(uint32 spellId, Player* bot)
{
    if (!bot)
        return 0;
    return GetDynamicMountSpeed(spellId, GetRiderMountSpeed(bot));
}

uint32 MountValue::GetRiderMountSpeed(Player* bot)
{
    if (!bot)
        return 0;
    switch (bot->GetSkillValue(SKILL_RIDING))
    {
    case 75:
        return 60;
    case 150:
        return 100;
    case 0:
        // Core applies ceil(level/2) on the uint level: integer division
        // truncates first, so ceil is a no-op (19 -> 9, 39 -> 19).
        return bot->GetLevel() / 2;
    default:
        // Core unmounts here (default branch); report unusable so selection
        // skips the mount instead of looping cast->unmount.
        return 0;
    }
}

uint32 MountValue::GetDynamicMountSpeed(uint32 spellId, uint32 riderSpeed)
{
    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    if (!spellInfo)
        return 0;
    if (spellInfo->Custom & SPELL_CUSTOM_MOUNT_SPEED_100)
        return 100;
    if (spellInfo->Custom & SPELL_CUSTOM_IGNORE_RIDING_SKILL_MOUNT_SPEED)
        return GetSpeed(spellId);
    return riderSpeed;
}

bool MountValue::IsMountSpell(uint32 spellId)
{
    // Travel form and ghost wolf are not mount auras but bots use them as
    // their mount before riding (GetSpeed hardcodes their 39%).
    if (spellId == 783 || spellId == 2645)
        return true;
    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    if (!spellInfo)
        return false;
    // Core definition (HandleAuraModIncreaseMountedSpeed ~3895): the mount
    // aura sits in effect 0. Static speed is never consulted, so 0-speed
    // mounts (30174 Swift Riding Turtle) still count.
    if (spellInfo->EffectApplyAuraName[0] == SPELL_AURA_MOUNTED)
        return true;
    // Legacy fallback: mounts GetSpeed already recognised before the
    // aura-based check (Black Qiraji Battle Tank 26656, whose effect 0 is a
    // dummy dispatching to 25863/26655 via the SpellEffects script, or a
    // mount aura outside effect 0). Keeps them in the mount list and out of
    // vendor/trash logic.
    return GetSpeed(spellId) != 0;
}

uint32 MountValue::GetSpeedFor(Player* bot) const
{
    if (!bot)
        return GetSpeed(spellId);
    return GetSpeedFor(bot, GetRiderMountSpeed(bot));
}

uint32 MountValue::GetSpeedFor(Player* bot, uint32 riderSpeed) const
{
    if (!bot)
        return GetSpeed(spellId);
    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    if (!spellInfo)
        return 0;
    // Hardcoded non-mount forms keep their static speeds (travel form,
    // ghost wolf, AQ mount above); only real mount-aura spells scale.
    bool isMountAura = false;
    for (int i = 0; i < 3; i++)
        if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOUNTED)
            isMountAura = true;
    if (!isMountAura)
        return GetSpeed(spellId);
    return GetDynamicMountSpeed(spellId, riderSpeed);
}

uint32 MountValue::GetMountSpell(uint32 itemId)
{
    // Tortoise collection mounts use a generic item spell (46499) and keep the
    // actual mount spell in the core-owned collection_mount table. Consult
    // that authoritative mapping before falling back to classic item spells.
    if (std::optional<uint32> collectionSpell = sMountMgr.GetMountSpellId(itemId))
        return *collectionSpell;

    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);

    if (!proto)
        return 0;

    for (int j = 0; j < MAX_ITEM_PROTO_SPELLS; j++)
    {
        if (IsMountSpell(proto->Spells[j].SpellId))
            return proto->Spells[j].SpellId;
    }

    return 0;
}

bool MountValue::IsValidLocation(Player* bot)
{
    const SpellEntry* const spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);

    if (!spellInfo)
        return false;

    bool isAQ40Mounted = false;

    switch (spellInfo->Id)
    {
    case 25863:    // spell used by ingame item for Black Qiraji mount (legendary reward)
    case 26655:    // spells also related to Black Qiraji mount but use/trigger unknown
    case 26656:
    case 31700:
        if (bot->GetMapId() == 531)
            isAQ40Mounted = true;
        break;
    case 25953:    // spells of the 4 regular AQ40 mounts
    case 26054:
    case 26055:
    case 26056:
        if (bot->GetMapId() == 531)
        {
            isAQ40Mounted = true;
            break;
        }
        else
            return false; //SPELL_FAILED_NOT_HERE;
    default:
        break;
    }

    // Ignore map check if spell have AreaId. AreaId already checked and this prevent special mount spells
    MapEntry const* mapEntry = sMapStorage.LookupEntry<MapEntry>(bot->GetMapId());
    if (bot->GetTypeId() == TYPEID_PLAYER &&
        !isAQ40Mounted &&   // [-ZERO] && !m_spellInfo->AreaId)
        (!mapEntry || !mapEntry->IsMountAllowed()))
    {
        return false;  //SPELL_FAILED_NO_MOUNTS_ALLOWED;
    }

    if (bot->GetAreaId() == 35)
        return false; //SPELL_FAILED_NO_MOUNTS_ALLOWED;

    return true;
}

uint32 CurrentMountSpeedValue::Calculate()
{
    Unit* unit = AI_VALUE(Unit*, getQualifier());

    if (!unit)
        return 0;

    uint32 mountSpeed = 0;
    // Mount, mounted-speed, and shapeshift auras: travel form 783 / ghost
    // wolf 2645 ride on SPELL_AURA_MOD_SHAPESHIFT, not SPELL_AURA_MOUNTED.
    // Without this the value reports 0 while shifted and Mount() cancels
    // the form, stops, and recasts it every tick.
    static const AuraType kMountAuraTypes[] = { SPELL_AURA_MOUNTED, SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED, SPELL_AURA_MOD_SHAPESHIFT };
    Player* speedBot = (unit->GetTypeId() == TYPEID_PLAYER) ? static_cast<Player*>(unit) : nullptr;
    uint32 riderSpeed = speedBot ? MountValue::GetRiderMountSpeed(speedBot) : 0;
    for (uint32 t = 0; t < sizeof(kMountAuraTypes) / sizeof(kMountAuraTypes[0]); ++t)
    {
        Unit::AuraList const& auras = unit->GetAurasByType(kMountAuraTypes[t]);

        if (auras.empty())
            continue;

        for (Unit::AuraList::const_iterator i = auras.begin(); i != auras.end(); i++)
        {
            Aura* aura = *i;
            if (!aura)
                continue;

            SpellEntry const* auraSpell = aura->GetSpellProto();
            if (!auraSpell)
                continue;
            // Only known mounts/forms report speed; any other buff sharing
            // these types falls back to cheap static speed (0 for non-mounts).
            uint32 auraSpeed = (speedBot && MountValue::IsMountSpell(auraSpell->Id))
                ? MountValue(auraSpell->Id).GetSpeedFor(speedBot, riderSpeed)
                : MountValue::GetSpeed(auraSpell->Id);

            if (auraSpeed < mountSpeed)
                continue;

            mountSpeed = auraSpeed;
        }
    }

    return mountSpeed;
}

std::vector<MountValue> FullMountListValue::Calculate()
{
    std::vector<MountValue> mounts;

    for (uint32 id = 0; id < sItemStorage.GetMaxEntry(); ++id)
    {
        ItemPrototype const* pProto = sItemStorage.LookupEntry<ItemPrototype>(id);
        if (!pProto)
            continue;

        if (!MountValue::GetMountSpell(pProto->ItemId))
            continue;

        mounts.push_back(MountValue(pProto));

        GAI_VALUE2(std::list<int32>, "item vendor list", pProto->ItemId);
    }

    for (uint32 id = 0; id < sSpellTemplate.GetMaxEntry(); ++id)
    {
        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(id);
        if (!spellInfo)
            continue;

        if (!MountValue::IsMountSpell(spellInfo->Id))
            continue;

        mounts.push_back(MountValue(spellInfo->Id));
    }

    return mounts;
}

std::vector<MountValue> MountListValue::Calculate()
{
    std::vector<MountValue> mounts;

	for (auto& mount : AI_VALUE2(std::list<Item*>, "inventory items", "mount"))
		mounts.push_back(MountValue(mount));

    for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
        if (itr->second.state != PLAYERSPELL_REMOVED && !itr->second.disabled && !IsPassiveSpell(itr->first))
            if (MountValue::IsMountSpell(itr->first))
                mounts.push_back(MountValue(itr->first));

    return mounts;
}

uint32 MaxMountSpeedValue::Calculate()
{
    std::vector<MountValue> mounts = AI_VALUE(std::vector<MountValue>, "mount list");

    uint32 maxSpeed = 0;
    uint32 riderSpeed = MountValue::GetRiderMountSpeed(bot);

    for (auto& mount : mounts)
        maxSpeed = std::max(maxSpeed, mount.GetSpeedFor(bot, riderSpeed));

    return maxSpeed;
}

std::string MountListValue::Format()
{
    std::ostringstream out; out << "{";
    uint32 riderSpeed = MountValue::GetRiderMountSpeed(bot);
    for (auto& mount : this->Calculate())
    {
        std::string speed = std::to_string(mount.GetSpeedFor(bot, riderSpeed) + 1) + "%";
        out << (mount.IsItem() ? "(item)" : "(spell)") << chat->formatSpell(mount.GetSpellId()) << "(" << speed << "),";
    }
    out << "}";
    return out.str();
}

uint32 MountSkillTypeValue::Calculate()
{
    switch (bot->GetRace())
    {
    case RACE_HUMAN:
        return SKILL_RIDING_HORSE;
    case RACE_ORC:
        return SKILL_RIDING_WOLF;
        break;
    case RACE_NIGHTELF:
        return SKILL_RIDING_TIGER;
        break;
    case RACE_DWARF:
        return SKILL_RIDING_RAM;
        break;
    case RACE_TROLL:
        return SKILL_RIDING_RAPTOR;
        break;
    case RACE_GNOME:
        return SKILL_RIDING_MECHANOSTRIDER;
        break;
    case RACE_UNDEAD:
        return SKILL_RIDING_UNDEAD_HORSE;
        break;
    case RACE_TAUREN:
        return SKILL_RIDING_KODO;
        break;
    default:
        return SKILL_RIDING;
        break;
    }
}

std::vector<int32> AvailableMountVendors::Calculate()
{
    std::vector<int32> mountVendors;
    std::vector<MountValue> const& mountList = GAI_VALUE_REF(std::vector<MountValue>, "full mount list");

    for (auto& mount : mountList)
    {
        if (!mount.IsItem())
            continue;

        uint32 itemId = mount.GetItemProto()->ItemId;

        ItemUsage usage = AI_VALUE2_LAZY(ItemUsage, "item usage", itemId);

        if (usage != ItemUsage::ITEM_USAGE_EQUIP)
            continue;

        for (auto& vendor : GAI_VALUE2(std::list<int32>, "item vendor list", itemId))
            if(std::find(mountVendors.begin(), mountVendors.end(), vendor) == mountVendors.end())
                mountVendors.push_back(vendor);
    }

    return mountVendors;
}

bool CanTrainMountValue::Calculate()
{
    if (bot->GetSkill(AI_VALUE(uint32, "mount skilltype"), true, true) >= bot->GetSkill(AI_VALUE(uint32, "mount skilltype"), true, true, true))
        return false;

    if (AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::mount) < AI_VALUE2(uint32, "total money needed for", (uint32)NeedMoneyFor::mount))
        return false;

    return true;
}

bool CanBuyMountValue::Calculate()
{
    uint8 minRidingLevel = 40;
    if (bot->GetLevel() < minRidingLevel)
        return false;

    if (!AI_VALUE(bool, "can buy"))
        return false;

    if (AI_VALUE2(uint32, "money needed for", (uint32)NeedMoneyFor::mount) == 0)
        return false;

    if (AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::mount) < AI_VALUE2(uint32, "total money needed for", (uint32)NeedMoneyFor::mount))
        return false;

    return true;
}
