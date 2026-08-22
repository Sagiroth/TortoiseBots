#pragma once
#include <cstdint>
#include <memory>

class ObjectGuid;
class Player;
class PlayerbotAI;

class Player;
class PlayerbotAI;
namespace ai {
    class AiObjectContext;
    class Engine;
}

namespace TortoiseBots {

// Adapter that owns a real PlayerbotAI per Headless bot and drives its Engine/Strategy stack.
// This is the integration point between the generic Headless SessionTransport lifecycle
// (BotManager) and the real PlayerBots runtime (PlayerbotAI/Engine/AiObjectContext).
// No core Player fields are added; the mapping is module-local via PlayerbotAIStorage.

class PlayerbotAIAdapter
{
public:
    explicit PlayerbotAIAdapter(Player* bot, Player* master);
    ~PlayerbotAIAdapter();

    // Called once per Headless bot when it enters world (BotManager::InWorld)
    bool Initialize();

    // Called every world tick (BotManager::Update) — drives Engine::DoNextAction
    void Update(uint32_t diff);

    // Called on bot logout/removal
    void Shutdown();

    PlayerbotAI* GetAI() const { return ai_; }
    bool IsInitialized() const { return initialized_; }

private:
    Player* bot_;
    Player* master_;
    PlayerbotAI* ai_ = nullptr; // raw ptr, owned via PlayerbotAIStorage lifecycle; avoids unique_ptr incomplete type at header
    bool initialized_ = false;
};

}
