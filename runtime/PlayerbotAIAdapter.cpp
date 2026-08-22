#include "PlayerbotAIAdapter.h"
#include "PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Log.h"

namespace TortoiseBots {

PlayerbotAIAdapter::PlayerbotAIAdapter(Player* bot, Player* master) : bot_(bot), master_(master) {}

PlayerbotAIAdapter::~PlayerbotAIAdapter()
{
    Shutdown();
}

bool PlayerbotAIAdapter::Initialize()
{
    if (!bot_ || !bot_->IsInWorld() || initialized_) return false;

    // Create the real PlayerbotAI for this Headless bot. The bot's Player is the
    // Headless Player (same GUID as the character), master is the human's Player.
    // This uses the Shyalya Tortoise baseline PlayerbotAI which is already
    // translated for MANGOSBOT_ZERO and Penqle's WorldLocation/Position APIs,
    // layered with mod-playerbots modern generic/class improvements.
    ai_ = new PlayerbotAI(bot_);
    if (!ai_) return false;

    // Set the master for the bot. PlayerbotAI::SetMaster expects a Player* that
    // is the human owner. For Headless same-account bots, master is the human's
    // network session Player (same account, different GUID).
    if (master_)
        ai_->SetMaster(master_);

    // PlayerbotAI(Player* bot) already created the AiObjectContext and the
    // four Engines (combat/non-combat/dead/reaction) via AiFactory, with the
    // per-class Strategy subfolders and the generic Base (Follow/Combat/etc).
    // No need to re-create them here; the adapter just ensures the master is set
    // and the AI is registered in the module-local storage.
    if (!ai_->GetAiObjectContext()) return false;

    // Register the AI in the module-local storage so donor code that still does
    // player->GetPlayerbotAI() (now shimmed to PlayerbotAIStorage) can find it
    // without touching core Player.
    PlayerbotAIStorage::Instance().SetAI(bot_, ai_);

    initialized_ = true;
    sLog.outString("TortoiseBots: PlayerbotAI attached for %s (%s) master %s",
        bot_->GetName(), bot_->GetObjectGuid().GetString().c_str(),
        master_ ? master_->GetName() : "none");
    return true;
}

void PlayerbotAIAdapter::Update(uint32_t diff)
{
    if (!initialized_ || !ai_ || !bot_ || !bot_->IsInWorld()) return;
    // Drive the real PlayerbotAI engine. This will process triggers, push
    // NextAction, and execute the highest relevance Action (e.g., Follow,
    // Attack, Heal, Buff, etc). The Engine uses the Strategy/Trigger/Action/
    // Value stack that was forward-ported from mod-playerbots on Shyalya base.
    ai_->UpdateAI(diff);
}

void PlayerbotAIAdapter::Shutdown()
{
    if (bot_) PlayerbotAIStorage::Instance().RemoveAI(bot_);
    delete ai_; ai_ = nullptr;
    initialized_ = false;
}

}
