
#include "playerbot/playerbot.h"
#include "CheckMailAction.h"
#include "Mail/Mail.h"
#include "MapNodes/MasterPlayer.h"
#include "Guild/GuildMgr.h"
#include "Guild/Guild.h"

#include "playerbot/PlayerbotAIConfig.h"
using namespace ai;

bool CheckMailAction::Execute(Event& event)
{
    WorldPacket p;
    bot->GetSession()->HandleQueryNextMailTime(p);

    MasterPlayer* master = bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
    if (!master)
        return false;

    std::list<uint32> ids;
    PlayerMails mails;

    //Fetch mails first and then loop over them to prevent needing to check mails sent to self.
    for (PlayerMails::iterator i = master->GetMailBegin(); i != master->GetMailEnd(); ++i)
    {
        mails.push_back(*i);
    }

    for (auto & mail : mails)
    {
        if (!mail || mail->state == MAIL_STATE_DELETED)
            continue;

        if (IsGuildMail(mail, bot))
        {
            // A guild member's `disenchant` mail queues for the service
            // instead of the bags, shielded from every disposal path.
            if (TortoiseBots::GuildOrders::IsDisenchantServiceSubject(mail->subject))
            {
                QueueDisenchantMail(mail);
                continue;
            }
            TakeGuildMail(mail);
            continue;
        }

        Player* owner = GetProcessableOwner(mail, bot);
        if (!owner)
            continue;

        ProcessMail(mail, owner);
        ids.push_back(mail->messageID);
        mail->state = MAIL_STATE_DELETED;
    }

    for (std::list<uint32>::iterator i = ids.begin(); i != ids.end(); ++i)
    {
        uint32 id = *i;
        CharacterDatabase.PExecute("DELETE FROM mail WHERE id = '%u'", id);
        CharacterDatabase.PExecute("DELETE FROM mail_items WHERE mail_id = '%u'", id);
        master->RemoveMail(id);
    }

    return true;
}

bool CheckMailAction::isUseful()
{
    MasterPlayer* master = bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
    if (!master || ai->GetMaster() || !master->GetMailSize() || bot->InBattleGround() || bot->IsInCombat())
        return false;

    // Only run when Execute() would act on at least one mail; auction, system, bot-sent and
    // offline-sender mail is never removed and would otherwise re-trigger this action forever.
    for (PlayerMails::iterator i = master->GetMailBegin(); i != master->GetMailEnd(); ++i)
    {
        if (GetProcessableOwner(*i, bot))
            return true;
        if (IsGuildMail(*i, bot) && CanTakeGuildMail(*i, master))
            return true;
    }

    return false;
}

Player* CheckMailAction::GetProcessableOwner(Mail* mail, Player* bot)
{
    if (!mail || mail->state == MAIL_STATE_DELETED)
        return nullptr;

    // Skip auction mail - auction payouts and won items must be collected via MailAction, never deleted!
    if (mail->stationery == MAIL_STATIONERY_AUCTION || mail->messageType != MAIL_NORMAL)
        return nullptr;

    Player* owner = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, mail->sender));
    if (!owner)
        return nullptr;

    uint32 account = sObjectMgr.GetPlayerAccountIdByGUID(owner->getObjectGuid());
    if (sPlayerbotAIConfig.IsInRandomAccountList(account))
        return nullptr;

    if (IsGuildMail(mail, bot))
        return nullptr;

    return owner;
}

// Guild members mail reagents, recipes and supplies to guild bots: the bot
// keeps them (recipes are learned from the bags), whether the sender is
// online or not.
bool CheckMailAction::IsGuildMail(Mail* mail, Player* bot)
{
    if (!mail || mail->state == MAIL_STATE_DELETED || !bot->GetGuildId() ||
        mail->messageType != MAIL_NORMAL || mail->stationery == MAIL_STATIONERY_AUCTION)
        return false;
    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
    return guild && guild->GetMemberSlot(ObjectGuid(HIGHGUID_PLAYER, mail->sender)) != nullptr;
}

// Takes a guild mail's items into the bags without a mailbox trip, as the
// core's take-item handler does; cash-on-delivery mail stays untouched.
// Something to take right now: the money, or the first item when it fits
// the bags. A unique item the bot already holds, or full bags, leave the mail
// alone instead of retrying it every tick.
bool CheckMailAction::CanTakeGuildMail(Mail* mail, MasterPlayer* master)
{
    if (mail->COD || mail->deliver_time > time(nullptr))
        return false;
    if (mail->money)
        return true;
    if (mail->items.empty())
        return false;
    Item* item = master->GetMItem(mail->items[0].item_guid);
    ItemPosCountVec dest;
    return item && bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) == EQUIP_ERR_OK;
}

