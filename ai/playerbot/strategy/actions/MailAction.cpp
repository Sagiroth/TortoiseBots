
#include "Mail/Mail.h"
#include "playerbot/playerbot.h"
#include "MailAction.h"
#include "MapNodes/MasterPlayer.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/Helpers.h"
#include "runtime/GuildOrderRelistPolicy.h"
#include "runtime/GuildOrderStore.h"
#include "host/ModuleLog.h"

#include <algorithm>
#include <cstdlib>

using namespace ai;

namespace
{
MasterPlayer* GetMailOwner(Player* bot)
{
    return bot && bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
}

// The core's auction notification subject is "<item>:<random>:<state>"
// (Mail.h AuctionMailState). Anything else is not an auction notification.
bool ParseAuctionSubject(std::string const& subject, uint32_t& itemId, uint32_t& state)
{
    std::vector<std::string> parts = split(subject, ':');
    if (parts.size() != 3 || parts[0].empty() || parts[2].empty())
        return false;
    char* end = nullptr;
    unsigned long const item = std::strtoul(parts[0].c_str(), &end, 10);
    if (!end || *end)
        return false;
    unsigned long const sentinel = std::strtoul(parts[2].c_str(), &end, 10);
    if (!end || *end)
        return false;
    itemId = uint32_t(item);
    state = uint32_t(sentinel);
    return true;
}

// A sale notification body is "<bidder>:<bid>:<buyout>:<deposit>:<cut>"
// (AuctionHouseMgr::SendAuctionSuccessfulMail); these are the listing terms
// the ledger row was posted with, so they identify the auction that settled.
void ParseAuctionSaleBody(std::string const& text, TortoiseBots::GuildOrders::RelistMail& view)
{
    std::vector<std::string> parts = split(text, ':');
    if (parts.size() != 5)
        return;
    uint32_t values[3] = {};
    for (size_t i = 0; i < 3; ++i)
    {
        if (parts[i + 1].empty())
            return;
        char* end = nullptr;
        unsigned long const value = std::strtoul(parts[i + 1].c_str(), &end, 10);
        if (!end || *end)
            return;
        values[i] = uint32_t(value);
    }
    view.bodyParsed = true;
    view.bodyBid = values[0];
    view.bodyBuyout = values[1];
    view.bodyDeposit = values[2];
}

TortoiseBots::GuildOrders::RelistMail MakeRelistMailView(Mail* mail, MasterPlayer* owner)
{
    TortoiseBots::GuildOrders::RelistMail view;
    view.auction = mail->stationery == MAIL_STATIONERY_AUCTION;
    view.hasItems = mail->has_items && !mail->items.empty();
    view.money = mail->money;
    ParseAuctionSubject(mail->subject, view.subjectItemId, view.subjectState);
    if (view.auction && view.subjectState == TortoiseBots::GuildOrders::kRelistMailSuccessful &&
        mail->itemTextId)
        ParseAuctionSaleBody(sObjectMgr.GetItemText(mail->itemTextId), view);
    if (view.hasItems)
    {
        Item* item = owner->GetMItem(mail->items.front().item_guid);
        if (item)
        {
            view.itemId = item->GetEntry();
            view.count = item->GetCount();
        }
    }
    return view;
}
}

std::map<std::string, MailProcessor*> MailAction::processors;

