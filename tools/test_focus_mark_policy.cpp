#include "../ai/playerbot/strategy/values/FocusMarkPolicy.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(cond) do { if (!(cond)) { std::printf("FAILED: %s (line %d)\n", #cond, __LINE__); std::exit(1); } } while (0)

int main()
{
    using ai::FocusRefusal;

    CHECK(ai::IsFocusMarkWord("skull"));
    CHECK(ai::IsFocusMarkWord("cross"));
    CHECK(ai::IsFocusMarkWord("star"));
    CHECK(!ai::IsFocusMarkWord("none"));
    CHECK(!ai::IsFocusMarkWord(""));
    CHECK(!ai::IsFocusMarkWord("clear"));

    // Own mark up: no fallback. Own mark gone: Skull. Skull or "none": nothing.
    CHECK(ai::FocusFallbackMark("cross", true).empty());
    CHECK(ai::FocusFallbackMark("cross", false) == "skull");
    CHECK(ai::FocusFallbackMark("square", false) == "skull");
    CHECK(ai::FocusFallbackMark("skull", false).empty());
    CHECK(ai::FocusFallbackMark("none", false).empty());

    std::vector<std::string> const ccMarks = { "moon", "diamond" };
    CHECK(ai::DecideFocusOrder(true, "cross", {}) == FocusRefusal::Healer);
    CHECK(ai::DecideFocusOrder(true, "skull", {}) == FocusRefusal::Healer);
    CHECK(ai::DecideFocusOrder(false, "moon", ccMarks) == FocusRefusal::CcMark);
    CHECK(ai::DecideFocusOrder(false, "diamond", ccMarks) == FocusRefusal::CcMark);
    CHECK(ai::DecideFocusOrder(false, "cross", ccMarks) == FocusRefusal::None);
    CHECK(ai::DecideFocusOrder(false, "skull", { "skull" }) == FocusRefusal::None);
    CHECK(ai::DecideFocusOrder(false, "moon", {}) == FocusRefusal::None);

    std::vector<std::string> const focusMarks = { "skull", "skull", "cross" };
    CHECK(ai::CcMarkHeldAsFocus("cross", focusMarks));
    CHECK(!ai::CcMarkHeldAsFocus("moon", focusMarks));
    CHECK(!ai::CcMarkHeldAsFocus("skull", focusMarks));
    CHECK(!ai::CcMarkHeldAsFocus("square", {}));

    std::printf("test_focus_mark_policy: OK\n");
    return 0;
}
