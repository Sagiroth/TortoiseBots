// Forward-ported from mod-playerbots QuestConfirmAcceptAction.cpp - modern donor
// Source: mod-playerbots@5397110, Shyalya@1f9497e Tortoise 1.18.1 baseline
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QuestConfirmAcceptAction.h"
#include "WorldPacket.h"

bool QuestConfirmAcceptAction::Execute(Event& event)
{
    WorldPacket packet(event.GetPacket());
    uint32 questId;
    packet >> questId;

    WorldPacket sendPacket(CMSG_QUEST_CONFIRM_ACCEPT);
    sendPacket << questId;
    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    if (!quest || !bot->CanAddQuest(quest, true))
    {
        return false;
    }
    std::ostringstream out;
    out << "Quest: " << chat->formatQuest(quest) << " confirm accept";
    ai->TellPlayerNoFacing(ai->GetMaster(), out.str());
    bot->GetSession()->HandleQuestConfirmAccept(sendPacket);
    return true;
}
