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
    ai_ = new PlayerbotAI(bot_); // pi-lens-ignore: clang:all
    if (!ai_) return false;
    if (master_) ai_->SetMaster(master_); // pi-lens-ignore: clang:all
    if (!ai_->GetAiObjectContext()) return false; // pi-lens-ignore: clang:all
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

void PlayerbotAIAdapter::Shutdown()
{
    if (bot_) PlayerbotAIStorage::Instance().RemoveAI(bot_); // pi-lens-ignore: clang:all
    delete ai_; ai_ = nullptr; // pi-lens-ignore: clang:all
    initialized_ = false;
}

}
