// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access,clang:uninitialized,clang:undefined_identifier,clang:undeclared_identifier,clang:all
#include "PlayerbotAIAdapter.h"
#include "BotManager.h"
#include "PlayerbotAIStorage.h"
#include "AltBotService.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/BotActionLog.h"
#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotDbStore.h"
#include "ByteBuffer.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Log.h"
#include "../host/ModuleLog.h"

namespace TortoiseBots {

PlayerbotAIAdapter::PlayerbotAIAdapter(Player* bot, Player* master) : bot_(bot), master_(master)
{
    if (bot_)
        botGuidLow_ = bot_->GetObjectGuid().GetCounter(); // pi-lens-ignore: clang:all
}

PlayerbotAIAdapter::~PlayerbotAIAdapter()
{
    Shutdown();
}

bool PlayerbotAIAdapter::Initialize()
{
    if (!bot_ || !bot_->IsInWorld() || initialized_) return false; // pi-lens-ignore: clang:all
    NormalizeHeadlessGmPresentation(bot_);
    if (!sPlayerbotAIConfig.enabled)
        return false;
    ai_ = new PlayerbotAI(bot_); // pi-lens-ignore: clang:all
    if (!ai_) return false;
    if (master_)
    {
        ai_->SetMaster(master_); // pi-lens-ignore: clang:all
        // The engines were built before the master was known, so the saved
        // strategies and values (loot settings, ignored spells) were skipped.
        ai_->ResetStrategies(); // pi-lens-ignore: clang:all
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
    ai::botdiag::BotActionLog::NoteLogin(bot_->GetObjectGuid().GetCounter()); // pi-lens-ignore: clang:all
    PlayerbotAIStorage::Instance().SetAI(bot_, ai_); // pi-lens-ignore: clang:all
    initialized_ = true;
    TB_LOG_DETAIL("TortoiseBots: PlayerbotAI attached for %s (%s) master %s", // pi-lens-ignore: clang:all
        bot_->GetName(), bot_->GetObjectGuid().GetString().c_str(), // pi-lens-ignore: clang:all
        master_ ? master_->GetName() : "none"); // pi-lens-ignore: clang:all
    return true;
}

void PlayerbotAIAdapter::Update(uint32_t diff)
{
    if (!IsUsable()) return; // pi-lens-ignore: clang:all
    try
    {
        ai_->UpdateAI(diff); // pi-lens-ignore: clang:all
    }
    catch (ByteBufferException const&)
    {
        // Donor packet actions parse opcode-specific payloads. A malformed
        // or host-variant packet must be dropped for this tick, never unwind
        // through the world update and terminate mangosd.
        sLog.outError("TortoiseBots: dropped malformed packet while updating bot %s",
            bot_->GetName()); // pi-lens-ignore: clang:all
    }
}

bool PlayerbotAIAdapter::IsUsable() const
{
    return initialized_ && bot_ && bot_->IsInWorld() && ai_ &&
        ai_->GetBot() == bot_ && ai_->GetAiObjectContext() &&
        ai_->GetCurrentEngine() &&
        PlayerbotAIStorage::Instance().GetAI(bot_) == ai_;
}

void PlayerbotAIAdapter::RebindMaster(Player* master)
{
    master_ = master;
    if (!ai_)
        return;

    ai_->SetMaster(master); // pi-lens-ignore: clang:all
    if (master && master->GetSession() && !master->GetSession()->IsHeadless())
    {
        ai_->Reset(true);
        if (bot_)
            bot_->StopMoving();

        if (AiObjectContext* context = ai_->GetAiObjectContext()) // pi-lens-ignore: clang:all
        {
            if (ai::Value<Unit*>* masterTarget = context->GetValue<Unit*>("master target"))
                masterTarget->Reset();
            if (ai::Value<Unit*>* followTargetValue = context->GetValue<Unit*>("follow target"))
                followTargetValue->Reset();
        }
    }
    // If the bot fell into wander while master was offline, return it to follow.
    if (ai_->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT) &&
        master && master->GetSession() && !master->GetSession()->IsHeadless())
    {
        ai_->SetMovementStrategy("follow"); // pi-lens-ignore: clang:all
    }
    // Mature strategy state survives a master pointer disconnect. Only repair
    // a bot with no movement strategy at all; never let a stale native intent
    // overwrite mature follow/stay/wander/guard/free/passive commands.
    else if (!ai_->HasActiveMovementStrategy()) // pi-lens-ignore: clang:all
    {
        ai_->EnsureDefaultMovementStrategy(); // pi-lens-ignore: clang:all
    }
}

void PlayerbotAIAdapter::DetachMaster()
{
    master_ = nullptr;
    if (ai_)
        ai_->ClearMasterPointer(); // pi-lens-ignore: clang:all
}

// Value commands such as `ll` and `ss` change only the live AI, and a
// strategy change saves only up to then, so a player's bot saves when it
// leaves. An owned alt whose owner is offline is still the player's
// companion; an alt playing on its own runs the pool kit, which must not
// overwrite it. Call before the alt session ends (it ends on logout).
void PlayerbotAIAdapter::SavePlayerSnapshot()
{
    if (!ai_ || !bot_ || sPlayerbotAIConfig.bExplicitDbStoreSave) // pi-lens-ignore: clang:all
        return;
    if (ai_->HasRealPlayerMaster() ||
        (ai_->IsOwnedBot() && !AltBotService::Instance().IsAutonomous(bot_->GetObjectGuid())))
        sPlayerbotDbStore.Save(ai_);
}

void PlayerbotAIAdapter::Shutdown()
{
    // RemoveAI first: map-thread hooks resolve the AI through storage, so
    // once this returns no new hook can reach ai_. Close then swaps the
    // tombstone in: a hook holding the old pointer still fails its reopen.
    if (bot_) PlayerbotAIStorage::Instance().RemoveAI(bot_); // pi-lens-ignore: clang:all
    if (botGuidLow_)
        ai::botdiag::BotActionLog::Close(botGuidLow_);
    delete ai_; ai_ = nullptr; // pi-lens-ignore: clang:all
    initialized_ = false;
}

}
