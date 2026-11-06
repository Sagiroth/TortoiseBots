#pragma once

#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"
#include "../MagePortalPolicy.h"

namespace ai
{
    // Whisper `portal [city]`: bare lists the mage's learned portals by city,
    // with a city casts that portal where the bot stands. Shares the policy
    // refusal order with `.bot service ... portal` (one function per surface
    // would drift). Fails closed: unknown words stay unhandled so the
    // OwnerOrGm tier never opens a new surface to group members.
    class PortalCommandAction : public ChatCommandAction
    {
    public:
        PortalCommandAction(PlayerbotAI* ai) : ChatCommandAction(ai, "portal") {}

        virtual bool Execute(Event& event) override
        {
            Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
            if (!requester)
                return false;

            std::string param = event.GetParam();
            while (!param.empty() && (param.front() == ' ' || param.front() == '\t'))
                param.erase(param.begin());

            std::vector<uint32_t> known = KnownPortalSpells();
            if (param.empty())
            {
                if (known.empty())
                {
                    ai->TellPlayer(requester, std::string(bot->GetClass() == CLASS_MAGE
                        ? "I know no portal spells yet."
                        : "I am not a mage."));
                    return true;
                }
                std::vector<std::string> cities;
                for (uint32_t id : known)
                    cities.push_back(ai::PortalCityName(id));
                ai->TellPlayer(requester, ai::FormatPortalCityList(cities));
                return true;
            }

            uint32_t spellId = ai::MatchPortalCity(param);
            if (!spellId || !bot->HasSpell(spellId))
            {
                ai->TellPlayer(requester, std::string(ai::PortalRefusalText(
                    !spellId ? ai::PortalRefusal::UnknownCity : ai::PortalRefusal::NotLearned)));
                return true;
            }
            return CastPortal(requester, spellId);
        }

        virtual bool isUsefulWhenStunned() override { return true; }

        // Shared cast body: the policy refusal order first, then the cast
        // through the engine ("cast <spellId>" reaches CastCustomSpellAction,
        // which handles facing, GCD and reagent checks). Returns false only
        // when the engine refused the cast itself.
        bool CastPortal(Player* requester, uint32_t spellId)
        {
            ai::PortalCastFacts facts;
            facts.alive = bot->IsAlive();
            facts.inCombat = bot->IsInCombat();
            facts.castingOrMoving = bot->IsNonMeleeSpellCasted(false) || bot->IsMoving();
            facts.isMage = bot->GetClass() == CLASS_MAGE;
            ai::PortalRefusal refusal = ai::CheckPortalCast(facts, spellId, bot->HasSpell(spellId));
            if (refusal != ai::PortalRefusal::None)
            {
                ai->TellPlayer(requester, std::string(ai::PortalRefusalText(refusal)));
                return true;
            }
            if (!bot->HasItemCount(ai::PortalRuneItemId(), 1))
                bot->StoreNewItemInInventorySlot(ai::PortalRuneItemId(), 1);
            if (!ai->DoSpecificAction("cast", ai::Event("portal", std::to_string(spellId), requester), true))
            {
                ai->TellPlayer(requester, "The portal fizzled; try again.");
                return false;
            }
            std::string confirm = std::string("Opening a portal to ") + ai::PortalCityName(spellId) + ".";
            ai->TellPlayer(requester, confirm);
            return true;
        }

    private:
        std::vector<uint32_t> KnownPortalSpells() const
        {
            std::vector<uint32_t> known;
            size_t count = 0;
            ai::MagePortalSpell const* table = ai::MagePortalTable(&count);
            for (size_t i = 0; i < count; ++i)
                if (bot->HasSpell(table[i].spellId))
                    known.push_back(table[i].spellId);
            return known;
        }
    };
}
