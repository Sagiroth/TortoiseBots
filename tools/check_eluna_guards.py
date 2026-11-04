#!/usr/bin/env python3
"""Eluna guard check: module code that talks to Eluna must compile on a core built with BUILD_ELUNA=ON.

The core keeps one Eluna state per map (Player/Map ::GetEluna()); the old global sEluna is gone.
Every Eluna call must sit inside #ifdef ENABLE_ELUNA (or PLAYERBOT_ELUNA) so a core built without
Eluna never sees it. CI no longer builds with Eluna; this check catches the common mistakes in about a second.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {".git", "build", "tools", "docs", "node_modules"}
GUARDS = ("ENABLE_ELUNA", "PLAYERBOT_ELUNA")
USE = re.compile(r"\bGetEluna\s*\(|\bEluna\s*\*|\bsEluna\b|\bsElunaConfig\b|\bsElunaMgr\b|#\s*include\s*[<\"](LuaEngine|ElunaConfig|ElunaMgr)\.h")
HEADER = re.compile(r"#\s*include\s*[<\"]LuaEngine\.h")
CALL = re.compile(r"\be\w*\s*->\s*On\w+\s*\(|\bEluna\s*\*\s*\w+\s*=")
LEGACY = re.compile(r"\bsEluna\b")
IF = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b(.*)")
ELSE = re.compile(r"^\s*#\s*(else|elif)\b")
ENDIF = re.compile(r"^\s*#\s*endif\b")


def check(path, rel):
    errors = []
    stack = []  # one bool per open #if: True while inside an Eluna-guarded branch
    first_call = 0  # first guarded call into Eluna (needs the full class, not just a pointer)
    has_header = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for n, line in enumerate(f, 1):
            m = IF.match(line)
            if m:
                kind, cond = m.group(1), m.group(2)
                guarded = kind != "ifndef" and "!" not in cond
                stack.append(next((g for g in GUARDS if guarded and g in cond), None))
                continue
            if ELSE.match(line):
                if stack:
                    stack[-1] = None
                continue
            if ENDIF.match(line):
                if stack:
                    stack.pop()
                continue
            code = line.split("//", 1)[0]
            if LEGACY.search(code):
                errors.append(f"{rel}:{n}: sEluna was removed; use `if (Eluna* e = player->GetEluna())`")
            elif USE.search(code) and not any(stack):
                errors.append(f"{rel}:{n}: Eluna use outside #ifdef ENABLE_ELUNA")
            elif "ENABLE_ELUNA" in stack:  # the core's Eluna; PLAYERBOT_ELUNA is a separate, never-defined fork API
                if HEADER.search(code):
                    has_header = True
                elif CALL.search(code) and not first_call:
                    first_call = n
    if first_call and not has_header and not rel.endswith((".h", ".hpp")):
        errors.append(f"{rel}:{first_call}: calls into Eluna without #include \"LuaEngine.h\" under #ifdef ENABLE_ELUNA")
    return errors


def main():
    errors = []
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if name.endswith((".cpp", ".h", ".hpp")):
                path = os.path.join(base, name)
                errors += check(path, os.path.relpath(path, ROOT))
    for e in errors:
        print(e)
    if errors:
        print(f"Eluna guard check FAILED ({len(errors)} problem(s))")
        return 1
    print("Eluna guard check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
