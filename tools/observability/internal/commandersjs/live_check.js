"use strict";
// Run by commandersjs_test.go under node: LiveView (web/static/js/live.js), the pure half of the live stream: the URL of the asked
// commanders, the copy-on-write merge of a position frame, the refresh-rate rule, the error fallback and the one-draw-per-frame
// scheduler.
const assert = require("assert");
const L = require(require("path").resolve(process.argv[2]));

// ---- url: dedupe, sort, at most 20
assert.strictEqual(L.url([]), null, "no commander, no stream");
assert.strictEqual(L.url(["b", "a", "b"]), "/api/v1/live?commander=a&commander=b");
assert.strictEqual(L.url(["a b&c"]), "/api/v1/live?commander=a%20b%26c", "values are encoded");
const many = Array.from({ length: 25 }, (_, i) => "c" + String(i).padStart(2, "0"));
const u = L.url(many);
assert.strictEqual((u.match(/commander=/g) || []).length, 20, "at most 20 commanders");
assert.ok(u.indexOf("c19") >= 0 && u.indexOf("c20") < 0, "the first 20 in order");

// ---- applyPos: copy-on-write merge by guid and id
const list = [{
  commander: "A", bot_meta: 1,
  bots: [{ guid: 1, name: "Lead", x: 1, y: 2, hp: 10, max_hp: 20, alive: true, map: 209 }],
  mobs: [{ id: "9", x: 3, y: 4, hp: 50 }],
}];
const out = L.applyPos(list, {
  commander: "A", ts_ms: 5, combat: true,
  bots: [{ g: 1, m: 530, x: 9, y: 8, z: 7, o: 1, hp: 5, mhp: 30, a: 0 }, { g: 2, x: 100, y: 100 }],
  mobs: [{ id: "9", x: 7, y: 8, hp: 40, v: "1" }, { id: "404", x: 1, y: 1 }],
});
assert.notStrictEqual(out, list, "a new list");
assert.notStrictEqual(out[0], list[0], "a new binding");
assert.notStrictEqual(out[0].bots, list[0].bots, "a new bots array");
assert.notStrictEqual(out[0].mobs, list[0].mobs, "a new mobs array");
assert.strictEqual(out[0].bot_meta, 1, "the rest of the binding is copied");
const b = out[0].bots[0];
assert.deepStrictEqual([b.x, b.y, b.z, b.hp, b.max_hp, b.alive, b.map], [9, 8, 7, 5, 30, false, 530], "bot fields");
assert.strictEqual(out[0].bots.length, 1, "an unknown guid is ignored");
const m = out[0].mobs[0];
assert.deepStrictEqual([m.x, m.y, m.hp, m.victim], [7, 8, 40, "1"], "mob fields");
assert.strictEqual(out[0].mobs.length, 1, "an unknown mob is ignored");
assert.strictEqual(list[0].bots[0].x, 1, "the input list is not changed");
assert.strictEqual(list[0].mobs[0].x, 3);
assert.strictEqual(L.applyPos(list, { commander: "Z", bots: [] }), list, "an unknown commander changes nothing");

// ---- noteReport: one glide state per commander, fed from the report; an empty map is fine
{
  const states = {};
  L.noteReport(states, [{ commander: "A", mobs_map: 209, bots: [{ guid: 1, x: 0, y: 0, map: 209, alive: true }], mobs: [{ id: "9", x: 3, y: 4 }] }], 0);
  L.noteReport(states, [{ commander: "A", mobs_map: 209, bots: [{ guid: 1, x: 10, y: 0, map: 209, alive: true }], mobs: [{ id: "9", x: 5, y: 4 }] }], 1000);
  assert.ok(states.A && states.A.units, "the binding's own state is created");
  assert.strictEqual(states.A.gap, 1000, "the report gap is sampled");
  assert.strictEqual(states.A.units["b:1"].tx, 10, "the bot's target");
  assert.strictEqual(states.A.units["m:9"].tx, 5, "the mob's target");
  L.noteReport({}, [], 0);
  L.noteReport(null, [{ commander: "A", bots: [] }], 0);
}

// ---- posAllowed: only the two live refresh rates move the dots
[0, 5, 10, 30].forEach((s) => assert.strictEqual(L.posAllowed(s), false, String(s)));
[1, 2].forEach((s) => assert.strictEqual(L.posAllowed(s), true, String(s)));

// ---- fallback: three errors within 60 s
const now = 1000000;
assert.strictEqual(L.fallback([now - 61000, now - 2000, now - 1000], now), false, "two inside the minute");
assert.strictEqual(L.fallback([now - 30000, now - 2000, now - 1000], now), true, "three inside the minute");
assert.strictEqual(L.fallback([], now), false);

// ---- scheduler: one draw per key per animation frame, glide redraws at most every 33 ms
{
  const frames = [];
  const raf = (fn) => { frames.push(fn); return frames.length; };
  let t = 0;
  const drawn = [];
  const sched = L.scheduler(raf, () => t, (k) => drawn.push(k));
  for (let i = 0; i < 5; i++) sched.mark("k");
  assert.strictEqual(frames.length, 1, "five marks request one frame");
  frames.shift()();
  assert.deepStrictEqual(drawn, ["k"], "five marks in one frame draw once");

  t = 0; sched.mark("g", true); frames.shift()();
  t = 10; sched.mark("g", true); frames.shift()();
  assert.deepStrictEqual(drawn, ["k", "g"], "a glide redraw inside 33 ms is skipped");
  t = 33; sched.mark("g", true); frames.shift()();
  assert.deepStrictEqual(drawn, ["k", "g", "g"], "the next one after 33 ms draws");
  t = 40; sched.mark("g"); frames.shift()();
  assert.deepStrictEqual(drawn, ["k", "g", "g", "g"], "a full draw is never throttled");
  assert.strictEqual(frames.length, 0);
}

console.log("live_check.js ok");
