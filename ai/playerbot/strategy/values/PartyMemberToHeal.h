#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
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

    class PartyMemberToRemoveRoots : public PartyMemberValue
    {
    public:
        PartyMemberToRemoveRoots(PlayerbotAI* ai, std::string name = "party member to remove roots") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };
}
