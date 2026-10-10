
#include "playerbot/playerbot.h"
#include "ReadyCheckAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ReadyRebuffPolicy.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

std::string formatPercent(std::string name, uint8 value, float percent)
{
    std::ostringstream out;

    std::string color;
    if (percent > 75)
        color = "|cff00ff00";
    else if (percent > 50)
        color = "|cffffff00";
    else
        color = "|cffff0000";

    out << "|cffffffff[" << name << "]" << color << "x" << (int)value;
    return out.str();
}

class ReadyChecker
{
public:
    virtual bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) = 0;
    virtual std::string GetName() = 0;
    virtual bool PrintAlways() { return true; }

    static std::list<ReadyChecker*> checkers;
};

std::list<ReadyChecker*> ReadyChecker::checkers;

class HealthChecker : public ReadyChecker
{
public:
    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        return AI_VALUE2(uint8, "health", "self target") > sPlayerbotAIConfig.almostFullHealth;
    }

    virtual std::string GetName() override { return "HP"; }
};

class ManaChecker : public ReadyChecker
{
public:
    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        return !AI_VALUE2(bool, "has mana", "self target") || AI_VALUE2(uint8, "mana", "self target") > sPlayerbotAIConfig.mediumHealth;
    }
    virtual std::string GetName() override { return "MP"; }
};

class DistanceChecker : public ReadyChecker
{
public:
    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        Player* bot = ai->GetBot();
        if (requester)
        {
            bool distance = sServerFacade.getDistance2d(bot, requester) <= sPlayerbotAIConfig.sightDistance;
            if (!distance)
            {
                return false;
            }
        }

        return true;
    }

    virtual bool PrintAlways() override { return false; }
    virtual std::string GetName() override { return "Far away"; }
};

class HunterChecker : public ReadyChecker
{
public:
    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        Player* bot = ai->GetBot();
        if (bot->GetClass() == CLASS_HUNTER)
        {
            if (!bot->GetUInt32Value(PLAYER_AMMO_ID))
            {
                ai->TellError(requester, "Out of ammo!");
                return false;
            }

            if (!bot->GetPet())
            {
                ai->TellError(requester, "No pet!");
                return false;
            }

            if (bot->GetPet()->GetHappinessState() == UNHAPPY)
            {
                ai->TellError(requester, "Pet is unhappy!");
                return false;
            }
        }

        return true;
    }

    virtual bool PrintAlways() override { return false; }
    virtual std::string GetName() override { return "Far away"; }
};


class ItemCountChecker : public ReadyChecker
{
public:
    ItemCountChecker(std::string item, std::string name) { this->item = item; this->name = name; }

    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        return AI_VALUE2(uint32, "item count", item) > 0;
    }

    virtual std::string GetName() override { return name; }

private:
    std::string item, name;
};

class ManaPotionChecker : public ItemCountChecker
{
public:
    ManaPotionChecker(std::string item, std::string name) : ItemCountChecker(item, name) {}

    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        return !AI_VALUE2(bool, "has mana", "self target") || ItemCountChecker::Check(requester, ai, context);
    }
};

bool ReadyCheckAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    WorldPacket p = event.GetPacket();
    if (!p.empty())
    {
        // Member answer forwarded to the raid leader (1.12 server sends
        // these with the member GUID + state): not a new check. Keep the
        // old path - ignore our own echo, otherwise answer immediately.
        ObjectGuid player;
        p.rpos(0);
        p >> player;
        if (player == bot->getObjectGuid())
            return false;
    }
    else if (p.getOpcode() == MSG_RAID_READY_CHECK && sPlayerbotAIConfig.forceRebuffOnReadyCheck && !bot->IsInCombat())
    {
        // Defer the confirm until buffs settle (SOC-S5): only a real
        // incoming check (1.12 request broadcast: this opcode with an empty
        // payload) holds. Manual "ready" whispers arrive with a default
        // opcode-0 packet, so they fall through to the immediate answer
        // below - an explicit order answers now, and it also clears any
        // pending anchor, so no second confirm goes out. Report status now,
        // stamp the anchor, and let the "force rebuff pending" trigger send
        // the confirm via "ready reply".
        ReportReadiness(requester);
        context->GetValue<time_t>("manual time", ai::ReadyRebuffAnchorKey())->Set(time(0));
        // Force-rebuff entry (donor ForceRebuffOnReadyCheck, strategy-gated
        // like the donor's Begin()): the 2-min OOC top-off window opens only
        // for bots with the "force rebuff" strategy, so `.bot nc -force
        // rebuff` disables the top-off, the per-tick bypass, and the reply
        // hold. The anchor stamp above stays ungated so opted-out bots keep
        // the pre-existing SOC-S5 defer.
        if (ai->HasStrategy("force rebuff", BotState::BOT_STATE_NON_COMBAT))
        {
            context->GetValue<int32>("manual int", "force rebuff begin ms")->Set(WorldTimer::getMSTime());
            context->GetValue<bool>("manual bool", "force rebuff buff pending")->Set(false);
            context->GetValue<bool>("manual bool", "force rebuff buff proposed")->Set(false);
        }
        return true;
    }

	return ReadyCheck(requester);
}

