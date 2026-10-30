#!/usr/bin/env python3
"""Fail when a code config default drifts from the shipped templates.

AGENTS.md rule: a key's code fallback (`Get*Default`) must equal the value the
shipped template sets, and a commented example must show the value that actually
runs — otherwise deleting a template line silently changes behaviour.

Scope: every statically visible `config.Get(Int|Bool|Float|String)Default(
"AiPlayerbot.X", <literal>)` read in the module, compared against
`ai/playerbot/aiplayerbot.conf.dist.in` and `conf/tortoise_bots.conf.dist`
(AiPlayerbot keys may live in either file). Keys read through a dynamically
built name (`key.c_str()`, `os.str()`) cannot be checked statically and are
skipped. Template keys nothing reads are not checked (the reverse direction).

Comparison semantics:
- an active template line is the shipped value: the code fallback must equal it;
- if only commented examples exist, at least one must equal the code fallback
  (keys documented with several example payloads, e.g. LLMApiJson, match any);
- booleans compare as 0/1, numbers compare numerically (`0.5` == `0.50`),
  strings compare after C escape decoding and whitespace collapsing;
- simple arithmetic defaults (`6 * 3600`) are evaluated;
- genuinely computed defaults are exempt via ALLOWLIST below, each with a reason.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEMPLATES = ["ai/playerbot/aiplayerbot.conf.dist.in", "conf/tortoise_bots.conf.dist"]

CALL = re.compile(
    r'Get(?:Int|Bool|Float|String)Default\(\s*"(AiPlayerbot\.[A-Za-z0-9_.]+)"\s*,\s*')

# Keys whose code fallback is genuinely computed (not a literal). Reason must
# say where the constant lives and why the value still matches the template.
ALLOWLIST = {
    "AiPlayerbot.ConfigFile":
        "meta-key: names an extra config file at bootstrap (PlayerbotAIConfig.cpp "
        "Initialize); empty default = feature off, so there is nothing to ship",
    "AiPlayerbot.ObservabilityHost":
        "empty fallback is deliberate: ObservabilityEmitter::Initialize resolves an "
        "empty host via the OBSERVABILITY_HOST env var, then loopback; "
        "conf/tortoise_bots.conf.dist ships the same effective value (127.0.0.1)",
    "AiPlayerbot.AmmoStacksNonHunter":
        "default is the constant ai::kAmmoStacksNonHunterDefault = 2 "
        "(AmmoStackPolicy.h); asserted equal to the template value by "
        "tools/test_verify_config_defaults.py",
    "AiPlayerbot.HireRaidPresets":
        "default is the constant TortoiseBots::kHireRaidPresetsDefault "
        "(runtime/HireRaidPolicy.h); asserted equal to the template value by "
        "tools/test_verify_config_defaults.py",
    "AiPlayerbot.BotGearPresets":
        "default is the constant TortoiseBots::BotGear::kPresetsDefault "
        "(runtime/policy/hire/BotGearPolicy.h); asserted equal to the template value by "
        "tools/test_bot_gear_wiring.py",
    "AiPlayerbot.HireRaidSpread":
        "default is the constant TortoiseBots::kHireRaidSpreadDefault "
        "(runtime/HireRaidPolicy.h); asserted equal to the template value by "
        "tools/test_verify_config_defaults.py",
    "AiPlayerbot.SelfBotLevel":
        "default is enum BotSelfBotLevel::GM_ONLY = 1 (PlayerbotAIConfig.h), equal "
        "to the commented template example",
    "AiPlayerbot.MaxRandomBotRandomizeTime":
        "default delegates to AiPlayerbot.MaxRandomRandomizeTime (PlayerbotAIConfig "
        "LoadUpdates), which itself defaults to 86400; the template documents both "
        "keys",
    "AiPlayerbot.LLMApiJson":
        "the template's first example differs from the code default only in "
        "insignificant JSON whitespace, and its second example documents the "
        "OpenAI-style chat-completions payload for a different LLMApiEndpoint",
    "AiPlayerbot.RemoteCommanderViewMs":
        "default is the constant TortoiseBots::RemoteCommander::View::"
        "kDefaultIntervalMs = 1000 (runtime/RemoteCommanderView.h), equal to the "
        "commented template example",
    "AiPlayerbot.RunTrace.IntervalMs":
        "default is the constant TortoiseBots::RunTrace::kDefaultIntervalMs = 500 "
        "(runtime/RunTraceRecords.h), equal to the commented template example",
    "AiPlayerbot.RunTrace.MaxFileMb":
        "default is the constant TortoiseBots::RunTrace::kDefaultMaxFileMb = 64 "
        "(runtime/RunTraceRecords.h), equal to the commented template example",
}


def extract_expr(text, start):
    """Return the balanced default expression beginning at `start`."""
    i, n = start, len(text)
    depth = 0
    while i < n:
        c = text[i]
        if c == 'R' and i + 1 < n and text[i + 1] == '"':
            end = text.find(')"', i + 3)  # skip the R"( delimiter paren too
            if end == -1:
                return None
            i = end + 2
            continue
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
            i += 1
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            if depth == 0:
                return text[start:i].strip()
            depth -= 1
        i += 1
    return None


ESCAPES = {'\\': '\\', '"': '"', 'n': '\n', 't': '\t', 'r': '\r', '0': '\0', "'": "'"}


def decode_string(literal):
    out, i = [], 1  # skip opening quote
    while i < len(literal) and literal[i] != '"':
        if literal[i] == '\\' and i + 1 < len(literal):
            out.append(ESCAPES.get(literal[i + 1], literal[i + 1]))
            i += 2
        else:
            out.append(literal[i])
            i += 1
    return ''.join(out)


def is_string_expr(expr):
    """True for one or more adjacent string literals / raw strings."""
    i, n = 0, len(expr)
    while i < n:
        if expr.startswith('R"', i):
            end = expr.find(')"', i + 3)
            if end == -1:
                return False
            i = end + 2
        elif expr[i] == '"':
            i += 1
            while i < n and expr[i] != '"':
                i += 2 if expr[i] == '\\' else 1
            if i >= n:
                return False
            i += 1
        elif expr[i] in ' \t\n\r':
            i += 1
        else:
            return False
    return i > 0


def parse_default(expr):
    """Evaluate a C++ default expression; return the value or None if computed."""
    expr = expr.strip()
    m = re.fullmatch(r'static_cast<[^>]+>\((.*)\)', expr, re.S)
    if m:
        return parse_default(m.group(1))
    m = re.fullmatch(r'(?:uint32|int32|uint8|int8|uint16|int16|float|double|uint|int|size_t)\((.*)\)',
                     expr, re.S)
    if m:
        return parse_default(m.group(1))
    if expr == 'true':
        return True
    if expr == 'false':
        return False
    if re.fullmatch(r'[+-]?\d+', expr):
        return int(expr)
    if re.fullmatch(r'[+-]?(?:\d+\.\d*|\.\d+|\d+\.?)[fF]?', expr):
        return float(expr.rstrip('fF'))
    # simple integer arithmetic: 6 * 3600, 1 * 1800, 2 * 3600
    if re.fullmatch(r'\d+\s*\*\s*\d+(?:\s*\*\s*\d+)*', expr):
        v = 1
        for t in re.findall(r'\d+', expr):
            v *= int(t)
        return v
    # one or more adjacent string literals ("a" "b"), optionally raw strings
    if is_string_expr(expr):
        value, i, n = '', 0, len(expr)
        while i < n:
            if expr[i] in ' \t\n\r':
                i += 1
            elif expr.startswith('R"', i):
                end = expr.find(')"', i + 3)
                value += expr[i + 3:end]
                i = end + 2
            else:
                j = i + 1
                while expr[j] != '"':
                    j += 2 if expr[j] == '\\' else 1
                value += decode_string(expr[i:j + 1])
                i = j + 1
        return value
    # default delegates to another key's read
    if re.fullmatch(r'\w+\s*\.\s*Get\w+Default\(.+', expr, re.S):
        return DELEGATED
    return None


DELEGATED = object()


def collapse_ws(s):
    return ' '.join(s.split())


def strip_outer_quotes(v):
    if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
        return v[1:-1]
    return v


def values_equal(code_value, template_value):
    if isinstance(code_value, bool):
        return template_value in ('1', '0') and int(template_value) == int(code_value)
    if isinstance(code_value, (int, float)):
        try:
            return abs(float(template_value) - float(code_value)) < 1e-6
        except ValueError:
            return False
    if isinstance(code_value, str):
        # template authors sometimes quote string values; the quotes are not
        # part of the value
        return collapse_ws(code_value) == collapse_ws(strip_outer_quotes(template_value))
    return False


def parse_template(path):
    """Return {key: [(active, value), ...]} for AiPlayerbot.* lines."""
    entries = {}
    line_re = re.compile(r'^(#?\s*)(AiPlayerbot\.[A-Za-z0-9_.]+)\s*=\s*(.*?)\s*$')
    for line in path.read_text(errors='replace').splitlines():
        m = line_re.match(line)
        if not m:
            continue
        active = not m.group(1).startswith('#')
        value = re.split(r'\s+#', m.group(3))[0].rstrip()
        entries.setdefault(m.group(2), []).append((active, value))
    return entries


def scan_code(root):
    """Return [(file, line, key, expr)], skipping tools/ and build dirs."""
    calls = []
    skip = {'tools', 'data', 'docs', 'conf', 'scripts', 'build'}
    for path in sorted(root.rglob('*')):
        if path.suffix not in ('.cpp', '.h') or path.is_dir():
            continue
        if any(part in skip for part in path.relative_to(root).parts):
            continue
        text = path.read_text(errors='replace')
        for m in CALL.finditer(text):
            expr = extract_expr(text, m.end())
            line = text.count('\n', 0, m.start()) + 1
            calls.append((path.relative_to(root), line, m.group(1), expr))
    return calls


def check(root=None):
    """Run the check; return a list of human-readable findings."""
    root = pathlib.Path(root) if root else ROOT
    findings = []
    template = {}
    for rel in TEMPLATES:
        for key, entries in parse_template(root / rel).items():
            template.setdefault(key, []).extend((rel,) + e for e in entries)

    defaults = {}
    for rel, line, key, expr in scan_code(root):
        if expr is None:
            findings.append(f"{rel}:{line}: {key}: unparseable default expression")
            continue
        value = parse_default(expr)
        if value is DELEGATED or value is None:
            if key not in ALLOWLIST:
                findings.append(
                    f"{rel}:{line}: {key}: computed default `{expr.strip()}` needs an "
                    "ALLOWLIST entry with a reason")
                continue
        if key not in ALLOWLIST:
            defaults.setdefault(key, []).append((rel, line, value, expr.strip()))

    for key in sorted(defaults):
        sites = defaults[key]
        uniq = {repr(s[2]): s[2] for s in sites}.values()
        if len(uniq) > 1:
            where = ', '.join(f"{r}:{l}" for r, l, _, _ in sites)
            findings.append(f"{key}: read with different defaults at {where}")
            continue
        value = sites[0][2]
        entries = template.get(key)
        if not entries:
            findings.append(
                f"{key}: code default `{sites[0][3]}` but no `{key}` entry in "
                f"{', '.join(TEMPLATES)} (read at {sites[0][0]}:{sites[0][1]})")
            continue
        active = [(src, v) for src, act, v in entries if act]
        commented = [(src, v) for src, act, v in entries if not act]
        if active:
            for src, v in active:
                if not values_equal(value, v):
                    findings.append(
                        f"{key}: code default `{sites[0][3]}` != active template value "
                        f"`{v}` in {src} (read at {sites[0][0]}:{sites[0][1]})")
        else:
            if not any(values_equal(value, v) for _, v in commented):
                shown = '; '.join(f"`{v}`" for _, v in commented)
                findings.append(
                    f"{key}: code default `{sites[0][3]}` matches none of the commented "
                    f"template examples {shown} (read at {sites[0][0]}:{sites[0][1]})")
    return findings


def main():
    findings = check()
    for key, reason in sorted(ALLOWLIST.items()):
        print(f"allowlisted {key}: {reason}")
    if findings:
        print()
        for f in findings:
            print(f"DRIFT {f}")
        print(f"\n{len(findings)} config default(s) drift from the shipped templates.")
        return 1
    print(f"\nAll code config defaults match the shipped templates "
          f"({', '.join(TEMPLATES)}).")
    return 0


if __name__ == '__main__':
    sys.exit(main())
