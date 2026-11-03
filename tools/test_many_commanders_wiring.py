"""Wiring pins for more than eight commanders (task R8m).

The split itself lives in runtime/CommanderTelemetry.h (PartitionBindings,
BuildPayloadParts, pinned by tools/test_commander_telemetry.cpp). These pins
prove the live code uses it: the emitter builds every part and sends each one
datagram, the per-binding bot cut happens in the partition (not once per
cycle), and the collect loop keeps as many bindings as kMaxCommandersReported.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CMD = (ROOT / "commands/Commander.cpp").read_text()
EMIT = (ROOT / "runtime/ObservabilityEmitter.cpp").read_text()
TELEMETRY = (ROOT / "runtime/CommanderTelemetry.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


CODE = strip_comments(EMIT)


class ManyCommandersWiring(unittest.TestCase):
    def test_the_collect_cap_names_the_commanders_reported(self):
        fn = body(strip_comments(CMD), "void CollectCommanderTelemetry(")
        self.assertIn("CommanderTelemetry::kMaxCommandersReported", fn)
        self.assertNotIn("CommanderTelemetry::kMaxBindings", fn)

    def test_the_emitter_sends_every_part(self):
        fn = body(CODE, "void ObservabilityEmitter::EmitCommanders()")
        self.assertIn("CommanderTelemetry::BuildPayloadParts(", fn)
        call = fn.index("CommanderTelemetry::BuildPayloadParts(")
        loop = fn.index("for (std::string const& payload :", call)
        self.assertLess(call, loop)
        self.assertLess(loop, fn.index("SendDatagram(payload);", loop))
        self.assertNotIn("BuildPayload(", fn)

    def test_the_bot_cut_is_the_partitions(self):
        fn = body(CODE, "void ObservabilityEmitter::EmitCommanders()")
        self.assertNotIn("botsTotal", fn)
        header = strip_comments(TELEMETRY)
        fn = body(header, "std::vector<std::vector<size_t>> PartitionBindings(")
        self.assertIn("kMaxBotsPerPayload", fn)
        self.assertIn("kMaxMobsPerPayload", fn)

    def test_the_partition_is_bounded_by_the_binding_cap(self):
        header = strip_comments(TELEMETRY)
        self.assertIn("constexpr size_t kMaxCommandersReported = 20;", header)
        self.assertIn("constexpr size_t kMaxTrackedCommanders = 20;", header)
        fn = body(header, "std::vector<std::vector<size_t>> PartitionBindings(")
        self.assertIn("kMaxBindings", fn)

    def test_build_payload_writes_the_part_only_when_split(self):
        header = strip_comments(TELEMETRY)
        fn = body(header, "inline std::string BuildPayloadOf(")
        gate = fn.index("if (parts > 1)")
        self.assertLess(gate, fn.index(",\\\"part\\\":", gate))
        self.assertLess(fn.index(",\\\"part\\\":", gate), fn.index(",\\\"parts\\\":", gate))


if __name__ == "__main__":
    unittest.main()
