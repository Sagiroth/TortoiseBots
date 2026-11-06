#pragma once
#include <map>
#include <vector>
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Value.h"
#include "PartyMemberValue.h"

namespace ai
{
    class PartyMemberToHeal : public PartyMemberValue
	{
	public:
        PartyMemberToHeal(PlayerbotAI* ai, std::string name = "party member to heal") :
          PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
        bool CanHealPet(Pet* pet);
        virtual bool Check(Unit* player);

    private:
        std::vector<Player*> GetPartyMembers();
	};

    // The group's tank jobs in one order (RaidTankRolePolicy.h): raid Main
    // Tank flag, then a tank Main Assist, then promoted raid assistants
    // (assist jobs only), then live tanks in slot order.
    // Only alive members on the bot's map and instance count. Without a
    // group the bot is its own main tank when it is a tank.
    struct RaidTankRoles
    {
        std::vector<Player*> tanks; // [0] main tank, [1..] assist tanks
        int botIndex = -1;          // the bot's place in `tanks`, -1 if none

        Player* MainTank() const { return tanks.empty() ? nullptr : tanks.front(); }
        bool BotIsMainTank() const { return botIndex == 0; }
    };

    RaidTankRoles GetRaidTankRoles(PlayerbotAI* ai);

    // Direct heals other group members are casting at each target right now
    // (HealComm-style): base spell amounts of cast-time heals still in their
    // cast bar, keyed by target guid. Computed ONCE per refresh (1 s TTL)
    // over the bot's own group slots — never the spatial grid — so the dozen
    // heal-action checks per tick share one bounded scan instead of each
    // paying grid queries, unit lookups and heap churn. UnGrouped bots see
    // an empty map. Only live group casters count; dead targets collect
    // nothing (a corpse needs no heal credit).
    class IncomingHealsValue : public CalculatedValue<std::map<ObjectGuid, uint32>>
    {
    public:
        IncomingHealsValue(PlayerbotAI* ai, std::string name = "incoming heals") :
            CalculatedValue<std::map<ObjectGuid, uint32>>(ai, name) {}

    protected:
        virtual std::map<ObjectGuid, uint32> Calculate() override;
    };

    // Base amount of the direct heals other group members are casting at
    // `target` right now (cast bar still running). Reads the cached
    // "incoming heals" value, so it costs a map lookup, not a scan.
    uint32 GetIncomingHeal(PlayerbotAI* ai, Unit* target);
    // mod-playerbots PartyMemberMainTankValue (LD-8): the raid/party main
    // tank — explicit raid flag first, else the first live tank. No
    // Misdirection/Tricks consumers exist in 1.18.1; raid tactics read it.
    // Same order as the boss scripts via GetRaidTankRoles.
    class PartyMemberMainTankValue : public PartyMemberValue
    {
    public:
        PartyMemberMainTankValue(PlayerbotAI* ai, std::string name = "main tank") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };

    class PartyMemberToProtect : public PartyMemberValue
    {
    public:
        PartyMemberToProtect(PlayerbotAI* ai, std::string name = "party member to protect") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };

    // Lowest-mana alive group healer (mod-playerbots parity): other agents'
    // innervate / mana-tide rows target this. Skips self (the caster's own
    // mana is covered by the plain low/medium mana triggers) and non-mana
    // users.
    class HealerLowMana : public PartyMemberValue
    {
    public:
        HealerLowMana(PlayerbotAI* ai, std::string name = "healer low mana") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };

    class TankToRemoveRoots : public PartyMemberValue
    {
    public:
        TankToRemoveRoots(PlayerbotAI* ai, std::string name = "tank to remove roots") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };

    class PartyMemberToRemoveRoots : public PartyMemberValue
    {
    public:
        PartyMemberToRemoveRoots(PlayerbotAI* ai, std::string name = "party member to remove roots") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };
}
