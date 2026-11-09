"use strict";
// Run by commandersjs_test.go under node: FightMap (web/static/js/fightmap.js), the pure logic of what the Fight debugger draws on the card
// map: trails cut by the range and split at a jump, the 300-point cap, the cloud a dying Son of Hakkar leaves (radius from the seek_aura row,
// else the default), the puller's path, the mechanics from the Hakkar tactic rows, heat cells, the focus filter, the fit box, the live and
// track frames, the Dungeons pull pins, and hostile input.
const assert = require("assert");
const path = require("path");
const F = require(path.resolve(process.argv[2]));
const Filter = require(path.resolve(process.argv[3]));
const eq = (a, b, msg) => assert.deepStrictEqual(JSON.parse(JSON.stringify(a)), b, msg);
const pts = (list) => list.filter((p) => p !== null);

// ---- fixtures: frames of fight ms
const bot = (x, y, hp, alive) => ({ x, y, hp: hp === undefined ? 100 : hp, alive: alive !== false, m: 309 });
const frame = (t, bots, mobs) => ({ t, bots: bots || {}, mobs: mobs || {} });

// ---- trails: the range cuts, a 50 yd jump splits the path
const walk = [0, 1, 2, 3, 4, 5].map((i) => frame(i * 2000, { 7: bot(i < 3 ? i * 5 : 200 + i * 5, 0) }));
let tr = F.trails(walk, null);
eq(Object.keys(tr), ["7"], "one trail per bot, keyed by guid");
eq(tr[7].map((p) => (p === null ? null : p[0])), [0, 5, 10, null, 215, 220, 225], "a 190 yd step is a teleport: a null cut, not a line across the map");
tr = F.trails(walk, { from: 2000, to: 6000 });
eq(tr[7].map((p) => (p === null ? null : p[2])), [2000, 4000, null, 6000], "the range keeps the frames from 2 s to 6 s only (t is the third number)");
eq(F.trails(walk, { from: 90000, to: 99000 }), {}, "a range with no frame draws nothing");
// a step of exactly the limit is a walk, one over it a jump (the brief's 40 yd)
assert.strictEqual(F.JUMP_YD, 40);
eq(F.trails([frame(0, { 1: bot(0, 0) }), frame(500, { 1: bot(40, 0) })], null)[1].length, 2, "40 yd is still one segment");
eq(F.trails([frame(0, { 1: bot(0, 0) }), frame(500, { 1: bot(40.5, 0) })], null)[1].length, 3, "over 40 yd is a cut");
// a bot that died is not drawn while dead and its path restarts when it is alive again
const died = [frame(0, { 1: bot(0, 0) }), frame(1000, { 1: bot(1, 0, 0, false) }), frame(2000, { 1: bot(1, 0, 0, false) }), frame(3000, { 1: bot(2, 0) })];
eq(F.trails(died, null)[1].map((p) => (p === null ? null : p[2])), [0, null, 3000], "the dead stretch is a cut");

// ---- 1000 frames: at most 300 points, the first and the last kept
const many = [];
for (let i = 0; i < 1000; i++) many.push(frame(i * 500, { 5: bot(i * 0.1, 0) }));
tr = F.trails(many, null);
assert.ok(pts(tr[5]).length <= 300 && pts(tr[5]).length > 100, "capped at 300, not emptied: " + pts(tr[5]).length);
eq(tr[5][0], [0, 0, 0], "the first frame is kept");
eq(tr[5][tr[5].length - 1], [99.9, 0, 499500], "the last frame is kept");
for (const n of [299, 300, 301, 599, 600, 601, 899, 1499]) {
  const fr = [];
  for (let i = 0; i < n; i++) fr.push(frame(i, { 5: bot(i * 0.01, 0) }));
  const p = F.trails(fr, null)[5];
  assert.ok(pts(p).length <= 300, n + " frames -> " + pts(p).length);
  eq([p[0][2], p[p.length - 1][2]], [0, n - 1], n + " frames keep first and last");
}
// a cut survives the thinning
const jumpy = [];
for (let i = 0; i < 900; i++) jumpy.push(frame(i, { 5: bot((i < 450 ? 0 : 500) + i * 0.01, 0) }));
assert.strictEqual(F.trails(jumpy, null)[5].filter((p) => p === null).length, 1, "one jump, one cut after thinning");

