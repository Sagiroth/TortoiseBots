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
#   stepPass/aheadNotedPass  run counters (how many times a step was entered)
PRIVACY_ALLOW = {
    "botKeysForPlayers": "doorKeys",
    "stepPass": "runPass",
    "aheadNotedPass": "runPass",
    "passSpotKnown": "islandPass",
    "passX": "islandPass",
    "passY": "islandPass",
    "passZ": "islandPass",
    "passed": "pullPassed",
}

PRIVACY = re.compile(r"pass|secret|token|cred|auth|key|account|ip(addr)?\b|email|pwd|pw\b|session|salt",
                     re.IGNORECASE)


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

    def test_std_function_member_is_skipped_and_named(self):
        # A callback is not data: left out of the captured fields, named in Struct.skipped (and the header comment).
        model = gen.scan_source("struct S { int a = 0; std::function<void()> cb; std::function<bool(float)> p, q; };",
                                "<test>")
        struct = model.structs["S"]
        self.assertEqual([field.name for field in struct.fields], ["a"])
        self.assertEqual(struct.skipped, ["cb", "p", "q"])

    def test_std_function_array_still_stops_the_generator(self):
        # Only a bare callback is skipped; an array of them is an unsupported type and names its field.
        result = run_scan("struct S { std::function<void()> cb[2]; };\n")
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
        self.assertIn("enum Kind: A=0,B=1", text)
        hashes = {gen.fnv1a64(schema_of(variant)) for variant in (base, swapped, retyped, reordered)}
        self.assertEqual(len(hashes), 4)

    def test_enum_values_are_in_the_schema(self):
        value = "enum class K { A = 1, B = 2 };\nstruct S { K k = K::A; };\n"
        other = "enum class K { A = 1, B = 5 };\nstruct S { K k = K::A; };\n"
        self.assertIn("enum K: A=1,B=2", schema_of(value))
        self.assertNotEqual(gen.fnv1a64(schema_of(value)), gen.fnv1a64(schema_of(other)),
                            "two enums that differ only in a value must hash differently")
        self.assertIn("enum K: A=1,B=2,C=3",
                      schema_of("enum class K { A = 1, B = 2, C };\nstruct S { K k = K::A; };\n"))
        self.assertIn("enum K: A=0,B=1",
                      schema_of("enum class K { A, B };\nstruct S { K k = K::A; };\n"))

    def test_enum_value_expression_is_refused(self):
        result = run_scan("enum class K { A = 1 << 2 };\nstruct S { K k = K::A; };\n")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("unrecognised enum entry", result.stderr)

    def test_base_class_is_an_unrecognised_statement(self):
        result = run_scan("struct Base {};\nstruct S : Base { int x = 0; };\n")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(":2:", result.stderr)
        self.assertIn("unrecognised statement", result.stderr)

    def test_constructor_brace_initialiser_is_an_unrecognised_statement(self):
        result = run_scan("struct S { int x = 0; S() : x{0} {} };\n")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("unrecognised statement", result.stderr)

    def test_alignas_and_decltype_fields_are_unrecognised(self):
        for source in ("struct S { alignas(8) int x = 0; };\n",
                       "struct S { decltype(1) x = 0; };\n"):
            result = run_scan(source)
            self.assertEqual(result.returncode, 2, source + result.stdout + result.stderr)
            self.assertIn("unrecognised statement", result.stderr)

    def test_union_is_an_error(self):
        result = run_scan("union U { int a = 0; float b = 0.0f; };\n")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("union", result.stderr)

    def test_optional_sample_is_the_codec_encoding(self):
        # The codec writes an optional as its element (or null), not as an array.
        model = gen.scan_source("struct S { std::optional<int> o; };\n", "<test>")
        gen.collect_nested_names(model)
        structs, _enums, _table = gen.captured_graph(model, tuple(model.structs))
        field = structs[0][1][0]
        self.assertEqual(gen.sample_json(model, field.node), "7")

    def test_header_hash_is_fnv1a_of_the_schema_text(self):
        header = (ROOT / "runtime" / "DecisionCaptureFields.gen.h").read_text(encoding="utf-8")
        chunks = re.findall(r'R"TBSCHEMA\((.*?)\)TBSCHEMA"(;?)', header, re.DOTALL)
        literals = [chunk for chunk, _tail in chunks]
        self.assertGreaterEqual(len(literals), 2, "schema literal must stay split for MSVC C2026")
        for chunk in literals:
            self.assertLessEqual(len(chunk.encode("utf-8")), 7000, "schema chunk too big for MSVC C2026")
        digest = re.search(r'kDecisionSchemaHash\[\] = "([0-9a-f]{16})";', header)
        self.assertIsNotNone(digest)
        self.assertEqual("%016x" % gen.fnv1a64("".join(literals)), digest.group(1))

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
