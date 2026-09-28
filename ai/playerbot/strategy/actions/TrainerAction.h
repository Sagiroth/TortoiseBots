#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

namespace ai
{
	class TrainerAction : public ChatCommandAction
    {
	public:
		TrainerAction(PlayerbotAI* ai) : ChatCommandAction(ai, "trainer") {}
        virtual bool Execute(Event& event) override;

    private:
        // Per-visit accounting for the fruitless-visit park (see Execute):
        // cheapest trainable spell skipped because it was unaffordable, and how
        // many spells this visit actually learned. Reset before each visit.
        uint32 visitCheapestUnaffordable = UINT32_MAX;
        uint32 visitLearned = 0;

        typedef void (TrainerAction::*TrainerSpellAction)(uint32, ObjectGuid trainerGuid, uint32 spellId, TrainerSpell const*, std::ostringstream& msg);
        bool Iterate(Player* requester, Creature* creature, TrainerSpellAction action, SpellIds& spells);
        void Learn(uint32 cost, ObjectGuid trainerGuid, uint32 spellId, TrainerSpell const* tSpell, std::ostringstream& msg);
        void TellHeader(Player* requester, Creature* creature);
        void TellFooter(Player* requester, uint32 totalCost);
    };
}