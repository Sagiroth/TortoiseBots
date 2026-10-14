"use strict";
// Run by commandersjs_test.go under node: the Commanders map drawing logic (paths.js): bot trails and their daemon history, run steps and
// their route, stall spots, objects, aggro radius, heal and crowd-control lines.
const assert = require("assert");
const P = require(require("path").resolve(process.argv[2]));

// ---- trails
const tr = P.newTrails();
const bind = (x, y, map = 209) => ({ commander: "C", bots: [{ guid: 1, name: "Lead", lead: true, map, x, y }, { guid: 2, name: "Tank", role: "tank", map, x: y, y: x }] });
P.noteTrails(tr, bind(0, 0), 100);
P.noteTrails(tr, bind(1, 0), 102);     // under 2 yd: no point
P.noteTrails(tr, bind(3, 0), 104);
assert.deepStrictEqual(P.trailOf(tr, "C", 1).pts, [[0, 0, 100], [3, 0, 104]]);
assert.strictEqual(P.trailOf(tr, "C", 9), null);
P.noteTrails(tr, bind(10, 10, 70), 106);   // another map: a new trail
assert.strictEqual(P.trailOf(tr, "C", 1).map, 70); assert.strictEqual(P.trailOf(tr, "C", 1).pts.length, 1);
for (let i = 0; i < 400; i++) P.noteTrails(tr, bind(10 + (i + 1) * 3, 10, 70), 200 + i);
assert.strictEqual(P.trailOf(tr, "C", 1).pts.length, P.TRAIL_MAX, "a ring buffer: bounded");
assert.strictEqual(P.trailOf(tr, "C", 1).pts[P.TRAIL_MAX - 1][2], 599, "newest kept");
P.noteTrails(tr, { commander: "C", bots: [{ guid: 0, map: 1, x: 1, y: 1 }, { guid: 5, map: 1, x: "a", y: 1 }, null] }, 1);
assert.strictEqual(P.trailOf(tr, "C", 5), null, "malformed bots are skipped");

// trailPoints: the slider's length, only on the same map
const t1 = P.trailOf(tr, "C", 1);
assert.strictEqual(P.trailPoints(t1, 70, 10).length, 10);
assert.strictEqual(P.trailPoints(t1, 70, 10)[9][2], 599);
assert.strictEqual(P.trailPoints(t1, 70, 9999).length, P.TRAIL_MAX);
assert.strictEqual(P.trailPoints(t1, 209, 50).length, 0, "another map");
assert.strictEqual(P.trailPoints(null, 70, 50).length, 0);
assert.strictEqual(P.trailPoints(t1, 70, "x").length, P.TRAIL_LEN_DEFAULT);

// member trails leave out the lead and short ones
const mt = P.newTrails();
P.noteTrails(mt, bind(0, 0), 1); P.noteTrails(mt, bind(5, 0), 3);
const ms = P.memberTrails(mt, bind(5, 0), 209, 120);
assert.strictEqual(ms.length, 1); assert.strictEqual(ms[0].bot.name, "Tank"); assert.strictEqual(ms[0].pts.length, 2);
assert.strictEqual(P.memberTrails(mt, bind(5, 0), 70, 120).length, 0);

// the daemon's history goes in front, only older points on the same map, junk dropped
const sd = P.newTrails();
P.noteTrails(sd, bind(100, 0), 500);
const taken = P.seedTrails(sd, {
  C: {
    1: { map: 209, pts: [[1, 2, 100], [3, 4, 200], [100, 0, 500], [5, 5, 600], ["x", 1, 2], [1, 2]] },
    2: { map: 70, pts: [[1, 2, 100]] },   // another map: ignored
    zz: { map: 209, pts: [[1, 2, 100]] }, // not a guid
    3: { map: 209, pts: [[7, 7, 10], [8, 8, 20]] }, // a bot this page has not seen yet
  },
  Gone: { 1: { map: 209, pts: [[1, 1, 1]] } },
  bad: 5,
});
assert.strictEqual(taken, 2, "points taken: the two older ones of bot 1");
assert.deepStrictEqual(P.trailOf(sd, "C", 1).pts.map((p) => p[2]), [100, 200, 500]);
assert.strictEqual(P.trailOf(sd, "C", 3), null, "a bot this page never saw report is not created from history");
assert.strictEqual(P.trailOf(sd, "Gone", 1), null, "nor a binding");
assert.strictEqual(P.trailOf(sd, "C", 2).pts.length, 1, "bot 2 keeps its own point on its own map");
assert.strictEqual(P.seedTrails(sd, null), 0); assert.strictEqual(P.seedTrails(sd, "x"), 0);
assert.strictEqual(P.seedTrails(sd, { C: { 1: { map: 209, pts: [[1, 2, 100], [3, 4, 200]] } } }), 0, "seeding twice adds nothing");
const big = []; for (let i = 0; i < 1000; i++) big.push([i, i, i]);
assert.strictEqual(P.cleanPoints(big).length, P.TRAIL_MAX); assert.strictEqual(P.cleanPoints("x").length, 0);

