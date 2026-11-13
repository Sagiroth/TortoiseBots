// Standalone regression test for the forced speed change ack
// (runtime/policy/infra/SpeedChangeAckPolicy.h): a bot's session must answer
// SMSG_FORCE_*_SPEED_CHANGE with the matching client ack, in the field order
// the core's WorldSession::HandleForceSpeedChangeAckOpcodes reads.
//
// Build and run:
//   nice g++ -std=c++17 -Wall -Wextra tools/test_speed_change_ack_policy.cpp -o /tmp/test_speed_change_ack_policy
//   /tmp/test_speed_change_ack_policy

#include "../runtime/policy/infra/SpeedChangeAckPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace TortoiseBots::SpeedChangeAck;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

namespace
{
// A WorldPacket stand-in with the same little-endian field writes the ack uses.
struct FakeMovementInfo
{
    uint8_t bytes[30];
};

FakeMovementInfo MakeMovementInfo()
{
    FakeMovementInfo info{};
    for (uint8_t i = 0; i < sizeof(info.bytes); ++i)
        info.bytes[i] = uint8_t(0xA0 + i);
    return info;
}

struct FakePacket
{
    std::vector<uint8_t> bytes;

    template <class T>
    void Append(T const& value)
    {
        uint8_t const* raw = reinterpret_cast<uint8_t const*>(&value);
        bytes.insert(bytes.end(), raw, raw + sizeof(T));
    }

    FakePacket& operator<<(uint64_t value) { Append(value); return *this; }
    FakePacket& operator<<(uint32_t value) { Append(value); return *this; }
    FakePacket& operator<<(float value) { Append(value); return *this; }
    FakePacket& operator<<(FakeMovementInfo const& value) { bytes.insert(bytes.end(), value.bytes, value.bytes + sizeof(value.bytes)); return *this; }
};
} // namespace

int main()
{
    // Every server opcode the AI answers has its matching client ack.
    struct Pair { uint16_t server; uint16_t client; Move move; };
    Pair const pairs[] = {
        { kServerWalk, kClientWalk, Move::Walk },
        { kServerRun, kClientRun, Move::Run },
        { kServerRunBack, kClientRunBack, Move::RunBack },
        { kServerSwim, kClientSwim, Move::Swim },
        { kServerSwimBack, kClientSwimBack, Move::SwimBack },
        { kServerTurnRate, kClientTurnRate, Move::TurnRate },
    };
    for (Pair const& pair : pairs)
    {
        Move move = Move::Walk;
        CHECK(MoveForServerOpcode(pair.server, move));
        CHECK(move == pair.move);
        CHECK(ClientOpcode(move) == pair.client);
        CHECK(std::strcmp(MoveName(move), "unknown") != 0);
    }

    // Anything else (a chat, a knock-back) is not a speed change.
    for (uint16_t other : { uint16_t(0), uint16_t(0x0EF), uint16_t(0x096), uint16_t(0x2DF + 1) })
    {
        Move move = Move::Run;
        CHECK(!MoveForServerOpcode(other, move));
        CHECK(move == Move::Run);
    }

    // Layout: 8-byte full guid, 4-byte counter, the opaque movement info, then
    // the forced speed. The server packet's packed guid is a different form;
    // a packed write here would shift every later field.
    uint64_t const guid = 0x0000000000000042ULL;
    uint32_t const counter = 0x12345678u;
    float const speed = 3.5f;
    FakeMovementInfo const info = MakeMovementInfo();

    FakePacket ack;
    WriteAck(ack, guid, counter, info, speed);

    CHECK(ack.bytes.size() == 8 + 4 + sizeof(info.bytes) + 4);

    uint64_t parsedGuid = 0;
    uint32_t parsedCounter = 0;
    float parsedSpeed = 0.f;
    std::memcpy(&parsedGuid, ack.bytes.data() + 0, sizeof(parsedGuid));
    std::memcpy(&parsedCounter, ack.bytes.data() + 8, sizeof(parsedCounter));
    std::memcpy(&parsedSpeed, ack.bytes.data() + 8 + 4 + sizeof(info.bytes), sizeof(parsedSpeed));

    CHECK(parsedGuid == guid);
    CHECK(parsedCounter == counter);
    CHECK(parsedSpeed == speed);
    CHECK(std::memcmp(ack.bytes.data() + 12, info.bytes, sizeof(info.bytes)) == 0);

    // The ack handler matches the pending change within 0.01 of the speed it
    // sent, so the forced value has to round-trip bit-exactly.
    float const seen = parsedSpeed;
    CHECK(seen == speed);

    // A fractional slowed rate (Harsh Winds: -86 %) survives as well.
    FakePacket slowAck;
    WriteAck(slowAck, guid, counter, info, 0.14f);
    float slowed = 0.f;
    std::memcpy(&slowed, slowAck.bytes.data() + 8 + 4 + sizeof(info.bytes), sizeof(slowed));
    CHECK(slowed == 0.14f);

    if (g_failed)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("speed change ack policy: all checks passed\n");
    return 0;
}
