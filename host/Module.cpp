#include "Module.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "BotChatAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotHostAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotLftRoleAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "LftFillAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotPacketAdapter.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "BotPlayerAdapter.h"

namespace TortoiseBots {

void RegisterScripts()
{
    new BotHostAdapter();
    new BotLftRoleAdapter();
    new LftFillAdapter();
    new BotPacketAdapter();
    new BotPlayerAdapter();
    new BotChatAdapter();
}

} // namespace TortoiseBots
