"use strict";
// Run by commandersjs_test.go under node: the aggro lanes of a fight (web/static/js/aggrolanes.js).
// The fixture is walk 2 (Onyxia's Lair, commander Glacienne), the same track the replay reads.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const A = require(path.resolve(process.argv[2]));

const fights = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-fights.json"), "utf8"));
const track = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-track.json"), "utf8"));
const byId = (id) => fights.find((f) => f.id === id);

// ---- exports --------------------------------------------------------------------------------------------------------
assert.strictEqual(A.MAX_LANES, 12);
assert.strictEqual(A.HOLD_S, 3);

// ---- the synthetic fight ----------------------------------------------------------------------------------------------
const BOTS = [
  { name: "Tank", guid: 1, role: "tank" },
  { name: "Heal", guid: 2, role: "healer" },
  { name: "Dps", guid: 3, role: "dps" },
];
const FIGHT = { start: 1000, dur_ms: 10000, bots: BOTS };
const bot = (guid, x) => [guid, x, 0, 100, 1, 249];
const m = (id, v, extra) => Object.assign({ id, e: 7, x: 0, y: 0, hp: 100 }, v == null ? {} : { v }, extra || {});
const fr = (t, mobs) => ({ t, map: 249, step: 20, fight: true, bots: [bot(1, 0), bot(2, 0), bot(3, 0)], mobs });
const wrap = (frames) => ({ commander: "C", frames, names: { 7: "Warder" } });
const segsOf = (lane) => lane.segs.map((s) => [s.fromMs, s.toMs, s.kind, s.bad, s.victim]);

// A frame before the window is used only as the edge; the last frame holds 2 s.
const frames = [
  fr(998, [m("m1", 1)]),
  fr(1000, [m("m1", 1)]),
  fr(1002, [m("m1", 2)]),
  fr(1004, [m("m1", 2)]),
  fr(1006, [m("m1", 1)]),
  fr(1008, [m("m1", 1), m("m2")]),
  fr(1010, [m("m1", null, { hp: 0 })]),
];
const res = A.lanes(wrap(frames), FIGHT, []);
assert.strictEqual(res.frames, 7);
assert.strictEqual(res.more, 0);
assert.strictEqual(res.lanes.length, 1);
const l1 = res.lanes[0];
assert.strictEqual(l1.id, "m1");
assert.strictEqual(l1.e, 7);
assert.strictEqual(l1.name, "Warder");
assert.strictEqual(l1.label, "Warder");
assert.strictEqual(l1.boss, false);
assert.deepStrictEqual(segsOf(l1), [
  [0, 2000, "tank", false, "Tank"],
  [2000, 6000, "healer", true, "Heal"],
  [6000, 10000, "tank", false, "Tank"],
]);
assert.strictEqual(l1.badMs, 4000);
assert.strictEqual(l1.onTankPct, 60);
assert.deepStrictEqual(l1.badNames, ["Heal"]);

// Hold caps a segment at 3 s; the next report of the mob starts its own segment.
const sparse = A.lanes(wrap(frames.filter((f) => f.t !== 1004 && f.t !== 1006)), FIGHT, []);
assert.deepStrictEqual(segsOf(sparse.lanes[0]), [
  [0, 2000, "tank", false, "Tank"],
  [2000, 5000, "healer", true, "Heal"],
  [8000, 10000, "tank", false, "Tank"],
]);
assert.strictEqual(sparse.lanes[0].badMs, 3000);
assert.strictEqual(sparse.lanes[0].onTankPct, 57);

// Two enemies of one entry get numbered labels; the healer is red for the damage dealer too.
const both = frames.map((f) => {
  const mobs = f.mobs.slice();
  if (f.t === 1002 || f.t === 1004) mobs.push(m("m3", 3));
  return Object.assign({}, f, { mobs });
});
const two = A.lanes(wrap(both), FIGHT, []);
assert.strictEqual(two.lanes.length, 2);
assert.deepStrictEqual(two.lanes.map((l) => l.label), ["Warder 1", "Warder 2"]);
assert.deepStrictEqual(segsOf(two.lanes[1]), [[2000, 6000, "dps", true, "Dps"]]);

// A gap between reports of one enemy is a gap, not a merged segment; the fight's roles win over the bind's.
const gapFrames = frames.map((f) => {
  const mobs = f.mobs.slice();
  if (f.t === 1000 || f.t === 1004) mobs.push(m("m7", 1));
  return Object.assign({}, f, { mobs });
});
const gap = A.lanes(wrap(gapFrames), FIGHT, [{ guid: 1, name: "Tank", role: "healer" }]);
const l7 = gap.lanes.find((l) => l.id === "m7");
assert.deepStrictEqual(segsOf(l7), [[0, 2000, "tank", false, "Tank"], [4000, 6000, "tank", false, "Tank"]]);