// forgetting
P.pruneTrails(sd, [{ commander: "C", bots: [{ guid: 1 }] }]);
assert.ok(P.trailOf(sd, "C", 1)); assert.strictEqual(P.trailOf(sd, "C", 2), null, "a bot that left the binding");
P.pruneTrails(sd, []);
assert.strictEqual(P.trailOf(sd, "C", 1), null);

// ---- run steps
const run = {
  step: 42, plan_steps: [
    { i: 44, k: "boss", x: 5, y: 5, r: 30, e: 4854, n: "Grimlok" }, { i: 40, k: "move", x: 0, y: 0 }, { i: 41, k: "pull", x: 1, y: 1, r: 15, sk: true },
    { i: 42, k: "pull", x: 2, y: 2, r: 15 }, { i: 43, k: "open", x: 3, y: 3, e: 2087 }, { i: 45, k: "bogus", x: 1, y: 1 }, { i: 46, k: "move", x: "a", y: 1 }, null,
    { i: 47, k: "rest", x: 6, y: 6, r: 500 }, { i: 48, k: "ritual", x: 7, y: 7, n: 5 },
  ],
};
const st = P.runSteps(run);
assert.deepStrictEqual(st.map((s) => s.i), [40, 41, 42, 43, 44, 47, 48], "sorted, malformed and unknown kinds dropped");
assert.deepStrictEqual(st.map((s) => s.state), ["cleared", "skipped", "next", "todo", "todo", "todo", "todo"]);
assert.strictEqual(st.find((s) => s.i === 47).r, 100, "radius capped");
assert.strictEqual(st.find((s) => s.i === 44).n, "Grimlok"); assert.strictEqual(st.find((s) => s.i === 48).n, "");
assert.deepStrictEqual(P.runSteps({ step: 1 }), []); assert.deepStrictEqual(P.runSteps(null), []); assert.deepStrictEqual(P.runSteps({ plan_steps: "x" }), []);
assert.strictEqual(P.runSteps({ step: 1, plan_steps: new Array(100).fill({ i: 1, k: "move", x: 1, y: 1 }) }).length, 24, "capped");
const seg = P.routeSegments(st);
assert.strictEqual(seg.length, 2);
assert.strictEqual(seg[0].done, true); assert.strictEqual(seg[0].points.length, 3, "cleared and skipped, plus the first point ahead");
assert.strictEqual(seg[1].done, false); assert.strictEqual(seg[1].points.length, 5);
assert.deepStrictEqual(seg[0].points[2], seg[1].points[0], "the two lines meet");
assert.deepStrictEqual(P.routeSegments([]), []);
assert.strictEqual(P.routeSegments(st.slice(3)).length, 1, "only what is ahead");
assert.deepStrictEqual(P.numberedSteps(st).map((s) => s.i), [41, 42, 44]);
assert.deepStrictEqual(["boss", "loot", "open", "use_item", "ritual", "rest", "wait_object", "wait_clear", "move", "pull"].map(P.stepIcon),
  ["crown", "coin", "door", "door", "spark", "stop", "stop", "stop", "", ""]);
assert.ok(/Step 42: Pull, radius 15 yd \(the step the run is on\)/.test(P.stepText(st[2])));
assert.ok(/Kill the boss Grimlok, radius 30 yd \(coming up\)/.test(P.stepText(st[4])));
assert.ok(/skipped/.test(P.stepText(st[1])) && /done/.test(P.stepText(st[0])));

