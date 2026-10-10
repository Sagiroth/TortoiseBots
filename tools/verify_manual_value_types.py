#!/usr/bin/env python3
"""Fail when a "manual ..." value is read with a type other than its registered one.

GetValue<T>() is a dynamic_cast: a Value<int32> fetched as Value<uint32> returns
null and the ->Get() crashes the world thread. The compiler cannot see this, so
check every GetValue<T>/AI_VALUE2/SET_AI_VALUE2 use of the manual values.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Registered type of each manual value (strategy/values/ValueContext.h + Value.h).
EXPECTED = {
    "manual bool": {"bool"},
    "manual int": {"int32", "int"},
    "manual saved int": {"int32", "int"},
    "manual string": {"std::string", "string"},
    "manual saved string": {"std::string", "string"},
    "manual time": {"time_t"},
}
PATTERN = re.compile(
    r'(?:GetValue<\s*([\w:]+)\s*>\s*\(|(?:SET_)?AI_VALUE2\(\s*([\w:]+)\s*,\s*)"(manual [a-z ]+?)"')

bad = []
for path in list(ROOT.glob("ai/**/*.cpp")) + list(ROOT.glob("ai/**/*.h")) + \
        list(ROOT.glob("runtime/**/*.cpp")) + list(ROOT.glob("commands/**/*.cpp")):
    text = path.read_text(errors="replace")
    for m in PATTERN.finditer(text):
        typ = m.group(1) or m.group(2)
        name = m.group(3)
        if name in EXPECTED and typ not in EXPECTED[name]:
            line = text.count("\n", 0, m.start()) + 1
            bad.append(f"{path.relative_to(ROOT)}:{line}: \"{name}\" read as {typ}, registered as {sorted(EXPECTED[name])[0]}")

if bad:
    print("manual value type mismatches (null dynamic_cast -> crash):")
    print("\n".join(bad))
    sys.exit(1)
print("manual value types OK")