// A boss lane comes first, whatever its first segment.
const withBoss = frames.map((f) => (f.t === 1008
  ? Object.assign({}, f, { mobs: f.mobs.concat([m("m4", 1, { rk: "b" })]) })
  : f));
const boss = A.lanes(wrap(withBoss), FIGHT, []);
assert.strictEqual(boss.lanes[0].id, "m4");
assert.strictEqual(boss.lanes[0].boss, true);

// A victim that is not a bound bot is "other", never bad; a bound-only victim uses the bind's role.
const offFrames = [fr(1000, [m("m5", null, { vo: 1 }), m("m6", 9)]), fr(1002, [m("m5", null, { vo: 1 }), m("m6", 9)])];
const off = A.lanes(wrap(offFrames), FIGHT, [{ guid: 9, name: "Off", role: "healer" }]);
assert.deepStrictEqual(segsOf(off.lanes[0]), [[0, 4000, "other", false, ""]]);
assert.deepStrictEqual(segsOf(off.lanes[1]), [[0, 4000, "healer", true, "Off"]]);

// At most 12 lanes are returned, with the count of the rest.
const manyMobs = [];
for (let i = 1; i <= 14; i++) manyMobs.push(m("m" + i, 1));
const many = A.lanes(wrap([fr(1000, manyMobs), fr(1002, manyMobs)]), FIGHT, []);
assert.strictEqual(many.lanes.length, 12);
assert.strictEqual(many.more, 2);

// A lane whose only engaged report falls on the fight's last instant has no segment: it is dropped, not sorted (its segs[0]
// used to crash the order and the whole strip with it).
const edge = A.lanes(wrap([fr(100, [m("m1", 1)]), fr(110, [m("m1", 1), m("m3", 1)])]), { start: 100, dur_ms: 10000, bots: BOTS }, []);
assert.strictEqual(edge.frames, 2);
assert.deepStrictEqual(edge.lanes.map((l) => l.id), ["m1"], "the lane with no segment is not a lane: " + JSON.stringify(edge.lanes.map((l) => l.id)));
assert.deepStrictEqual(segsOf(edge.lanes[0]), [[0, 3000, "tank", false, "Tank"]], "the first frame's own segment survives");
assert.strictEqual(edge.more, 0);

// ---- walk 2: one warder on Irelanthe, 10 s into fight 5-3 -------------------------------------------------------------
const w = A.lanes(track, byId("5-3"), []);
assert.strictEqual(w.frames, 1);
assert.strictEqual(w.more, 0);
assert.strictEqual(w.lanes.length, 1);
assert.strictEqual(w.lanes[0].label, "Onyxian Warder");
assert.deepStrictEqual(segsOf(w.lanes[0]), [[10000, 12000, "tank", false, "Irelanthe"]]);
assert.strictEqual(w.lanes[0].badMs, 0);
assert.strictEqual(w.lanes[0].onTankPct, 100);
assert.deepStrictEqual(w.lanes[0].badNames, []);

// An empty or missing track answers with nothing (no throw).
assert.deepStrictEqual(A.lanes({ frames: [], names: {} }, FIGHT, []), { lanes: [], more: 0, frames: 0, ntWorst: null, lane: A.LANE });
assert.deepStrictEqual(A.lanes(null, FIGHT, []), { lanes: [], more: 0, frames: 0, ntWorst: null, lane: A.LANE });

// ---- the non-tank lane rule: held segments, stints and the worst lane ------------------------------------------------
assert.deepStrictEqual(A.LANE, { reachYd: 30, warnMs: 8000, failMs: 12000 });
assert.strictEqual(l1.ntMs, 4000);
assert.strictEqual(l1.ntLongestMs, 4000);
assert.strictEqual(res.ntWorst, l1);
assert.strictEqual(res.lane, A.LANE);

// Attempt 2, the committed fixture: Onyxia's lane was held by non-tanks 12 s in a row inside 30 yd, 23 s in all.
const a2 = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "lanes-a2.json"), "utf8"));
const a2res = A.lanes(a2.track, a2.fight, a2.fight.bots);
assert.strictEqual(a2res.frames, 163);
const ony = a2res.lanes.find((l) => l.label === "Onyxia");
assert.ok(ony, "the Onyxia lane is in the strip");
assert.strictEqual(ony.ntMs, 23000);
assert.strictEqual(ony.ntLongestMs, 12000);
assert.deepStrictEqual(ony.badNames, ["Quelin", "Sael", "Aelia", "Venomwind", "Shadowfall", "Pozzle", "Ironcaller", "Beardster",
  "Hald", "Lorenthil", "Garic", "Frostz", "Shadowbrand", "Soulmane", "Blackwalker", "Impz", "Ravenbringer", "Brrtastic", "Basicie",
  "Kaelis", "Beastcrest"]);