// ---- stall spots
const sl = P.newStalls();
const paused = { commander: "C", orders: [], run: { state: "paused", step: 42, note: "a unit survived 3 pulls", changed: 1000 } };
P.noteStalls(sl, paused, 1001, [], { x: 9, y: 9 }, st, 209);
let list = P.stallsOf(sl, "C", 209);
assert.strictEqual(list.length, 1);
assert.deepStrictEqual([list[0].kind, list[0].x, list[0].y, list[0].live], ["pause", 2, 2, true], "a pause sits on its step");
assert.ok(/Run paused: a unit survived/.test(P.stallText(list[0])));
P.noteStalls(sl, paused, 1003, [], { x: 50, y: 50 }, st, 209);
assert.strictEqual(P.stallsOf(sl, "C", 209).length, 1, "noted once");
assert.strictEqual(P.stallsOf(sl, "C", 70).length, 0, "only on its own map");
P.noteStalls(sl, { commander: "C", orders: [], run: { state: "running", step: 43, changed: 1010 } }, 1010, [], null, st, 209);
assert.strictEqual(P.stallsOf(sl, "C", 209)[0].live, false, "resumed: the spot stays, dimmed");
assert.ok(/resumed since/.test(P.stallText(P.stallsOf(sl, "C", 209)[0])));
// no step known: the lead's place; no place at all: nothing
const sl2 = P.newStalls();
P.noteStalls(sl2, paused, 1001, [], { x: 9, y: 9 }, [], 209);
assert.deepStrictEqual([P.stallsOf(sl2, "C", 209)[0].x, P.stallsOf(sl2, "C", 209)[0].y], [9, 9]);
const sl3 = P.newStalls();
P.noteStalls(sl3, paused, 1001, [], null, [], 209);
assert.strictEqual(P.stallsOf(sl3, "C", 209).length, 0);
// refused orders: a move names its point, others use where the lead was; accepted and non-movement orders are not stalls
const orders = [
  { at: 2000, verb: "move", detail: "1517 833 9", ok: false, result: "no-path: No walkable path." },
  { at: 2010, verb: "attack", detail: "0xF13000", ok: false, result: "invalid-target: No unit with that GUID" },
  { at: 2020, verb: "move", detail: "1 2 3", ok: true, result: "party 5" },
  { at: 2030, verb: "state", detail: "", ok: false, result: "x: y" },
  { at: 2040, verb: "follow", detail: "", ok: false, result: "" },
];
const trail = [[10, 10, 1990], [20, 20, 2005], [30, 30, 2015]];
const sl4 = P.newStalls();
P.noteStalls(sl4, { commander: "C", orders }, 2050, trail, { x: 99, y: 99 }, [], 209);
const rs = P.stallsOf(sl4, "C", 209);
assert.deepStrictEqual(rs.map((s) => [s.verb, s.x, s.y]), [["move", 1517, 833], ["attack", 20, 20], ["follow", 30, 30]], "points: named, then where the lead was at the time");
assert.strictEqual(rs[0].reason, "No walkable path"); assert.strictEqual(rs[1].reason, "No unit with that GUID"); assert.strictEqual(rs[2].reason, "no reason given");
assert.ok(/^Move refused: No walkable path$/.test(P.stallText(rs[0])));
P.noteStalls(sl4, { commander: "C", orders }, 2052, trail, { x: 99, y: 99 }, [], 209);
assert.strictEqual(P.stallsOf(sl4, "C", 209).length, 3, "the same refusal is not noted twice");
P.noteStalls(sl4, { commander: "C", orders: [] }, 2010 + P.STALL_KEEP_SEC + 5, trail, null, [], 209);
assert.strictEqual(P.stallsOf(sl4, "C", 209).length, 1, "old ones expire after 15 min");
// bounded
const sl5 = P.newStalls(), many = [];
for (let i = 0; i < 40; i++) many.push({ at: 3000 + i, verb: "move", detail: i + " 1", ok: false, result: "e: bad" });
P.noteStalls(sl5, { commander: "C", orders: many }, 3100, [], null, [], 1);
assert.strictEqual(P.stallsOf(sl5, "C", 1).length, P.STALL_MAX);
assert.strictEqual(P.stallsOf(sl5, "C", 1)[P.STALL_MAX - 1].x, 39, "the newest are kept");
P.pruneStalls(sl5, []);
assert.strictEqual(P.stallsOf(sl5, "C", 1).length, 0);
assert.deepStrictEqual(P.orderPoint("to 5 6"), null); assert.deepStrictEqual(P.orderPoint("-3.5, 4"), [-3.5, 4]); assert.strictEqual(P.orderPoint(""), null);
assert.strictEqual(P.placeAt([], 1), null); assert.deepStrictEqual(P.placeAt([[1, 2, 10], [3, 4, 20]], 5), { x: 1, y: 2 }, "before the first: the first");