// ---- mob trails: summons and the boss only, at most 20
const mobFrames = [0, 1, 2].map((i) => frame(i * 2000, { 1: bot(0, 0) }, { 90: { e: 11357, x: i, y: 1, hp: 100, v: 0 }, 91: { e: 4242, x: i, y: 2, hp: 100, v: 0 }, 92: { e: 14834, x: i, y: 3, hp: 100, v: 0 } }));
eq(Object.keys(F.trails(mobFrames, null, { mobs: false })), ["1"], "no mob trails unless asked");
eq(Object.keys(F.trails(mobFrames, null, { mobs: true, boss: 14834 })).sort(), ["1", "m:90:11357", "m:92:14834"], "the Son and the boss, not a plain mob");
const crowd = [frame(0, {}, {})];
for (let i = 0; i < 30; i++) crowd[0].mobs[1000 + i] = { e: 11357, x: i, y: 0, hp: 100, v: 0 };
eq(Object.keys(F.trails(crowd, null, { mobs: true })).length, 20, "at most 20 mob trails");

// ---- clouds: a Son seen in frames 1-3 and gone in 4 leaves one cloud at frame 3's place
const son = (x, hp) => ({ e: 11357, x, y: 7, hp: hp === undefined ? 100 : hp, v: 0 });
const sonFrames = [frame(1000, {}, { 50: son(10) }), frame(3000, {}, { 50: son(11) }), frame(5000, {}, { 50: son(12) }), frame(7000, {}, {})];
const seek = [{ map: 309, boss: 14834, kind: "seek_aura", target: 11357, spell: 24321, param: 6, spotX: -11787.4, spotY: -1664.3 }];
let cl = F.clouds(sonFrames, null, seek);
eq(cl, [{ x: 12, y: 7, t: 5000, r: 6, label: "Son cloud" }], "one cloud at frame 3's place, the radius of the seek_aura row");
eq(F.clouds(sonFrames, null, [])[0].r, 4, "no row: the default radius 4");
eq(F.clouds(sonFrames, null, [{ kind: "seek_aura", target: 11357, param: 4 }])[0].r, 4, "a row of 4.0 gives 4");
eq(F.clouds(sonFrames, null, [{ kind: "seek_aura", target: 11357, param: 0 }])[0].r, 4, "a row with no radius gives the default");
eq(F.clouds(sonFrames, { from: 0, to: 4000 }, seek), [], "a cloud whose time is outside the range is not listed");
eq(F.clouds(sonFrames.slice(0, 3), null, seek), [], "a Son still in the last frame has not died");
eq(F.clouds([frame(1000, {}, { 50: son(10) }), frame(3000, {}, { 50: son(11, 1) }), frame(5000, {}, { 50: son(11, 0) })], null, seek).length, 1, "at 1 % hp or less it counts once, not again when it vanishes");
eq(F.clouds([frame(1000, {}, { 50: { e: 14834, x: 1, y: 1, hp: 100, v: 0 } }), frame(3000, {}, {})], null, seek), [], "only a CLOUD_SOURCES entry leaves a cloud");
eq(F.cloudsAt(cl, 5000).length, 1, "on the map at its own time");
eq(F.cloudsAt(cl, 24999).length, 1, "and for 20 s");
eq(F.cloudsAt(cl, 25001).length, 0, "then gone");
eq(F.cloudsAt(cl, 4999).length, 0, "not before it");
eq(F.cloudsAt(cl, null).length, 1, "no shown time: all of them");

