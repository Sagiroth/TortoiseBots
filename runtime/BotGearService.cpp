// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "BotGearService.h"

#include "BotManager.h"
#include "HireProvisionService.h"
#include "PlayerbotAIStorage.h"
#include "policy/hire/HireGearPolicy.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "HireGearCache.h"
#include "../ai/playerbot/ChatHelper.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../ai/playerbot/RandomItemMgr.h"
#include "../host/ModuleLog.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace TortoiseBots
{

namespace
{
// The item GUIDs sitting in the bot's bags (equipment excluded): the
// "existed before this call" set. Whatever the gear pass displaces from the
// equipment lands here afterwards and is untouched by the purge below.
void CollectBagItemGuids(Player* bot, std::set<uint32_t>& out)
{
    out.clear();
    if (!bot)
        return;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            out.insert(item->GetGUIDLow());
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* container = static_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
        if (!container)
            continue;
        for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
            if (Item* item = container->GetItemByPos(static_cast<uint8>(slot)))
                out.insert(item->GetGUIDLow());
    }
}

// Everything in the bags now that was not there before: created by this
// call's AutogearOwned/EquipHireGearSet passes (or displaced off the body by
// them). The purge below may destroy only a GUID in this set that ended up
// unused (still in the bags, above the cap).
void CollectCreatedItemGuids(Player* bot, std::set<uint32_t> const& before, std::set<uint32_t>& created)
{
    created.clear();
    if (!bot)
        return;
    std::set<uint32_t> now;
    CollectBagItemGuids(bot, now);
    for (uint32_t guidLow : now)
        if (!before.count(guidLow))
            created.insert(guidLow);
}

// After gearing, a still-worn weapon/armor above the cap is unequipped to the
// bags (never destroyed), so "no worn weapon/armor above the cap" holds.
// Returns the count moved. A full bag keeps the piece worn (reported, not
// lost): the pin still stops the bot re-equipping it later.
uint32_t UnequipAboveCapWorn(Player* bot, uint32_t cap)
{
    if (!bot || !cap)
        return 0;
    uint32_t moved = 0;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        ItemPrototype const* proto = item ? item->GetProto() : nullptr;
        if (!proto)
            continue;
        if (proto->Class != BotGear::ITEM_CLASS_WEAPON && proto->Class != BotGear::ITEM_CLASS_ARMOR)
            continue;
        if (!BotGear::WornAboveCap(proto->ItemLevel, cap))
            continue;
        ItemPosCountVec dest;
        if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
            continue;
        uint8 bag = item->GetBagSlot();
        uint8 wornSlot = item->GetSlot();
        bot->RemoveItem(bag, wornSlot, true);
        bot->StoreItem(dest, item, true);
        ++moved;
    }
    return moved;
}

} // namespace
BotGearService& BotGearService::Instance()
{
    static BotGearService service;
    return service;
}

bool BotGearService::Queue(uint64_t requesterGuid, std::vector<ObjectGuid> const& bots, BotGear::Args const& args)
{
    if (m_running)
        return false;
    m_job.requesterGuid = requesterGuid;
    m_job.bots = bots;
    m_job.args = args;
    m_job.next = 0;
    m_job.result = JobResult();
    m_job.result.tier = args.tier;
    m_job.result.ilvlCap = args.ilvlCap;
    m_running = true;
    return true;
}

bool BotGearService::IsPinned(uint32_t guidLow) const
{
    return m_pinned.count(guidLow) != 0;
}

uint32_t BotGearService::PinnedCap(uint32_t guidLow) const
{
    auto it = m_pinned.find(guidLow);
    return it != m_pinned.end() ? it->second.ilvlCap : 0;
}

void BotGearService::OnBotLogout(uint32_t guidLow)
{
    m_pinned.erase(guidLow);
}

uint32_t BotGearService::WornMaxIlvl(Player* bot)
{
    uint32_t wornMax = 0;
    if (!bot)
        return wornMax;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            continue;
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        ItemPrototype const* proto = item ? item->GetProto() : nullptr;
        if (proto && proto->ItemLevel > wornMax)
            wornMax = proto->ItemLevel;
    }
    return wornMax;
}

float BotGearService::AverageWornIlvl(Player* bot)
{
    uint32_t sum = 0;
    uint32_t count = 0;
    if (!bot)
        return 0.0f;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            continue;
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        ItemPrototype const* proto = item ? item->GetProto() : nullptr;
        if (!proto)
            continue;
        sum += proto->ItemLevel;
        ++count;
    }
    return count ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
}


