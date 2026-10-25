#pragma once
#include <string>
class Player;
namespace TortoiseBots {
// 1 applied, 0 skipped, -1 failed. Explicit GM command only; never automatic.
std::string InspectExperimentalBis(Player* bot);
int ApplyExperimentalBis(Player* bot, std::string& report);
}