// ---- puller: the bot a Son targeted first, and its path over the frames in which a Son targets it
const pf = [
  frame(0, { 1: bot(0, 0), 2: bot(5, 5) }, { 50: { e: 11357, x: 9, y: 9, hp: 100, v: 0 } }),
  frame(2000, { 1: bot(1, 0), 2: bot(6, 5) }, { 50: { e: 11357, x: 9, y: 9, hp: 100, v: 2 } }),
  frame(4000, { 1: bot(2, 0), 2: bot(7, 5) }, { 50: { e: 11357, x: 9, y: 9, hp: 100, v: 2 }, 51: { e: 11357, x: 9, y: 9, hp: 100, v: 1 } }),
  frame(6000, { 1: bot(3, 0), 2: bot(8, 5) }, { 50: { e: 11357, x: 9, y: 9, hp: 100, v: 1 } }),
];
eq(F.pullerPath(pf, null), { g: "2", pts: [[6, 5, 2000], [7, 5, 4000]] }, "bot 2 was targeted first; its path covers the frames in which a Son targets it");
eq(F.pullerPath(pf, { from: 5000, to: 9000 }).g, "1", "inside the range only: the first target there");
eq(F.pullerPath([frame(0, { 1: bot(0, 0) }, { 50: { e: 4242, x: 1, y: 1, hp: 100, v: 1 } })], null), null, "a target of something that is no Son does not count");
eq(F.pullerPath([frame(0, { 1: bot(0, 0) }, { 50: { e: 11357, x: 1, y: 1, hp: 100, v: 99 } })], null), null, "a target that is no bot of the frame does not count");

// ---- mechanics: the two Hakkar rows
const hakkar = [
  { map: 309, boss: 14834, kind: "anchor", target: 0, spell: 0, priority: 25, param: 30, spotX: -11787.4, spotY: -1649.34, spotZ: 54.1 },
  { map: 309, boss: 14834, kind: "seek_aura", target: 11357, spell: 24321, priority: 30, param: 4, spotX: -11787.4, spotY: -1664.3, spotZ: 54 },
  { map: 309, boss: 14834, kind: "interrupt", target: 0, spell: 1, priority: 0, param: 0, spotX: 5, spotY: 5 },
  { map: 309, boss: 11380, kind: "anchor", target: 0, spell: 0, priority: 20, param: 30, spotX: 1, spotY: 1 },
  { map: 249, boss: 14834, kind: "anchor", target: 0, spell: 0, priority: 20, param: 30, spotX: 1, spotY: 1 },
];
eq(F.mechanics(hakkar, 309, 14834), [
  { kind: "anchor", x: -11787.4, y: -1649.34, r: 30, rTank: 25, label: "Anchor 30 yd · tanks 25 yd" },
  { kind: "stand", x: -11787.4, y: -1664.3, label: "Stand B" },
], "the anchor circle (30, tanks 25) and the stand at the seek spot; another boss, map or kind gives nothing");
eq(F.mechanics([{ map: 309, boss: 14834, kind: "anchor", priority: 25, param: 30, spotX: 0, spotY: 0 }, { map: 309, boss: 14834, kind: "seek_aura", spotX: 0, spotY: 0 }], 309, 14834), [], "a row with spot 0 draws nothing");
eq(F.mechanics([{ map: 309, boss: 14834, kind: "anchor", priority: 40, param: 30, spotX: 1, spotY: 2 }], 309, 14834)[0].rTank, 0, "a tank radius that is not inside the circle is left out");
eq(F.mechanics([{ map: 309, boss: 14834, kind: "anchor", priority: 25, param: 0, spotX: 1, spotY: 2 }], 309, 14834), [], "an anchor with no radius draws nothing");
eq(F.mechanics("x", 309, 14834), [], "hostile tactics");
eq(F.mechanics([null, 5, "x", { map: 309, boss: 14834, kind: "anchor", param: "30", spotX: "1", spotY: 1 }], 309, 14834), [], "rows of the wrong types are skipped");