void CheckMailAction::TakeGuildMail(Mail* mail)
{
    MasterPlayer* master = bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
    if (!master || !CanTakeGuildMail(mail, master))
        return;
    bool taken = false;
    if (mail->money)
    {
        bot->ModifyMoney(int32(mail->money));
        mail->money = 0;
        taken = true;
    }
    while (!mail->items.empty())
    {
        uint32 itemGuid = mail->items[0].item_guid;
        Item* item = master->GetMItem(itemGuid);
        if (!item)
            break;
        ItemPosCountVec dest;
        if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
            break;
        mail->RemoveItem(itemGuid);
        mail->removedItems.push_back(itemGuid);
        master->RemoveMItem(itemGuid);
        item->SetState(ITEM_UNCHANGED);
        bot->MoveItemToInventory(dest, item, true);
        taken = true;
    }
    if (!taken)
        return;
    mail->state = MAIL_STATE_CHANGED;
    master->MarkMailsUpdated();
    CharacterDatabase.BeginTransaction(bot->GetGUIDLow());
    bot->SaveInventoryAndGoldToDB();
    master->SaveMails();
    CharacterDatabase.CommitTransaction();
}

// Takes a guild member's `disenchant` mail into the bags like guild mail,
// but records every item on the service queue first, so the queued items
// stay shielded from every disposal path and the service action knows whom
// to mail back. Cash-on-delivery mail stays untouched like guild mail.
void CheckMailAction::QueueDisenchantMail(Mail* mail)
{
    MasterPlayer* master = bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
    if (!master || !CanTakeGuildMail(mail, master))
        return;
    bool moved = false;
    while (!mail->items.empty())
    {
        uint32_t infoGuid = mail->items[0].item_guid;
        Item* item = master->GetMItem(infoGuid);
        if (!item)
            break;
        ItemPosCountVec dest;
        if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
            break;
        uint32_t itemId = item->GetEntry();
        uint32_t itemGuidLow = item->GetGUIDLow();
        uint32_t senderLow = mail->sender;
        mail->RemoveItem(infoGuid);
        mail->removedItems.push_back(infoGuid);
        master->RemoveMItem(infoGuid);
        item->SetState(ITEM_UNCHANGED);
        bot->MoveItemToInventory(dest, item, true);
        TortoiseBots::GuildOrders::DisenchantQueueEntry entry;
        entry.botGuidLow = bot->GetGUIDLow();
        entry.senderGuidLow = senderLow;
        entry.itemGuidLow = itemGuidLow;
        entry.itemId = itemId;
        TortoiseBots::GuildOrders::NoteDisenchantQueued(entry);
        moved = true;
    }
    if (!moved)
        return;
    Mail* current = master->GetMail(mail->messageID);
    if (!current)
        return;
    if (current->items.empty() && current->money == 0)
    {
        CharacterDatabase.PExecute("DELETE FROM mail WHERE id = '%u'", mail->messageID);
        CharacterDatabase.PExecute("DELETE FROM mail_items WHERE mail_id = '%u'", mail->messageID);
        master->RemoveMail(mail->messageID);
    }
    else
        current->state = MAIL_STATE_CHANGED;
    master->MarkMailsUpdated();
    CharacterDatabase.BeginTransaction(bot->GetGUIDLow());
    bot->SaveInventoryAndGoldToDB();
    master->SaveMails();
    CharacterDatabase.CommitTransaction();
}

void CheckMailAction::ProcessMail(Mail* mail, Player* owner)
{
    MasterPlayer* master = bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
    if (!master || mail->items.empty())
    {
        return;
    }

    if (mail->subject.find("Item(s) you asked for") != std::string::npos)
        return;

    if (mail->messageType != MAIL_NORMAL || mail->stationery == MAIL_STATIONERY_AUCTION)
        return;

    for (MailItemInfoVec::iterator i = mail->items.begin(); i != mail->items.end(); ++i)
    {
        Item *item = master->GetMItem(i->item_guid);
        if (!item)
            continue;

        std::ostringstream body;
        body << "Hello, " << owner->GetName() << ",\n";
        body << "\n";
        body << "Here are the item(s) you've sent me by mistake";
        body << "\n";
        body << "Thanks,\n";
        body << bot->GetName() << "\n";

        MailDraft draft("Item(s) you've sent me", body.str());
        draft.AddItem(item);
        master->RemoveMItem(i->item_guid);
        draft.SendMailTo(MailReceiver(owner), MailSender(bot));
        return;
    }
}