// ---- objects
const ob = P.objectsOf({
  mobs_map: 209, objects: [
    { id: "1", entry: 5, name: "Door", x: 1, y: 2, type: "d", state: "r" }, { id: "2", name: "N".repeat(80), x: 1, y: 2, type: "z", state: "?" },
    { id: "x", x: 1, y: 2 }, { id: "3", x: "a", y: 2 }, null,
  ],
}, 209);
assert.strictEqual(ob.length, 2);
assert.deepStrictEqual([ob[1].type, ob[1].state, ob[1].name.length], ["o", "r", 40], "unknown letters read as object / closed; names clipped");
assert.deepStrictEqual(P.objectsOf({ mobs_map: 209, objects: [] }, 70), [], "another map");
assert.deepStrictEqual(P.objectsOf({ mobs_map: 209 }, 209), [], "not reported");
assert.deepStrictEqual(P.objectsOf(null, 209), []);
assert.strictEqual(P.objectsOf({ mobs_map: 1, objects: new Array(60).fill({ id: "1", x: 1, y: 1 }) }, 1).length, 24);
assert.strictEqual(P.objectText({ name: "Ancient Chest", type: "c", state: "u" }), "Ancient Chest - chest, used");
assert.strictEqual(P.objectText({ name: "", type: "g", state: "a" }), "Gong or altar - gong or altar, open");

// ---- aggro radius: 20 yd at equal level, one more per level above the party, one less below, 5-45
assert.strictEqual(P.aggroRadius(40, 40), 20);
assert.strictEqual(P.aggroRadius(45, 40), 25); assert.strictEqual(P.aggroRadius(38, 40), 18);
assert.strictEqual(P.aggroRadius(70, 40), 45, "capped"); assert.strictEqual(P.aggroRadius(5, 60), 5, "floored");
assert.strictEqual(P.aggroRadius(0, 40), 20); assert.strictEqual(P.aggroRadius(40, 0), 20); assert.strictEqual(P.aggroRadius(undefined, undefined), 20);

// ---- combat lines
const bots = [
  { id: "10", name: "Priest", role: "healer", alive: true, cast: "Flash Heal", ctgt: "11" },
  { id: "11", name: "Tank", role: "tank", alive: true },
  { id: "12", name: "Dead", alive: false },
  { id: "13", name: "Mage", role: "dps", alive: true, cast: "Polymorph", ctgt: "900" },
  { id: "14", name: "OldPriest", role: "healer", alive: true, target: "11" },       // an older module: the selection stands in
  { id: "15", name: "Self", role: "healer", alive: true, cast: "Renew", ctgt: "15" },
  { id: "16", name: "ToDead", role: "healer", alive: true, cast: "Resurrection", ctgt: "12" },
  { id: "17", name: "Locked", role: "dps", alive: true, cast: "Fireball", ctgt: "901" },
];
const hl = P.healLines(bots);
assert.deepStrictEqual(hl.map((l) => [l.from.name, l.to.name]), [["Priest", "Tank"], ["OldPriest", "Tank"]], "healer to ally; not itself, not the dead");
const mobs = [{ id: "900", cc: "c", x: 1, y: 1 }, { id: "901", x: 2, y: 2 }];
assert.deepStrictEqual(P.ccLines(bots, mobs).map((l) => [l.from.name, l.to.id]), [["Mage", "900"]], "only a mob that is crowd controlled");
assert.deepStrictEqual(P.ccLines([], mobs), []); assert.deepStrictEqual(P.healLines(null), []);
assert.strictEqual(P.hasCastTargets(bots), true); assert.strictEqual(P.hasCastTargets([{ id: "1" }]), false);
console.log("paths.js ok");