// ---- heat: a 40 % drop and a death land in their cells; 500 cells keep 400
const hf = [frame(0, { 1: bot(1, 1, 100), 2: bot(50, 50, 100) }), frame(2000, { 1: bot(1, 1, 60), 2: bot(50, 50, 100) }), frame(4000, { 1: bot(1, 1, 60), 2: bot(50, 50, 100) })];
let h = F.heat(hf, { deaths: [{ name: "A", t_ms: 3000, x: 50, y: 50, killer: "X" }] }, null);
eq(h.cell, 4);
eq(h.cells, [[12, 12, 1], [0, 0, 0.4]], "the death (100) in its cell 12,12, the 40 % drop in cell 0,0, scaled to the strongest");
h = F.heat(hf, { deaths: [] }, null);
eq(h.cells, [[0, 0, 1]], "only the drop");
h = F.heat(hf, { deaths: [] }, { from: 3000, to: 9000 });
eq(h.cells, [], "the drop is before the range");
h = F.heat([frame(0, { 1: bot(1, 1, 100) }), frame(2000, { 1: bot(1, 1, 60, false) })], { deaths: [] }, null);
eq(h.cells, [], "a bot that died in the next frame is the death's, not a drop");
const wide = [], wf = [];
for (let i = 0; i < 500; i++) wide.push({ name: "B" + i, t_ms: 1000, x: 4 + i * 4, y: 0, killer: "k" });
h = F.heat(wf, { deaths: wide }, null);
assert.ok(h.cells.length <= 200, "deaths are capped at 200, so 500 of them make at most 200 cells");
const dmg = [];
for (let i = 0; i < 500; i++) dmg.push(frame(i * 1000, { 1: bot(i * 4, 0, 100) }), frame(i * 1000 + 500, { 1: bot(i * 4, 0, 100 - (i % 50) - 1) }));
h = F.heat(dmg, null, null);
assert.strictEqual(h.cells.length, 400, "500 damaged cells -> 400");
assert.ok(h.cells.every((c) => c[2] > 0 && c[2] <= 1) && h.cells[0][2] === 1, "weights are 0-1, the strongest is 1");
assert.ok(h.cells.every((c, i) => i === 0 || h.cells[i - 1][2] >= c[2]), "sorted by weight");

// ---- death marks
const fightD = { bots: [{ name: "Tank", role: "tank" }], deaths: [
  { name: "Tank", t_ms: 5000, x: 10, y: 20, killer: "Hakkar" },
  { name: "Ghost", t_ms: 6000, x: 0, y: 0, killer: "X" },
  { name: "Late", t_ms: 99000, x: 1, y: 1, killer: "" },
  { name: "Bad", t_ms: "x", x: 1, y: 1 },
  { name: "NoPlace", t_ms: 1, x: NaN, y: 1 },
] };
eq(F.deathMarks(fightD, null).map((d) => [d.i, d.name, d.killer, d.role]), [[0, "Tank", "Hakkar", "tank"], [2, "Late", "unknown", ""]], "a death with no place or no time is skipped; no killer reads unknown");
eq(F.deathMarks(fightD, { from: 0, to: 10000 }).map((d) => d.name), ["Tank"], "inside the range");
assert.strictEqual(F.deathTitle(F.deathMarks(fightD, null)[0]), "Tank · killed by Hakkar · 0:05");
assert.strictEqual(F.clock(125000), "2:05");
eq(F.deathMarks(null, null), []);
eq(F.deathMarks({ deaths: "x" }, null), []);

// ---- focus group goes through FdFilter
const g = F.focusGroup("Sagiroth");
assert.ok(Filter.validate(g).ok, "the focus group is a valid filter");
assert.strictEqual(Filter.describe(g), "source is Sagiroth or target is Sagiroth");
eq(g.op, "or");
assert.strictEqual(F.focusGroup(""), null);
assert.strictEqual(F.focusGroup("x".repeat(81)), null, "a name FdFilter would refuse");
assert.strictEqual(F.focusGroup(5), null);
assert.ok(Filter.match(g, { src: "Sagiroth", tgt: "Hakkar" }) && Filter.match(g, { src: "Hakkar", tgt: "Sagiroth" }) && !Filter.match(g, { src: "A", tgt: "B" }));

// ---- unitAt
eq(F.unitAt(frame(0, { 7: bot(1, 2) }, { 8: { e: 5, x: 3, y: 4, hp: 50, v: 0 } }), 7).kind, "bot");
eq(F.unitAt(frame(0, { 7: bot(1, 2) }, { 8: { e: 5, x: 3, y: 4, hp: 50, v: 0 } }), "8").kind, "mob");
assert.strictEqual(F.unitAt(frame(0), 9), null);
assert.strictEqual(F.unitAt(null, 9), null);