bool ReadyCheckAction::ReadyCheck(Player* requester)
{
    ReportReadiness(requester);

    SendReadyConfirm();

    ai->ChangeStrategy("-ready check", BotState::BOT_STATE_NON_COMBAT);

    // A manual finish answers an already-open check: clear any deferred
    // anchor so the pending trigger cannot send a second confirm.
    context->GetValue<time_t>("manual time", ai::ReadyRebuffAnchorKey())->Set(time_t(0));

    return true;
}

void ReadyCheckAction::ReportReadiness(Player* requester)
{
    if (ReadyChecker::checkers.empty())
    {
        ReadyChecker::checkers.push_back(new HealthChecker());
        ReadyChecker::checkers.push_back(new ManaChecker());
        ReadyChecker::checkers.push_back(new DistanceChecker());
        ReadyChecker::checkers.push_back(new HunterChecker());

        ReadyChecker::checkers.push_back(new ItemCountChecker("food", "Food"));
        ReadyChecker::checkers.push_back(new ManaPotionChecker("drink", "Water"));
        ReadyChecker::checkers.push_back(new ItemCountChecker("healing potion", "Hpot"));
        ReadyChecker::checkers.push_back(new ManaPotionChecker("mana potion", "Mpot"));
    }

    // Diagnostics first (donor ReportReadinessToMaster shape): hunter ammo /
    // pet warnings must fire on the deferred path too, which never reaches
    // ReadyCheck.
    bool result = true;
    for (std::list<ReadyChecker*>::iterator i = ReadyChecker::checkers.begin(); i != ReadyChecker::checkers.end(); ++i)
    {
        ReadyChecker* checker = *i;
        bool ok = checker->Check(requester, ai, context);
        result = result && ok;
    }

    std::ostringstream out;

    uint32 hp = AI_VALUE2(uint32, "item count", "healing potion");
    out << formatPercent("Hp", hp, 100.0 * hp / 5);

    out << ", ";
    uint32 food = AI_VALUE2(uint32, "item count", "food");
    out << formatPercent("Food", food, 100.0 * food / 20);

    if (AI_VALUE2(bool, "has mana", "self target"))
    {
        out << ", ";
        uint32 mp = AI_VALUE2(uint32, "item count", "mana potion");
        out << formatPercent("Mp", mp, 100.0 * mp / 5);

        out << ", ";
        uint32 water = AI_VALUE2(uint32, "item count", "water");
        out << formatPercent("Water", water, 100.0 * water / 20);
    }

    ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
}

void ReadyCheckAction::SendReadyConfirm()
{
    WorldPacket packet(MSG_RAID_READY_CHECK);
    packet << uint8(1);
    bot->GetSession()->HandleRaidReadyCheckOpcode(packet);
}

bool FinishReadyCheckAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    return ReadyCheck(requester);
}

bool ReadyReplyAction::isUseful()
{
    if (!sPlayerbotAIConfig.forceRebuffOnReadyCheck || bot->IsInCombat())
        return false;

    time_t anchor = context->GetValue<time_t>("manual time", ai::ReadyRebuffAnchorKey())->Get();
    if (anchor == time_t(0))
        return false;

    // Force-rebuff gate (donor ReadyReply gating): hold the confirm while a
    // buff cast landed this cycle, one is mid-cast, or a buff trigger fired
    // this tick ("proposed", covering inter-cast gaps where neither pending
    // nor casting is set for chained instants). Past the hard cap the due
    // verdict replies regardless. The 30 s cap still bounds the hold.
    bool buffWorking = context->GetValue<bool>("manual bool", "force rebuff buff pending")->Get();
    bool proposed = context->GetValue<bool>("manual bool", "force rebuff buff proposed")->Get();
    bool isCasting = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) != nullptr ||
        bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) != nullptr;
    if ((buffWorking || proposed || isCasting) && !ai::ReadyRebuffPastCap(anchor, time(0)))
        return false;
    return ai::ReadyRebuffDue(anchor, time(0), isCasting);
}

bool ForceRebuffAction::Execute(Event& /*event*/)
{
    // Manual "rebuff" command (donor ForceRebuffAction): open the 2-min OOC
    // top-off window with no ready check to answer. Strategy-gated like the
    // donor (needs the "force rebuff" strategy) and OOC-only.
    if (bot->IsInCombat() || !ai->HasStrategy("force rebuff", BotState::BOT_STATE_NON_COMBAT))
        return false;

    context->GetValue<int32>("manual int", "force rebuff begin ms")->Set(WorldTimer::getMSTime());
    context->GetValue<bool>("manual bool", "force rebuff buff pending")->Set(false);
    context->GetValue<bool>("manual bool", "force rebuff buff proposed")->Set(false);
    return true;
}

bool ReadyReplyAction::Execute(Event& /*event*/)
{
    SendReadyConfirm();

    context->GetValue<time_t>("manual time", ai::ReadyRebuffAnchorKey())->Set(time_t(0));

    ai->ChangeStrategy("-ready check", BotState::BOT_STATE_NON_COMBAT);
    return true;
}