class TellMailProcessor : public MailProcessor
{
public:
    bool Before(Player* requester, PlayerbotAI* ai) override
    {
        ai->TellPlayer(requester, "=== Mailbox ===", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        tells.clear();
        return true;
    }

    bool Process(Player* requester, int index, Mail* mail, PlayerbotAI* ai, Event& event) override
    {
        Player* bot = ai->GetBot();
        MasterPlayer* mailOwner = GetMailOwner(bot);
        if (!mailOwner)
            return false;

        time_t cur_time = time(0);
        int days = (cur_time - mail->deliver_time) / 3600 / 24;
        std::ostringstream out;
        out << "#" << (index+1) << " ";
        if (!mail->money && !mail->has_items)
            out << "|cffffffff" << mail->subject;

        if (mail->money)
        {
            out << "|cffffff00" << ChatHelper::formatMoney(mail->money);
            if (!mail->subject.empty()) out << " |cffa0a0a0(" << mail->subject << ")";
        }

        if (mail->has_items)
        {
            for (MailItemInfoVec::iterator i = mail->items.begin(); i != mail->items.end(); ++i)
            {
                Item* item = mailOwner->GetMItem(i->item_guid);
                int count = item ? item->GetCount() : 1;
                ItemPrototype const *proto = sObjectMgr.GetItemPrototype(i->item_template);
                if (proto)
                {
                    sPlayerbotAIConfig.logEvent(ai, "MailAction", proto->Name1, std::to_string(proto->ItemId));
                    out << ChatHelper::formatItem(item, count);
                    if (!mail->subject.empty()) out << " |cffa0a0a0(" << mail->subject << ")";
                }
            }
        }

        out  << ", |cff00ff00" << days << " day(s)";
        tells.push_front(out.str());
        return true;
    }

    bool After(Player* requester, PlayerbotAI* ai) override
    {
        for (std::list<std::string>::iterator i = tells.begin(); i != tells.end(); ++i)
        {
            ai->TellPlayer(requester, *i, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return true;
    }

    static TellMailProcessor instance;

private:
    std::list<std::string> tells;
};

class TakeMailProcessor : public MailProcessor
{
public:

    bool Before(Player* requester, PlayerbotAI* ai) override
    {
        copper = 0;
        items.clear();
        return true;
    }

    bool Process(Player* requester, int index, Mail* mail, PlayerbotAI* ai, Event& event) override
    {
        Player* bot = ai->GetBot();
        if (!GetMailOwner(bot))
            return false;

        if (!CheckBagSpace(bot))
        {
            ai->TellError(requester, "Not enough bag space");
            return false;
        }

        ObjectGuid mailbox = FindMailbox(ai);
        bool moneyTaken = false;
        if (mail->money)
        {
            std::ostringstream out;
            if (event.GetSource() == "rpg action")
            {
                copper += mail->money;
            }
            else
            {
                out << mail->subject << ", |cffffff00" << ChatHelper::formatMoney(mail->money) << "|cff00ff00 processed";
                ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }

            WorldPacket packet;
            packet << mailbox;
            packet << mail->messageID;
            bot->GetSession()->HandleMailTakeMoney(packet);
            moneyTaken = true;
        }

        if (mail->has_items)
        {
            std::list<uint32> guids;
            for (MailItemInfoVec::iterator i = mail->items.begin(); i != mail->items.end(); ++i)
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(i->item_template);
                if (proto)
                    guids.push_back(i->item_guid);
            }

            for (std::list<uint32>::iterator i = guids.begin(); i != guids.end(); ++i)
            {
                WorldPacket packet;
                packet << mailbox;
                packet << mail->messageID;
                Item* item = GetMailOwner(bot)->GetMItem(*i);

                if (item)
                {
                    if (event.GetSource() == "rpg action")
                    {
                        items.push_back(ChatHelper::formatItem(item, item->GetCount()));
                    }
                    else
                    {
                        std::ostringstream out;
                        out << mail->subject << ", " << ChatHelper::formatItem(item) << "|cff00ff00 processed";
                        ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                    }
                }

                bot->GetSession()->HandleMailTakeItem(packet);
            }
        }

        // Only delete the mail if both money and items have been taken (or if it was an empty notification mail)
        MasterPlayer* mailOwner = GetMailOwner(bot);
        Mail* currentMail = mailOwner ? mailOwner->GetMail(mail->messageID) : nullptr;
        if (currentMail && currentMail->money == 0 && currentMail->items.empty() && !ai->HasActivePlayerMaster())
        {
            RemoveMail(bot, mail->messageID, mailbox);
        }
        else if (mail->sender < 10 && !ai->HasActivePlayerMaster() && !mail->money && !mail->has_items)
        {
            RemoveMail(bot, mail->messageID, mailbox);
        }
        return true;
    }

    bool After(Player* requester, PlayerbotAI* ai) override
    {
        if (!items.empty())
        {
            std::map<std::string, std::string> args;
            args["%itemcount"] = std::to_string(items.size());

            std::vector<std::string> lines = { BOT_TEXT2("|cff00ff00%itemcount items recieved from mail: ", args) };
            for (auto& item : items)
            {
                if (lines.back().size() + item.size() > 256)
                    lines.push_back("");

                lines.back() = lines.back() + item;
            }

            for (auto& line : lines)
                ai->TellPlayer(requester, line, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        if (copper > 0)
        {
            std::map<std::string, std::string> args;
            args["%money"] = ChatHelper::formatMoney(copper);
            ai->TellPlayer(requester, BOT_TEXT2("|cffffff00%money |cff00ff00recieved from mail.", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return true;
    }

    static TakeMailProcessor instance;

private:
    bool CheckBagSpace(Player* bot)
    {
        uint32 totalused = 0, total = 16;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
        {
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                totalused++;
        }
        uint32 totalfree = 16 - totalused;
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            if (const Bag* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag))
            {
                ItemPrototype const* pBagProto = pBag->GetProto();
                if (pBagProto->Class == ITEM_CLASS_CONTAINER && pBagProto->SubClass == ITEM_SUBCLASS_CONTAINER)
                    totalfree += pBag->GetFreeSlots();
            }

        }

        return totalfree >= 2;
    }

private:
    uint32 copper = 0;
    std::vector<std::string> items;
};

// Takes one order item id from waiting mail: fits that single item through
// the core's per-item check (stack merge included) instead of demanding free
// slots for the whole mailbox. A bot whose bags hold nothing sellable still
// lifts its order item this way; the broad take above needs two free slots
// and would refuse forever. Stops at the first item that no longer fits so
// the leg can park instead of looping past unrelated mail.
uint32_t ai::TakeOrderMail(PlayerbotAI* ai, uint32_t itemId)
{
    if (!ai || !itemId)
        return 0;
    Player* bot = ai->GetBot();
    MasterPlayer* owner = GetMailOwner(bot);
    if (!bot || !owner)
        return 0;
    ObjectGuid mailbox = MailProcessor::FindMailbox(ai);
    if (mailbox.IsEmpty())
        return 0;
    uint32_t taken = 0;
    time_t curTime = time(nullptr);
    for (PlayerMails::iterator itr = owner->GetMailBegin(); itr != owner->GetMailEnd(); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;
        if (!mail->has_items)
            continue;
        uint32_t wantedGuid = 0;
        for (MailItemInfoVec::const_iterator it = mail->items.begin(); it != mail->items.end(); ++it)
            if (it->item_template == itemId)
            {
                wantedGuid = it->item_guid;
                break;
            }
        if (!wantedGuid)
            continue;
        Item* item = owner->GetMItem(wantedGuid);
        if (!item)
            continue;
        uint32_t count = item->GetCount();
        ItemPosCountVec dest;
        if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
            break;
        WorldPacket packet;
        packet << mailbox;
        packet << mail->messageID;
        bot->GetSession()->HandleMailTakeItem(packet);
        taken += count;
        Mail* current = owner->GetMail(mail->messageID);
        if (current && current->money == 0 && current->items.empty() && !ai->HasActivePlayerMaster())
        {
            WorldPacket delPacket;
            delPacket << mailbox;
            delPacket << mail->messageID;
            bot->GetSession()->HandleMailDelete(delPacket);
        }
    }
    return taken;
}

// Settles the guild relist ledger against the waiting auction mail. A sale
// mail pays its gold into the bot's hands, an expired listing returns the
// stack; the ledger row then waits for the bank trip the relist drain makes.
// Runs at any order state, or with no order row at all: the guild-funded
// value outlives the order that posted it. Returns the rows this read matched.
uint32_t ai::TakeRelistMail(PlayerbotAI* ai)
{
    if (!ai)
        return 0;
    Player* bot = ai->GetBot();
    MasterPlayer* owner = GetMailOwner(bot);
    if (!bot || !owner || !bot->GetGuildId())
        return 0;
    std::vector<TortoiseBots::GuildOrders::RelistLedgerEntry> entries =
        TortoiseBots::GuildOrders::RelistEntries(bot->GetGUIDLow());
    if (entries.empty())
        return 0;
    ObjectGuid mailbox = MailProcessor::FindMailbox(ai);
    if (mailbox.IsEmpty())
        return 0;

    using namespace TortoiseBots::GuildOrders;
    uint32_t settled = 0;
    time_t curTime = time(nullptr);
    for (PlayerMails::iterator itr = owner->GetMailBegin(); itr != owner->GetMailEnd(); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;
        if (!mail->money && !mail->has_items)
            continue;
        RelistMail view = MakeRelistMailView(mail, owner);
        if (!view.auction)
            continue;
        // A mail settles a row only when its item, listing terms and timing
        // identify an unread row. Look-alike rows of one guild settle as the
        // oldest: that guild is credited either way. Rows of different guilds
        // leave the mail to normal mail handling, because the ledger never
        // pays one guild another auction's value.
        size_t matched = 0;
        size_t const matches = RelistMatchCount(entries, view, uint64_t(curTime), matched);
        if (!matches || !RelistSettleIndex(entries, view, uint64_t(curTime), matched))
        {
            if (matches > 1)
                TB_LOG_BASIC("TortoiseBots: relist mail ambiguous for bot %u (item %u): %zu ledger "
                    "rows match across guilds; leaving it to normal mail handling.", bot->GetGUIDLow(),
                    view.subjectItemId, matches);
            continue;
        }
        std::vector<RelistLedgerEntry>::iterator entry = entries.begin() + matched;

        RelistMailKind const kind = RelistMailMatch(*entry, view, uint64_t(curTime));
        uint32_t gold = 0;
        uint32_t items = 0;
        if (kind == RelistMailKind::Sold)
        {
            uint32_t before = bot->GetMoney();
            WorldPacket packet;
            packet << mailbox;
            packet << mail->messageID;
            bot->GetSession()->HandleMailTakeMoney(packet);
            uint32_t after = bot->GetMoney();
            if (after <= before)
                continue;
            gold = after - before;
        }
        else if (kind == RelistMailKind::Expired)
        {
            uint32_t itemGuid = 0;
            for (MailItemInfoVec::const_iterator it = mail->items.begin(); it != mail->items.end(); ++it)
            {
                if (it->item_template == entry->itemId)
                {
                    itemGuid = it->item_guid;
                    break;
                }
            }
            if (!itemGuid)
                continue;
            uint32_t before = ai->GetInventoryItemsCountWithId(entry->itemId);
            WorldPacket packet;
            packet << mailbox;
            packet << mail->messageID;
            bot->GetSession()->HandleMailTakeItem(packet);
            uint32_t after = ai->GetInventoryItemsCountWithId(entry->itemId);
            if (after <= before)
                continue;
            items = after - before;
        }
        else
            continue;

        RelistNoteMailReceived(*entry, kind, gold, items);
        if (!TortoiseBots::GuildOrders::NoteRelistReceived(bot->GetGUIDLow(), entry->seq,
                entry->heldGold, entry->heldCount))
            continue;
        ++settled;
        Mail* current = owner->GetMail(mail->messageID);
        if (current && current->money == 0 && current->items.empty() && !ai->HasActivePlayerMaster())
        {
            WorldPacket delPacket;
            delPacket << mailbox;
            delPacket << mail->messageID;
            bot->GetSession()->HandleMailDelete(delPacket);
        }
    }
    return settled;
}

class DeleteMailProcessor : public MailProcessor
{
public:
    bool Process(Player* requester, int index, Mail* mail, PlayerbotAI* ai, Event& event) override
    {
        std::ostringstream out;
        out << "|cffffffff" << mail->subject << "|cffff0000 deleted";
        RemoveMail(ai->GetBot(), mail->messageID, FindMailbox(ai));
        ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    static DeleteMailProcessor instance;
};

class ReadMailProcessor : public MailProcessor
{
public:
    bool Process(Player* requester, int index, Mail* mail, PlayerbotAI* ai, Event& event) override
    {
        std::ostringstream out, body;
        out << "|cffffffff" << mail->subject;
        ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        if (mail->itemTextId)
        {
            body << "\n" << sObjectMgr.GetItemText(mail->itemTextId);
            ai->TellPlayer(requester, body.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }
        return true;
    }

    static ReadMailProcessor instance;
};

TellMailProcessor TellMailProcessor::instance;
TakeMailProcessor TakeMailProcessor::instance;
DeleteMailProcessor DeleteMailProcessor::instance;
ReadMailProcessor ReadMailProcessor::instance;

bool MailAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (!requester && event.GetSource() != "rpg action")
        return false;

    if (!MailProcessor::FindMailbox(ai) && event.GetSource() != "debug")
    {
        ai->TellError(requester, "There is no mailbox nearby");
        return false;
    }

    if (processors.empty())
    {
        processors["?"] = &TellMailProcessor::instance;
        processors["take"] = &TakeMailProcessor::instance;
        processors["delete"] = &DeleteMailProcessor::instance;
        processors["read"] = &ReadMailProcessor::instance;
    }

    std::string text = event.GetParam();
    if (text.empty())
    {
        ai->TellPlayer(requester, "whisper 'mail ?' to query mailbox, 'mail take/delete/read filter' to take/delete/read mails by filter");
        return false;
    }

    std::vector<std::string> ss = split(text, ' ');
    std::string action = ss[0];
    std::string filter = ss.size() > 1 ? ss[1] : "";
    MailProcessor* processor = processors[action];
    if (!processor)
    {
        std::ostringstream out; out << action << ": I don't know how to do that";
        ai->TellPlayer(requester, out.str());
        return false;
    }

    // A mail read also settles the guild relist ledger, whatever the bot's
    // order state: the guild-funded value arrives long after the posting.
    if (action == "take")
        ai::TakeRelistMail(ai);

    if (!processor->Before(requester, ai))
        return false;

    MasterPlayer* mailOwner = GetMailOwner(bot);
    if (!mailOwner)
        return false;

    std::vector<Mail*> mailList;
    time_t cur_time = time(0);
    for (PlayerMails::iterator itr = mailOwner->GetMailBegin(); itr != mailOwner->GetMailEnd(); ++itr)
    {
        if ((*itr)->state == MAIL_STATE_DELETED || cur_time < (*itr)->deliver_time)
            continue;

        Mail *mail = *itr;
        mailList.push_back(mail);
    }

    if (mailList.empty())
        return false;

    std::map<int, Mail*> filtered = filterList(mailList, filter);
    for (std::map<int, Mail*>::iterator i = filtered.begin(); i != filtered.end(); ++i)
    {
        if (!processor->Process(requester, i->first, i->second, ai, event))
            break;
    }

    return processor->After(requester, ai);
}

void MailProcessor::RemoveMail(Player* bot, uint32 id, ObjectGuid mailbox)
{
    WorldPacket packet;
    packet << mailbox;
    packet << id;
    bot->GetSession()->HandleMailDelete(packet);
}

ObjectGuid MailProcessor::FindMailbox(PlayerbotAI* ai)
{
    std::list<ObjectGuid> gos = *ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest game objects no los");
    ObjectGuid mailbox;
    for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); ++i)
    {
        GameObject* go = ai->GetGameObject(*i);
        if (go && go->GetGoType() == GAMEOBJECT_TYPE_MAILBOX)
        {
            mailbox = go->getObjectGuid();
            break;
        }
    }

    return mailbox;
}