// ---- bounds
eq(F.bounds([frame(0, { 1: bot(0, 0), 2: bot(10, -5) })], { deaths: [{ x: 20, y: 3 }], mechanics: [{ kind: "anchor", x: 0, y: 0, r: 30 }] }), { x0: -30, y0: -30, x1: 30, y1: 30 }, "frames, deaths and the anchor circle");
eq(F.bounds([frame(0, { 1: bot(0, 0), 2: bot(10, -5) })], null), { x0: 0, y0: -5, x1: 10, y1: 0 });
assert.strictEqual(F.bounds([], null), null);
assert.strictEqual(F.bounds("x", { deaths: "y" }), null);
// the fit holds the fight's own cluster: a bot on another map, a bot at 1e6 yd, a bot 40000 yd away, a death or an anchor far off are no part of it
const cluster = [frame(0, { 1: bot(0, 0), 2: bot(30, 20) }), frame(1000, { 1: bot(10, 5), 2: bot(40, 10) })];
const want = { x0: 0, y0: 0, x1: 40, y1: 20 };
eq(F.bounds(cluster, null, 309), want, "the cluster alone");
const other = frame(2000, { 9: { x: 5000, y: 5000, hp: 100, alive: true, m: 249 } });
eq(F.bounds(cluster.concat([other]), null, 309), want, "a bot on another map does not move the fit");
eq(F.bounds(cluster.concat([frame(3000, { 9: bot(1e6, 1e6) })]), null, 309), want, "a bot at 1e6 yd (no place in the world) is dropped");
eq(F.bounds(cluster.concat([frame(3000, { 9: bot(40000, -40000) })]), null, 309), want, "a bot 40000 yd away is an outlier");
eq(F.bounds(cluster, { deaths: [{ x: 9000, y: 9000 }, { x: 1e9, y: 1 }], mechanics: [{ kind: "stand", x: -7000, y: 0 }] }, 309), want, "a far death and a far stand are outliers too");
eq(F.bounds(cluster, { mechanics: [{ kind: "anchor", x: 20, y: 10, r: 1e9 }] }, 309), { x0: -80, y0: -90, x1: 120, y1: 110 }, "an anchor radius of 1e9 is clamped to 100 yd");
eq(F.bounds(cluster, { mechanics: [{ kind: "anchor", x: 20, y: 10, r: 30 }] }, 309), { x0: -10, y0: -20, x1: 50, y1: 40 }, "an honest anchor near the fight counts with its radius");
assert.strictEqual(F.bounds([frame(0, { 9: { x: 1, y: 1, hp: 1, alive: true, m: 249 } })], null, 309), null, "only another map: nothing to fit");
assert.strictEqual(F.model({ frames: cluster.concat([other]), fight: { deaths: [] }, mapId: 309, fit: true }, {}).bounds.x1, 40, "the model passes the card's map");
eq(F.mechanics([{ map: 309, boss: 1, kind: "anchor", param: 1e9, priority: 1e8, spotX: 1, spotY: 1 }], 309, 1)[0].r, 100, "a radius of 1e9 is clamped by mechanics()");
eq(F.mechanics([{ map: 309, boss: 1, kind: "anchor", param: 1e9, priority: 1e8, spotX: 1, spotY: 1 }], 309, 1)[0].rTank, 0, "and a tank radius beyond it is left out");
eq(F.clouds(sonFrames, null, [{ kind: "seek_aura", target: 11357, param: 1e9 }])[0].r, 100, "a cloud radius of 1e9 is clamped");

