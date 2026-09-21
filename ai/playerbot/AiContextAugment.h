#pragma once

// Extension seam for other modules (a dungeon runner, a city-life module): a registered
// augmenter receives every bot's AI context right after it is built - class contexts
// included - and may append its own strategy / action / trigger / value contexts through
// AiObjectContext::AddShared. Registration also walks the bots that already exist, so a
// module that starts late still reaches them.
//
// Ownership: append fresh instances for every bot; the receiving list deletes what is not
// marked shared when the bot's context is torn down (a bot relogin). Never hand the same
// static instance to every bot - the list would delete it on the first teardown, and a
// context caches the objects it created by name, so all later bots would get the objects
// bound to the first bot's PlayerbotAI.

class PlayerbotAI;
namespace ai { class AiObjectContext; }

typedef void (*AiContextAugmenter)(PlayerbotAI* ai, ai::AiObjectContext* context);

// Register an augmenter (applied to every bot context built from now on, and to the
// contexts of the bots that are already in the world).
void RegisterAiContextAugmenter(AiContextAugmenter augmenter);

// Called by AiFactory once a bot's context is complete.
void ApplyAiContextAugmenters(PlayerbotAI* ai, ai::AiObjectContext* context);