assert.strictEqual(a2res.ntWorst, ony);

// Two warders on the main tank for 30 s; one 2-sample bounce to a healer 5 yd from the enemy is 4 s, under the 8 s warn.
const strayBot = (guid, x, alive) => [guid, x, 0, 100, alive, 249];
const trashBots = [strayBot(1, 0, 1), strayBot(2, 5, 1), strayBot(3, 40, 1)];
const trashFrames = [];
for (let t = 2000; t <= 2030; t += 2) {
  const bounce = t === 2010 || t === 2012;
  trashFrames.push({ t, map: 249, step: 1, fight: true, bots: trashBots, mobs: [m("w1", bounce ? 2 : 1), m("w2", 1)] });
}
const trash = A.lanes({ commander: "T", frames: trashFrames, names: { 7: "Warder" } }, { start: 2000, dur_ms: 30000, bots: BOTS }, []);
assert.strictEqual(trash.lanes.length, 2);
const w1lane = trash.lanes.find((l) => l.id === "w1");
assert.strictEqual(w1lane.ntMs, 4000);
assert.strictEqual(w1lane.ntLongestMs, 4000);
assert.strictEqual(trash.ntWorst, w1lane);

// Reach is 30 yd: a healer 31 yd away is never held; neither is a dead one.
const farFrames = [];
const deadFrames = [];
for (let t = 2000; t <= 2020; t += 2) {
  farFrames.push({ t, map: 249, step: 1, fight: true, bots: [strayBot(2, 31, 1)], mobs: [m("w1", 2)] });
  deadFrames.push({ t, map: 249, step: 1, fight: true, bots: [strayBot(2, 5, 0)], mobs: [m("w1", 2)] });
}
const far = A.lanes({ frames: farFrames, names: { 7: "Warder" } }, { start: 2000, dur_ms: 20000, bots: BOTS }, []);
assert.strictEqual(far.lanes[0].ntMs, 0);
assert.strictEqual(far.lanes[0].ntLongestMs, 0);
const dead = A.lanes({ frames: deadFrames, names: { 7: "Warder" } }, { start: 2000, dur_ms: 20000, bots: BOTS }, []);
assert.strictEqual(dead.lanes[0].ntMs, 0);
assert.strictEqual(dead.lanes[0].ntLongestMs, 0);

// Two healers within reach, alternating: their segments stay apart, the stint (any victim) is 10 s.
const twoHealFrames = [];
for (let t = 2000; t <= 2008; t += 2) {
  const victim = t === 2004 || t === 2008 ? 4 : 2;
  twoHealFrames.push({ t, map: 249, step: 1, fight: true, bots: [strayBot(2, 5, 1), strayBot(4, 6, 1)], mobs: [m("w1", victim)] });
}
const twoHealBots = BOTS.concat([{ name: "Heal2", guid: 4, role: "healer" }]);
const twoHeal = A.lanes({ frames: twoHealFrames, names: { 7: "Warder" } }, { start: 2000, dur_ms: 10000, bots: twoHealBots }, []);
assert.strictEqual(twoHeal.lanes[0].segs.length, 4, "each victim is its own segment: " + JSON.stringify(segsOf(twoHeal.lanes[0])));
assert.strictEqual(twoHeal.lanes[0].ntMs, 10000);
assert.strictEqual(twoHeal.lanes[0].ntLongestMs, 10000);

// The hold flag is part of a segment: the same healer in reach for 4 s, then walking out of it, is two segments (a merge would
// paint all 8 s as held).
const walkFrames = [];
for (let t = 2000; t <= 2006; t += 2) {
  walkFrames.push({ t, map: 249, step: 1, fight: true, bots: [strayBot(2, t <= 2002 ? 5 : 40, 1)], mobs: [m("w1", 2)] });
}
const walk = A.lanes({ frames: walkFrames, names: { 7: "Warder" } }, { start: 2000, dur_ms: 8000, bots: BOTS }, []);
assert.strictEqual(walk.lanes[0].segs.length, 2, "held and not held stay apart: " + JSON.stringify(walk.lanes[0].segs));
assert.strictEqual(walk.lanes[0].ntMs, 4000);
assert.strictEqual(walk.lanes[0].ntLongestMs, 4000);