// ---- trails: another map's positions are not drawn; a mob at 0 hp starts a new segment
{
  const fr = [frame(0, { 1: bot(0, 0) }), frame(1000, { 1: { x: 5, y: 0, hp: 100, alive: true, m: 249 } }), frame(2000, { 1: bot(2, 0) })];
  eq(F.trails(fr, null, { mapId: 309 })[1].map((p) => (p === null ? null : p[2])), [0, 2000], "the position on map 249 is skipped on a fight of map 309");
  eq(F.trails(fr, null)[1].filter((p) => p !== null).length, 3, "with no map given every position counts");
  const mf = [0, 1, 2, 3, 4].map((i) => frame(i * 1000, {}, { 7: { e: 11357, x: i * 3, y: 0, hp: i === 2 ? 0 : 100, v: 0 } }));
  eq(F.trails(mf, null, { mobs: true })["m:7:11357"].map((p) => (p === null ? null : p[2])), [0, 1000, null, 3000, 4000], "a mob at hp 0 cuts the line");
}

// ---- an unknown hp is unknown: no cloud, no cut, no heat drop
{
  const unk = (x) => ({ e: 11357, x, y: 7, hp: null, v: 0 });
  const fr = [frame(1000, {}, { 50: unk(1) }), frame(3000, {}, { 50: unk(2) })];
  eq(F.clouds(fr, null, []), [], "a Son whose health the reports never carried is not dying");
  eq(F.clouds([frame(1000, {}, { 50: unk(1) }), frame(3000, {}, {})], null, []).length, 1, "but one that vanishes still leaves its cloud");
  const hf2 = [frame(0, { 1: { x: 1, y: 1, hp: null, alive: true, m: 309 } }), frame(2000, { 1: bot(1, 1, 10) })];
  eq(F.heat(hf2, { deaths: [] }, null).cells, [], "a bot with no known health lost none");
  eq(F.trails([frame(0, { 1: { x: 0, y: 0, hp: null, alive: true, m: 309 } }), frame(1000, { 1: { x: 1, y: 0, hp: null, alive: true, m: 309 } })], null)[1].length, 2, "and its trail has no cut");
}

// ---- a Son that vanishes in the last 4 s of a wipe is the reset, not a death
{
  const fr = [frame(50000, {}, { 50: son(1) }), frame(52000, {}, { 50: son(2) }), frame(54000, {}, {}), frame(56000, {}, {})];
  eq(F.clouds(fr, null, [], { endMs: 56000, wipe: true }), [], "it vanished at 52 s of a 56 s wipe: no cloud");
  eq(F.clouds(fr, null, [], { endMs: 56000, wipe: false }).length, 1, "the same on a kill leaves one");
  eq(F.clouds(fr, null, [], { endMs: 70000, wipe: true }).length, 1, "and on a wipe that went on longer");
  eq(F.clouds([frame(50000, {}, { 50: son(1) }), frame(52000, {}, { 50: son(2, 0) }), frame(54000, {}, {})], null, [], { endMs: 56000, wipe: true }).length, 1, "a Son at 0 hp did die, wipe or not");
  eq(F.model({ frames: fr, fight: { dur_ms: 56000, outcome: "wipe", deaths: [] }, mapId: 309, tactics: [] }, { marks: true }).clouds, [], "the model hands the fight's outcome on");
}

// ---- caps and hp of the sources
{
  const many = {};
  for (let i = 1; i <= 120; i++) many[i] = [i, i, 1, 100, 1, 309];
  eq(Object.keys(F.fromTrack({ frames: [{ t: 1000, bots: Object.fromEntries(Object.entries(many).map(([k, r]) => [k, { x: r[1], y: r[2], hp: 100, alive: true, map: 309 }])), mobs: {} }] }, { start: 1000, dur_ms: 5000 })[0].bots).length, 80, "at most 80 bots per frame (track)");
  eq(Object.keys(F.fromLive({ startMs: 1, pos: [{ t: 10, bots: Object.values(many), mobs: [] }] })[0].bots).length, 80, "at most 80 bots per frame (live)");
  eq(Object.keys(F.fromTrack({ frames: [{ t: 1000, bots: { 1: { x: 1e9, y: 1, hp: 1, alive: true, map: 1 }, 2: { x: 5, y: 5, hp: 1, alive: true, map: 1 } }, mobs: { 9: { e: 1, x: 1, y: 7e6 } } }] }, { start: 1000, dur_ms: 5000 })[0].bots), ["2"], "a coordinate beyond the world is dropped (track)");
  eq(Object.keys(F.fromLive({ startMs: 1, pos: [{ t: 10, bots: [[1, 1e9, 1, 1, 1], [2, 5, 5, 1, 1]], mobs: [["9", 1, 7e6, 1]] }] })[0].bots), ["2"], "and in a live frame");
  const t = F.fromTrack({ frames: [{ t: 1000, bots: { 1: { x: 1, y: 1, hp: 0, hk: false, alive: true, map: 1 }, 2: { x: 1, y: 1, hp: 0, hk: true, alive: true, map: 1 } }, mobs: { 9: { e: 1, x: 1, y: 1, hp: 0, hk: false } } }] }, { start: 1000, dur_ms: 5000 })[0];
  eq([t.bots[1].hp, t.bots[2].hp, t.mobs[9].hp], [null, 0, null], "a health the report did not carry (hk false) is null, a real 0 stays 0");
  eq(F.fromLive({ startMs: 1, pos: [{ t: 10, bots: [[1, 1, 1, null, 1]], mobs: [["9", 1, 1, null]] }] })[0].bots[1].hp, null, "a live frame's unknown health stays null");
}

