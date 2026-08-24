#include "../host/Module.h"

// Called by Penqle's generated native-module loader.
void AddTortoiseBotsScripts()
{
    TortoiseBots::RegisterScripts();
}
