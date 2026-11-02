#!/usr/bin/env python3
"""Checks for tools/gen_decision_capture.py (decision capture C1).

Run: python3 -m unittest tools/test_gen_decision_capture.py
"""

import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))

import gen_decision_capture as gen  # noqa: E402

# Every captured field name that matches the privacy regex, with a one-word
# reason. Reviewed: all of these are game concepts, not credentials.
#   botKeysForPlayers  a config flag (bots carry the key, not a player)
#   passSpotKnown/...  the island pass spot (a position on the navmesh)
#   passed             the units a pull already passed
PRIVACY_ALLOW = {
    "botKeysForPlayers": "doorKeys",
    "passSpotKnown": "islandPass",
    "passX": "islandPass",
    "passY": "islandPass",
    "passZ": "islandPass",
    "passed": "pullPassed",
}

PRIVACY = re.compile(r"pass|secret|token|cred|auth|key|account|ip(addr)?\b|email", re.IGNORECASE)


def run_scan(source):
    with tempfile.NamedTemporaryFile("w", suffix=".h", delete=False) as handle:
        handle.write(source)
        path = handle.name
    try:
        return subprocess.run([sys.executable, str(HERE / "gen_decision_capture.py"), "--scan-file", path],
                              capture_output=True, text=True)
    finally:
        pathlib.Path(path).unlink()


def schema_of(source):
    model = gen.scan_source(source, "<test>")
    return gen.schema_text(model, tuple(model.structs))


class GeneratorTest(unittest.TestCase):
    def test_check_passes_on_the_tree(self):
        result = subprocess.run([sys.executable, str(HERE / "gen_decision_capture.py"), "--check"],
                                capture_output=True, text=True, cwd=str(ROOT))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_two_declarators(self):
        model = gen.scan_source("struct S { float a = 0.0f, b = 0.0f; };", "<test>")
        struct = model.structs["S"]
        self.assertEqual([field.name for field in struct.fields], ["a", "b"])
        self.assertEqual([field.type_text for field in struct.fields], ["float", "float"])

    def test_function_member_type_names_the_field(self):
        result = run_scan("struct S { std::function<void()> cb; };\n")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("cb", result.stderr)

    def test_unknown_statement_reports_its_line(self):
        result = run_scan("struct S\n{\n    int x = 0;\n    #pragma pack(1)\n    int y = 0;\n};\n")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(":4:", result.stderr)

    def test_hash_follows_enumerators_types_and_order(self):
        base = "enum class Kind : std::uint8_t { A, B };\n" \
               "struct S { std::uint32_t x = 0; Kind k = Kind::A; };\n"
        swapped = base.replace("{ A, B }", "{ B, A }")
        retyped = base.replace("std::uint32_t x", "float x")
        reordered = "enum class Kind : std::uint8_t { A, B };\n" \
                    "struct S { Kind k = Kind::A; std::uint32_t x = 0; };\n"
        text = schema_of(base)
        self.assertIn("enum Kind: A,B", text)
        hashes = {gen.fnv1a64(schema_of(variant)) for variant in (base, swapped, retyped, reordered)}
        self.assertEqual(len(hashes), 4)

    def test_header_hash_is_fnv1a_of_the_schema_text(self):
        header = (ROOT / "runtime" / "DecisionCaptureFields.gen.h").read_text(encoding="utf-8")
        text = re.search(r'kDecisionSchemaText\[\] = R"TBSCHEMA\((.*)\)TBSCHEMA";', header, re.DOTALL)
        digest = re.search(r'kDecisionSchemaHash\[\] = "([0-9a-f]{16})";', header)
        self.assertIsNotNone(text)
        self.assertIsNotNone(digest)
        self.assertEqual("%016x" % gen.fnv1a64(text.group(1)), digest.group(1))

    def test_privacy_allowlist_is_complete(self):
        model = gen.load_model()
        structs, _enums, _table = gen.captured_graph(model)
        names = {field.name for _struct, fields in structs for field in fields}
        matches = {name for name in names if PRIVACY.search(name)}
        self.assertEqual(matches - set(PRIVACY_ALLOW), set(),
                         "new captured field name looks private: add it to PRIVACY_ALLOW with a reason")
        self.assertEqual(set(PRIVACY_ALLOW) - names, set(), "PRIVACY_ALLOW names a field that is not captured")


if __name__ == "__main__":
    unittest.main()