// ---- fromTrack: FightReplay's frames (t in unix seconds) become fight ms, only the frames around the fight
const track = { frames: [
  { t: 90, bots: { 7: { x: 1, y: 2, hp: 100, alive: true, map: 309 } }, mobs: {} },
  { t: 998, bots: { 7: { x: 1, y: 2, hp: 100, alive: true, map: 309 } }, mobs: { 50: { e: 11357, x: 3, y: 4, hp: 80, v: 7 } } },
  { t: 1010, bots: { 7: { x: 5, y: 6, hp: 90, alive: false, map: 309 }, 8: { x: "bad", y: 1 } }, mobs: { 51: { e: "x", x: 1, y: 1 } } },
  { t: 5000, bots: {}, mobs: {} },
  null, { t: "x" },
] };
const tf = F.fromTrack(track, { start: 1000, dur_ms: 20000 });
eq(tf.map((f) => f.t), [-2000, 10000], "frames from 4 s before the start to 4 s after the end, in fight ms");
eq(tf[0].mobs[50], { e: 11357, x: 3, y: 4, hp: 80, v: 7 });
eq(tf[1].bots, { 7: { x: 5, y: 6, hp: 90, alive: false, m: 309 } }, "a bot without a place and a mob without an entry are dropped");
eq(F.fromTrack(track, {}), [], "no start, no frames");
eq(F.fromTrack(null, { start: 1 }), []);

// ---- fromLive: FdLive's position frames, the mobs' entry and victim from the samples
const st = { startMs: 100000, pos: [
  { t: 100500, bots: [[7, 1, 2, 100, 1], [8, 0, 0, 100, 1], [9, 3, 4, 50, 0]], mobs: [["50", 5, 6, 90]] },
  { t: 103000, bots: [[7, 2, 2, 90, 1]], mobs: [["50", 6, 6, 80], ["51", 0, 0, 80]] },
  { t: "x" }, null,
], samples: [{ t: 100000, mobs: [{ id: "50", e: 11357, hp: 90, v: "7" }] }, { t: 102000, mobs: [{ id: "50", e: 11357, hp: 85, v: "9" }] }] };
const lf = F.fromLive(st);
eq(lf.map((f) => f.t), [500, 3000], "fight ms from the stream's clock");
eq(Object.keys(lf[0].bots), ["7", "9"], "a bot at 0, 0 has no place");
eq(lf[0].bots[9].alive, false);
eq(lf[0].mobs[50], { e: 11357, x: 5, y: 6, hp: 90, v: 7 }, "entry and victim from the newest sample at or before the frame");
eq(lf[1].mobs[50].v, 9, "the next sample's victim");
eq(Object.keys(lf[1].mobs), ["50"]);
eq(F.fromLive({ startMs: 0, pos: [{ t: 1, bots: [[1, 1, 1, 1, 1]] }] }), [], "no start, no frames");
eq(F.fromLive(null), []);
const longPos = { startMs: 1, pos: [] };
for (let i = 0; i < 2000; i++) longPos.pos.push({ t: 10 + i, bots: [[1, 1, 1, 1, 1]], mobs: [] });
assert.ok(F.fromLive(longPos).length <= 1500, "the frame count is capped");

