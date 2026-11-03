#!/usr/bin/env python3
# Build a fight-track fixture from one recorded Onyxia attempt, for the aggrolanes check: the attempt's sampler lines
# (samples.jsonl) become the daemon's track answer shape (GET /api/v1/commanders/track) plus a finished-fight record.
# The result is data only: the JS lanes and the Go history port read the same file.
# usage: python3 -I mk_lanes_fixture.py <attempt dir> <out.json>
import json, math, os, sys
d, out = sys.argv[1], sys.argv[2]
p2 = json.load(open(os.path.join(d, "p2.json"))); cs = p2["commanders"] if "commanders" in p2 else [p2]
binds = cs[0]["bots"]; byname = {b["name"]: b for b in binds}; gs = {str(b["guid"]) for b in binds}
frames, start, last = [], None, None
for line in open(os.path.join(d, "samples.jsonl")):
    r = json.loads(line); o = r.get("ony"); t = int(math.floor(r["t"])); bots = []
    for b in r["bots"]:
        g = byname.get(b["name"], {}).get("guid")
        if not g: continue
        hp = math.floor(b["hp"] * 100 / b["max_hp"] + 0.5) if b["max_hp"] else 0
        bots.append([g, round(b["x"], 1), round(b["y"], 1), hp, 1 if b["alive"] else 0, 249])
    mobs = []
    if o:
        m = {"id": o["id"], "e": o["entry"], "x": round(o["x"], 1), "y": round(o["y"], 1), "hp": o["hp"], "rk": o.get("rank") or "b"}
        v = str(o.get("victim") or "")
        if v in gs: m["v"] = int(v)
        elif v: m["vo"] = 1
        mobs.append(m)
        if start is None and (v or o["hp"] < 100): start = t
        if start is not None and any(b[4] for b in bots): last = t
    if frames and frames[-1]["t"] == t: continue
    frames.append({"t": t, "map": 249, "step": 0, "fight": bool(o), "bots": bots, "mobs": mobs})
fight = {"start": start, "dur_ms": (last - start) * 1000 + 2000, "map": 249,
         "bots": [{"name": b["name"], "guid": b["guid"], "class": b["class"], "role": b["role"]} for b in binds]}
json.dump({"track": {"commander": cs[0]["commander"], "frames": frames, "names": {"10184": "Onyxia"}}, "fight": fight},
          open(out, "w"), separators=(",", ":"))
