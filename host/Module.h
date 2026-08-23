#pragma once

namespace TortoiseBots {

// Called by the generated native-module loader. Registration is intentionally
// small: the actual behavior remains behind the native script adapters.
void RegisterScripts();

} // namespace TortoiseBots
