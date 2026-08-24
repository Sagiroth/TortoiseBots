#pragma once

#include "Common.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "WorldPosition.h"
#include "AuctionHouse/AuctionHouseMgr.h"

#include <list>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
struct ItemPrototype;

using PlayerBotMap = std::map<uint32, Player*>;

// Narrow behavior adapter for mature strategy code. It is not a lifecycle
// owner: World owns sessions, BotManager owns records/AI, and RandomBotService
// owns population orchestration.
class RandomBotFacade
{
public:
    static RandomBotFacade& instance()
    {
        static RandomBotFacade instance;
        return instance;
    }

    bool IsRandomBot(Player* bot);
    bool IsRandomBot(uint32 guid);
    bool IsFreeBot(Player* bot);
    bool IsFreeBot(uint32 guid);

    PlayerBotMap& GetPlayers() { return players; }
    void SyncNativePlayers();

    uint32 GetValue(Player* bot, std::string type);
    uint32 GetValue(uint32 guid, std::string type);
    int32 GetValueValidTime(uint32 guid, std::string event);
    std::string GetData(uint32 guid, std::string type);
    void SetValue(uint32 guid, std::string type, uint32 value, std::string data = "", int32 validIn = -1);
    void SetValue(Player* bot, std::string type, uint32 value, std::string data = "", int32 validIn = -1);

    double GetBuyMultiplier(Player* bot);
    double GetSellMultiplier(Player* bot);
    uint32 GetTradeDiscount(Player* bot, Player* master);
    void SetTradeDiscount(Player* bot, Player* master, uint32 value);
    void AddTradeDiscount(Player* bot, Player* master, int32 value);

    void Remove(Player* bot);
    void Refresh(Player* bot);
    void UpdateGearSpells(Player* bot);
    bool ProcessBot(Player* player);
    void ChangeStrategy(Player* player);
    void Revive(Player* player);

    bool GetNamedLocation(std::string const& name, WorldLocation& location);
    bool getNamedLocation(std::string const& name, WorldLocation& location)
    {
        return GetNamedLocation(name, location);
    }
    bool IsPinnedBot(uint32 guidLow);
    void PrintTeleportCache();
    void LoadBattleMastersCache();
    void LoadAuctionPrices();

    static InventoryResult CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item);
    const std::map<Team, std::map<BattleGroundTypeId, std::list<uint32>>>& GetBattleMastersCache() const
    {
        return battleMastersCache;
    }

    const std::vector<AuctionEntry>& GetAhPrices(uint32 itemId) const;
    std::mutex m_ahActionMutex;

private:
    RandomBotFacade() = default;
    ~RandomBotFacade() = default;

    PlayerBotMap players;
    std::map<Team, std::map<BattleGroundTypeId, std::list<uint32>>> battleMastersCache;
    std::unordered_map<uint32, std::vector<AuctionEntry>> ahMirror;
};

#define sRandomBotFacade RandomBotFacade::instance()