// ---- the model computes only what is on
const input = { frames: pf, fight: { deaths: [] }, range: null, tactics: hakkar, mapId: 309, boss: 14834, fit: true };
let m = F.model(input, { trails: true, marks: true, heat: true, summons: true });
assert.ok(m.trails[1] && m.trails["m:50:11357"], "bot and Son trails");
assert.strictEqual(m.mechanics.length, 2);
assert.ok(m.puller && m.heat && m.bounds);
m = F.model(input, { trails: false, marks: false, heat: false, summons: false });
eq([Object.keys(m.trails), m.mechanics, m.clouds, m.puller, m.heat], [[], [], [], null, null], "everything off draws nothing");
assert.ok(m.bounds, "the fit box is still worked out");
m = F.model(Object.assign({}, input, { fit: false }), { trails: true });
eq([Object.keys(m.trails), m.bounds, m.heat], [["1", "2"], null, null], "bot trails only, no fit asked");
eq(F.model(null, null).trails, {});

// ---- pull pins: at the lead's first frame at or after the fight's start
const track2 = [{ t: 100, bots: { 7: { x: 1, y: 1 } } }, { t: 102, bots: { 7: { x: 2, y: 2 } } }, { t: 104, bots: { 7: { x: 3, y: 3 } } }, { t: 200, bots: { 7: { x: 9, y: 9 } } }, { t: 202, bots: { 8: { x: 5, y: 5 } } }];
const fights = [
  { key: "a", start: 101, boss_name: "Hakkar", outcome: "wipe", live: false },
  { key: "b", start: 200, boss_name: "", outcome: "kill" },
  { key: "c", start: 50, boss_name: "Old" },
  { key: "d", start: 201, outcome: "kill" },
  { key: "e", start: 103.5, boss_name: "Live", live: true },
  { start: 100 }, null,
];
eq(F.pullPins(fights, track2, 7), [
  { key: "a", x: 2, y: 2, label: "Hakkar", wipe: true, live: false },
  { key: "b", x: 9, y: 9, label: "trash", wipe: false, live: false },
  { key: "e", x: 3, y: 3, label: "Live", wipe: false, live: true },
], "a pin at the lead's first frame at or after the start; none for a fight before the track or with the lead missing; trash and wipe named");
eq(F.pullPins(fights, track2, null), []);
eq(F.pullPins("x", "y", 7), []);
const lots = [];
for (let i = 0; i < 200; i++) lots.push({ key: "k" + i, start: 100 });
assert.ok(F.pullPins(lots, track2, 7).length <= F.MAX_PINS, "pins are capped");

// ---- hostile input never throws
[null, undefined, 5, "x", [], {}, [null, 5, "x", { t: "a" }, { t: 1, bots: 5, mobs: "x" }, { t: 2, bots: { 1: null, 2: { x: "a" } }, mobs: { 1: 5 } }]].forEach((bad) => {
  F.trails(bad, null); F.trails(bad, { from: "a" }, { mobs: true }); F.clouds(bad, null, bad); F.pullerPath(bad, null); F.heat(bad, bad, null);
  F.deathMarks(bad, null); F.mechanics(bad, 1, 2); F.bounds(bad, bad); F.model({ frames: bad, fight: bad, tactics: bad, range: bad, fit: true }, { trails: true, marks: true, heat: true, summons: true });
  F.pullPins(bad, bad, bad); F.fromTrack(bad, bad); F.fromLive(bad); F.unitAt(bad, bad); F.cloudsAt(bad, bad); F.focusGroup(bad);
});
assert.deepStrictEqual(F.SUMMONS, { 11357: "Son of Hakkar", 14986: "Shade of Jin'do" });
assert.deepStrictEqual(F.CLOUD_SOURCES, { 11357: { sid: 24321, defaultR: 4 } });
console.log("fightmap_check ok");
