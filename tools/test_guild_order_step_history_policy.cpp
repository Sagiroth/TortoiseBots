// Standalone test for runtime/GuildOrderStepHistoryPolicy.h: the per-order step timeline.
#include "../runtime/GuildOrderStepHistoryPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // An empty history differs from any tuple; the first note lands it.
    OrderStepHistory h;
    CHECK(OrderStepDiffers(h, OrderState::Working, OrderReason::None, SupplyStep::Bank, 2319));
    NoteOrderStep(h, OrderStepEntry{ 1000, OrderState::Working, OrderReason::None, SupplyStep::Bank, 2319 });
    CHECK(h.entries.size() == 1);
    // The same tuple again does not append: the first time is kept.
    NoteOrderStep(h, OrderStepEntry{ 9999, OrderState::Working, OrderReason::None, SupplyStep::Bank, 2319 });
    CHECK(h.entries.size() == 1);
    CHECK(h.entries.back().atSec == 1000);
    // Each single-field change appends exactly one entry.
    NoteOrderStep(h, OrderStepEntry{ 1100, OrderState::Working, OrderReason::NoBanker, SupplyStep::Bank, 2319 });
    CHECK(h.entries.size() == 2);
    NoteOrderStep(h, OrderStepEntry{ 1200, OrderState::Working, OrderReason::NoBanker, SupplyStep::AuctionHouse, 2319 });
    CHECK(h.entries.size() == 3);
    NoteOrderStep(h, OrderStepEntry{ 1300, OrderState::Working, OrderReason::NoBanker, SupplyStep::AuctionHouse, 2320 });
    CHECK(h.entries.size() == 4);

    // Overflow drops the oldest and counts it.
    OrderStepHistory o;
    for (uint32_t id = 1; id <= 17; ++id)
        if (OrderStepDiffers(o, OrderState::Working, OrderReason::None, SupplyStep::Farm, id))
            NoteOrderStep(o, OrderStepEntry{ uint64_t(1000 + id), OrderState::Working, OrderReason::None, SupplyStep::Farm, id });
    CHECK(o.entries.size() == 16);
    CHECK(o.dropped == 1);
    CHECK(o.entries.front().itemId == 2);
    CHECK(o.entries.back().itemId == 17);
    // dropped saturates instead of wrapping.
    o.dropped = UINT32_MAX;
    if (OrderStepDiffers(o, OrderState::Working, OrderReason::None, SupplyStep::Farm, 18))
        NoteOrderStep(o, OrderStepEntry{ 2000, OrderState::Working, OrderReason::None, SupplyStep::Farm, 18 });
    CHECK(o.entries.size() == 16);
    CHECK(o.dropped == UINT32_MAX);

    // Exact wire format, oldest first, ages from nowSec.
    OrderStepHistory f;
    f.entries.push_back(OrderStepEntry{ 1000, OrderState::Working, OrderReason::None, SupplyStep::Bank, 2319 });
    f.entries.push_back(OrderStepEntry{ 1060, OrderState::Working, OrderReason::NoBanker, SupplyStep::Bank, 2319 });
    std::vector<std::string> lines = FormatOrderStepLines("Goldie", f, 1090);
    CHECK(lines.size() == 4);
    CHECK(lines[0] == "TBM:ORDER_STEPS_BEGIN|Goldie|2|0");
    CHECK(lines[1] == "TBM:ORDER_STEP|Goldie|90|working|-|bank|2319");
    CHECK(lines[2] == "TBM:ORDER_STEP|Goldie|30|working|nobanker|bank|2319");
    CHECK(lines[3] == "TBM:ORDER_STEPS_END|Goldie");

    // A client clock ahead of the server clamps the age at 0.
    OrderStepHistory skew;
    skew.entries.push_back(OrderStepEntry{ 2000, OrderState::Working, OrderReason::None, SupplyStep::Bank, 2319 });
    std::vector<std::string> skewLines = FormatOrderStepLines("Goldie", skew, 1000);
    CHECK(skewLines[1] == "TBM:ORDER_STEP|Goldie|0|working|-|bank|2319");

    // An empty history is still a real answer.
    OrderStepHistory empty;
    std::vector<std::string> emptyLines = FormatOrderStepLines("Goldie", empty, 5000);
    CHECK(emptyLines.size() == 2);
    CHECK(emptyLines[0] == "TBM:ORDER_STEPS_BEGIN|Goldie|0|0");
    CHECK(emptyLines[1] == "TBM:ORDER_STEPS_END|Goldie");

    // A finished entry carries the `-` tokens for reason and step.
    OrderStepHistory done;
    done.entries.push_back(OrderStepEntry{ 1000, OrderState::Done, OrderReason::None, SupplyStep::None, 0 });
    std::vector<std::string> doneLines = FormatOrderStepLines("Goldie", done, 1000);
    CHECK(doneLines[1] == "TBM:ORDER_STEP|Goldie|0|done|-|-|0");

    if (failures == 0)
        std::printf("guild order step history policy: all tests passed\n");
    return failures != 0;
}
