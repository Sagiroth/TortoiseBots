#pragma once
class PlayerbotAI;
class Unit;
class Player;
namespace ai {
Unit* PatchwerkSafetyBoss(PlayerbotAI* ai);
Player* PatchwerkSafetyMain(PlayerbotAI* ai, Unit* boss);
Player* PatchwerkHealingTarget(PlayerbotAI* ai);
bool PatchwerkSafetyHold(PlayerbotAI* ai);
bool PatchwerkSafetyMeleeHold(PlayerbotAI* ai, Unit* boss);
bool PatchwerkSafetyBlocksSpell(PlayerbotAI* ai, unsigned spellId, Unit* target);
bool UpdatePatchwerkSafety(PlayerbotAI* ai);
}