void BotGearService::SendLines(uint64_t requesterGuid, std::string const& plain, std::string const& structured)
{
    Player* requester = nullptr;
    if (requesterGuid)
        requester = ObjectAccessor::FindPlayer(ObjectGuid(requesterGuid));
    if (requester && requester->IsInWorld())
    {
        ChatHandler chat(requester);
        chat.PSendSysMessage("%s", plain.c_str());
        chat.PSendSysMessage("%s", structured.c_str());
        return;
    }
    TB_LOG_BASIC("%s", plain.c_str());
    TB_LOG_BASIC("%s", structured.c_str());
}
void BotGearService::Update(uint32_t /*diff*/)
{
    if (!m_running)
        return;
    uint32_t done = 0;
    while (m_job.next < m_job.bots.size() && done < BotGear::kGearBotsPerTick)
    {
        ObjectGuid guid = m_job.bots[m_job.next];
        ++m_job.next;
        ++done;
        BotOutcome outcome;
        outcome.role = "dps";
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot)
        {
            outcome.name = "?";
            outcome.skip = "offline";
            m_job.result.bots.push_back(outcome);
            continue;
        }
        outcome.name = bot->GetName();
        if (!BotManager::Instance().IsBot(guid))
        {
            outcome.skip = "not-bot";
            m_job.result.bots.push_back(outcome);
            continue;
        }
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        uint8_t forcedRole = ai ? ai->GetForcedRole() : 0;
        uint8_t role = forcedRole ? forcedRole : HireProvisionService::DefaultRoleForClass(bot->GetClass());
        outcome.role = role == ai::BOT_ROLE_TANK ? "tank" : (role == ai::BOT_ROLE_HEALER ? "healer" : "dps");
        if (!bot->IsInWorld())
        {
            outcome.skip = "not-in-world";
            m_job.result.bots.push_back(outcome);
            continue;
        }
        if (!bot->IsAlive())
        {
            outcome.skip = "dead";
            m_job.result.bots.push_back(outcome);
            continue;
        }
        if (bot->GetLevel() < 60)
        {
            outcome.skip = "level";
            m_job.result.bots.push_back(outcome);
            continue;
        }
        char const* specKey = ResolveHireGearSpecKey(bot->GetClass(), 0, role);
        bool const hasKit = m_job.args.tier > 0 && specKey &&
            HireGearCache::Instance().HasSetFor(bot->GetClass(), specKey, m_job.args.tier);
        if (m_job.args.tier > 0 && !hasKit)
        {
            outcome.skip = "no-kit";
            m_job.result.bots.push_back(outcome);
            continue;
        }
        if (!ai && m_job.args.tier > 0)
        {
            outcome.skip = "no-ai";
            m_job.result.bots.push_back(outcome);
            continue;
        }
        std::set<uint32_t> before;
        CollectBagItemGuids(bot, before);
        if (m_job.args.ilvlCap)
        {
            PlayerbotFactory factory(bot, bot->GetLevel());
            factory.AutogearOwned(ITEM_QUALITY_EPIC, m_job.args.ilvlCap);
        }
        std::vector<uint8_t> capped;
        if (m_job.args.tier > 0 && ai)
        {
            HireProvisionService::EquipHireGearSet(bot, ai, bot->GetClass(), specKey, bot->GetRace(),
                m_job.args.tier, m_job.args.ilvlCap, m_job.args.enchants, &capped);
        }
        std::set<uint32_t> created;
        CollectCreatedItemGuids(bot, before, created);
        uint32_t wornMax = WornMaxIlvl(bot);
        uint32_t purged = 0;
        uint32_t kept = 0;
        auto purgeSlot = [&](uint8 bag, uint8 slot, Item* item)
        {
            if (!item)
                return;
            ItemPrototype const* proto = item->GetProto();
            if (!proto)
                return;
            if (!BotGear::PurgeBagItem(proto->Class, proto->InventoryType, proto->ItemLevel, m_job.args.ilvlCap, wornMax))
                return;
            uint32_t guidLow = item->GetGUIDLow();
            if (BotGear::CreatedItemUnused(guidLow, created, true))
            {
                bot->DestroyItem(bag, slot, true);
                created.erase(guidLow);
                ++purged;
                return;
            }
            ++kept;
        };
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            purgeSlot(INVENTORY_SLOT_BAG_0, slot, bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            Bag* container = static_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
            if (!container)
                continue;
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                purgeSlot(bag, static_cast<uint8>(slot), container->GetItemByPos(static_cast<uint8>(slot)));
        }
        uint32_t unequipped = UnequipAboveCapWorn(bot, m_job.args.ilvlCap);
        outcome.avgIlvl = AverageWornIlvl(bot);
        outcome.cappedSlots = static_cast<uint32_t>(capped.size());
        outcome.purged = purged;
        outcome.kept = kept;
        outcome.geared = true;
        m_job.result.bots.push_back(outcome);
        bot->SaveToDB();
        m_pinned[guid.GetCounter()] = BotGearPin{m_job.args.tier, m_job.args.ilvlCap};
    }
    if (m_job.next < m_job.bots.size())
        return;
    FinishJob();
}

void BotGearService::FinishJob()
{
    std::vector<BotGear::BotResult> results;
    results.reserve(m_job.result.bots.size());
    for (BotOutcome const& o : m_job.result.bots)
    {
        BotGear::BotResult r;
        r.name = o.name;
        r.role = o.role;
        r.geared = o.geared;
        r.skip = o.skip;
        r.avgIlvl = o.avgIlvl;
        r.cappedSlots = o.cappedSlots;
        r.purged = o.purged;
        results.push_back(r);
    }
    std::string structured = BotGear::SummaryLine(m_job.result.tier, m_job.result.ilvlCap, results);
    uint32_t geared = 0, purged = 0, kept = 0;
    for (BotOutcome const& o : m_job.result.bots)
    {
        geared += o.geared ? 1 : 0;
        purged += o.purged;
        kept += o.kept;
    }
    char plain[256];
    std::snprintf(plain, sizeof(plain), "Geared %u of %u bots: tier %u, ilvl cap %u, %u bag items purged, kept %u items above the cap in the bags.",
        geared, static_cast<uint32_t>(m_job.result.bots.size()),
        static_cast<uint32_t>(m_job.result.tier), m_job.result.ilvlCap, purged, kept);
    SendLines(m_job.requesterGuid, plain, structured);
    m_running = false;
    m_job = ActiveJob();
}

} // namespace TortoiseBots
