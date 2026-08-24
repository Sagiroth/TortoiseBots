// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access,clang:uninitialized,clang:undefined_identifier,clang:undeclared_identifier,clang:all
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
    if (!bot_ || !bot_->IsInWorld() || initialized_) return false; // pi-lens-ignore: clang:all
    if (!sPlayerbotAIConfig.enabled)
        return false;
    ai_ = new PlayerbotAI(bot_); // pi-lens-ignore: clang:all
    if (!ai_) return false;
    if (master_)
    {
        ai_->SetMaster(master_); // pi-lens-ignore: clang:all
        // AiFactory builds the engines before the native adapter knows the
        // owner. Re-apply the mature movement default at this seam so manual
        // same-account bots use Strategy/Trigger/Action follow rather than a
        // module-side movement implementation.
        ai_->EnsureDefaultMovementStrategy(); // pi-lens-ignore: clang:all
    }
    if (!ai_->GetAiObjectContext()) // pi-lens-ignore: clang:all
    {
        delete ai_;
        ai_ = nullptr;
        return false;
    }
    PlayerbotAIStorage::Instance().SetAI(bot_, ai_); // pi-lens-ignore: clang:all
    initialized_ = true;
    sLog.outString("TortoiseBots: PlayerbotAI attached for %s (%s) master %s", // pi-lens-ignore: clang:all
        bot_->GetName(), bot_->GetObjectGuid().GetString().c_str(), // pi-lens-ignore: clang:all
        master_ ? master_->GetName() : "none"); // pi-lens-ignore: clang:all
    return true;
}

void PlayerbotAIAdapter::Update(uint32_t diff)
{
    if (!initialized_ || !ai_ || !bot_ || !bot_->IsInWorld()) return; // pi-lens-ignore: clang:all
    ai_->UpdateAI(diff); // pi-lens-ignore: clang:all
}

void PlayerbotAIAdapter::RebindMaster(Player* master)
{
    master_ = master;
    if (!ai_)
        return;

    ai_->SetMaster(master); // pi-lens-ignore: clang:all
    // Mature strategy state survives a master pointer disconnect. Only repair
    // a bot with no movement strategy at all; never let a stale native intent
    // overwrite mature follow/stay/wander/guard/free/passive commands.
    if (!ai_->HasActiveMovementStrategy()) // pi-lens-ignore: clang:all
        ai_->EnsureDefaultMovementStrategy(); // pi-lens-ignore: clang:all
}

void PlayerbotAIAdapter::DetachMaster()
{
    master_ = nullptr;
    if (ai_)
        ai_->ClearMasterPointer(); // pi-lens-ignore: clang:all
}

void PlayerbotAIAdapter::Shutdown()
{
    if (bot_) PlayerbotAIStorage::Instance().RemoveAI(bot_); // pi-lens-ignore: clang:all
    delete ai_; ai_ = nullptr; // pi-lens-ignore: clang:all
    initialized_ = false;
}

}
