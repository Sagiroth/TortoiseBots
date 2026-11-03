#!/usr/bin/env python3
# Build the live-stream replay fixture for glide_replay_check.js: one card's frames in the order and at the page times they
# reach the page, as the dashboard sends them during a raid walk.
#
# What is recorded and what is built: the ZG walks (TortoiseBots-audit/tests/raid-zg-walk1, -walk2) kept no stream with arrival
# times while the raid moved (walk 1's sse-raw.txt is 24 idle frames, walk 2 has none), so this file is built, on the recorded
# facts: the units and mob ids of the Glacienne raid (walk 1's snapshot, walk 2's report at the stop), the recorded cadence (the
# COMMANDERS report every 2006-2013 ms, the 1 s idle COMMANDER_POS 49 ms after it, 0.5 s in combat), the module's 0.1 yd print,
# the WebSocket report carrying no ts_ms (internal/udp/listener.go: hub.Broadcast("commanders", view), the view's bindings have
# none), and one late report (a 1.3 s world hitch) which puts the page's 2 s refresh gate behind by the same for the minutes
# after (refresh.js: a report less than 1.6 s after the last applied one waits for the interval). The walk: on foot at 7 yd/s,
# a stop, a fight (0.5 s feed, small strafes), a walk on, a real turn back, a stand, and at the end the WebSocket down (the
# report then comes as the stream's own commander frame, with its ts_ms). Walk 2's page sample also froze for 14.8 s in a fight
# (smooth-walkA.json): here the page's event loop is blocked for 3 s in the fight, so no frame is drawn and the frames that
# arrived meanwhile are handled in one burst at its end.
# usage: python3 -I mk_walk_trace.py <out.json>
import json, math, random, sys

random.seed(1029)
S0 = 1793310148733          # walk 1's first COMMANDERS ts_ms
PAGE0 = 5000.0              # the page's performance.now() when that report was sent
END = 44000
COMBAT = (14000, 24000)
WS_DOWN = 38000
HITCH_AT = 6000             # the report after this one leaves the server 1.3 s late
FREEZE = (PAGE0 + 18000, PAGE0 + 21000)  # the page's event loop blocked
UNITS = [(808, "Wildfist", 0.0, 0.0, 0), (809, "Varin", -2.5, 1.5, 250), (810, "Ristra", -6.0, -2.0, 500),
         (811, "Ithan", -6.5, 2.5, 450), (812, "Radin", -4.0, -4.0, 350)]
MOBS = [("17379391152746227101", 11368, "Bloodseeker Bat", True), ("17379391152746227102", 11368, "Bloodseeker Bat", True),
        ("17379391209486729193", 14750, "Gurubashi Bat Rider", False)]   # the last is not in the position feed's watch list
X0, Y0 = -11918.5, -1235.2

def lead_x(t):  # server ms since S0 -> the lead's x offset (walk, stop, fight, walk, turn back, stand)
    pts = [(0, 0), (12000, 84), (14000, 84), (24000, 86), (32000, 142), (36000, 114), (END, 114)]
    for (ta, xa), (tb, xb) in zip(pts, pts[1:]):
        if t <= tb:
            return xa + (xb - xa) * (t - ta) / (tb - ta)
    return pts[-1][1]

def bot_at(u, t):
    g, _, ox, oy, lag = u
    x = lead_x(max(0, t - lag)) + ox
    y = oy
    if COMBAT[0] <= t < COMBAT[1] and g != 808:
        y += 2.0 * math.sin((t - COMBAT[0]) / 1500.0 + g)  # a dps strafe
    return round(X0 + x, 1), round(Y0 + y, 1)

def mob_at(i, t):
    if t < COMBAT[0]:
        return round(X0 + 100 + 4 * i, 1), round(Y0 + 6 - 3 * i, 1)
    f = min(1.0, (t - COMBAT[0]) / 3000.0)
    lx = lead_x(t)
    return round(X0 + (100 + 4 * i) * (1 - f) + (lx + 1.5 + i) * f, 1), round(Y0 + (6 - 3 * i) * (1 - f) + i * f, 1)

def report(t):
    bots = []
    for u in UNITS:
        x, y = bot_at(u, t)
        bots.append({"guid": u[0], "id": str(u[0]), "name": u[1], "x": x, "y": y, "z": 93.1, "map": 309, "alive": True})
    mobs = []
    for i, (mid, e, n, _) in enumerate(MOBS):
        x, y = mob_at(i, t)
        mobs.append({"id": mid, "entry": e, "name": n, "hp": 100, "x": x, "y": y, "reaction": "h"})
    return {"commander": "Glacienne", "commander_guid": 525, "online": True, "group": 2, "mobs_map": 309, "bots": bots, "mobs": mobs}

def pos(t):
    bots = []
    for u in UNITS:
        x, y = bot_at(u, t)
        bots.append({"g": u[0], "m": 309, "x": x, "y": y, "z": 93.1, "o": 0, "hp": 5000, "mhp": 5000, "a": 1})
    mobs = []
    for i, (mid, _, _, fed) in enumerate(MOBS):
        if fed:
            x, y = mob_at(i, t)
            mobs.append({"id": mid, "x": x, "y": y, "hp": 100, "v": ""})
    return {"commander": "Glacienne", "ts_ms": S0 + t, "combat": COMBAT[0] <= t < COMBAT[1], "bots": bots, "mobs": mobs}

frames = []
lat = lambda: 18 + random.random() * 45   # one-way delay to the page, ms
# the reports: every 2006-2013 ms as recorded
t, k = 0, 0
while t <= END:
    sent = t + (1300 if (t > HITCH_AT and t - 2007 <= HITCH_AT) else 0)
    at = PAGE0 + sent + lat()
    body = report(t)
    if t < WS_DOWN:
        frames.append([round(at, 1), "ws", {"commanders": [body], "fresh": True, "updated_at": (S0 + t) // 1000}])
    else:
        body = dict(body, ts_ms=S0 + t)
        frames.append([round(at, 1), "commander", body])
    t += random.choice([2006, 2007, 2007, 2009, 2013])
# the position frames: 49 ms after a report's clock, 1 s idle, 0.5 s in combat
t = 49
while t <= END:
    frames.append([round(PAGE0 + t + lat(), 1), "pos", pos(t)])
    t += 500 if COMBAT[0] <= t < COMBAT[1] else random.choice([1001, 1003, 1004, 1007])
for f in frames:
    if FREEZE[0] <= f[0] < FREEZE[1]:
        f[0] = FREEZE[1] + (f[0] - FREEZE[0]) / 1000.0  # handled in arrival order, all at the end of the block
frames.sort(key=lambda f: f[0])
json.dump({"about": "built by mk_walk_trace.py on the recorded ZG walk facts; [page ms, kind, data]; kind ws = the WebSocket "
           "commanders report (no ts_ms), commander = the stream's report frame (ts_ms), pos = COMMANDER_POS",
           "commander": "Glacienne", "freezes": [list(FREEZE)], "ws_down_at": PAGE0 + WS_DOWN, "end": PAGE0 + END + 500, "frames": frames},
          open(sys.argv[1], "w"), separators=(",", ":"))
