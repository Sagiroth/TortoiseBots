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

// ---- noteReport: one glide state per commander, fed with the report's own server time; an empty map is fine
{
  const states = {};
  L.noteReport(states, [{ commander: "A", mobs_map: 209, ts_ms: 5000000, bots: [{ guid: 1, x: 0, y: 0, map: 209, alive: true }], mobs: [{ id: "9", x: 3, y: 4, o: 1 }] }], 900);
  L.noteReport(states, [{ commander: "A", mobs_map: 209, bots: [{ guid: 1, x: 10, y: 0, map: 209, alive: true }], mobs: [{ id: "9", x: 5, y: 4, o: 2 }] }], 1900);
  assert.ok(states.A && states.A.units, "the binding's own state is created");
  assert.deepStrictEqual(states.A.gaps, [1000], "the report gap is sampled");
  assert.strictEqual(states.A.offset, 900 - 5000000, "a report without ts_ms falls back to the arrival time of the last server time");
  let s = states.A.units["b:1"].samples;
  assert.strictEqual(s[s.length - 1].x, 10, "the bot's newest sample");
  s = states.A.units["m:9"].samples;
  assert.strictEqual(s[s.length - 1].x, 5, "the mob's newest sample");
  assert.strictEqual(s[s.length - 1].f, 2, "and its facing, for a boss's arrow");
  // a later report carrying its own ts_ms moves the clock sample
  L.noteReport(states, [{ commander: "A", mobs_map: 209, ts_ms: 5002000, bots: [], mobs: [] }], 2800);
  assert.strictEqual(states.A.offset, 2800 - 5002000, "the smaller arrival-minus-server wins");
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

// ---- scheduler: one draw per key per animation frame; the focused map at the screen's rate (16 ms), every other visible map
// at the grid rate (64 ms)
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

  sched.focus("g");
  t = 0; sched.mark("g", true); frames.shift()();
  t = 10; sched.mark("g", true); frames.shift()();
  assert.deepStrictEqual(drawn, ["k", "g"], "a glide redraw inside 16 ms is skipped");
  t = 16; sched.mark("g", true); frames.shift()();
  assert.deepStrictEqual(drawn, ["k", "g", "g"], "the focused map draws at the next animation frame");
  t = 40; sched.mark("g"); frames.shift()();
  assert.deepStrictEqual(drawn, ["k", "g", "g", "g"], "a full draw is never throttled");
  assert.strictEqual(frames.length, 0);

  // the same key without the focus glides at the grid rate
  sched.focus(null);
  t = 100; sched.mark("g", true); frames.shift()();
  assert.deepStrictEqual(drawn, ["k", "g", "g", "g"], "a grid map is not redrawn inside 64 ms");
  t = 104; sched.mark("g", true); frames.shift()();
  assert.deepStrictEqual(drawn, ["k", "g", "g", "g", "g"], "the grid rate draws it at 64 ms");
  t = 140; sched.mark("g", true); frames.shift()();
  assert.deepStrictEqual(drawn, ["k", "g", "g", "g", "g"], "and not again inside the next 64 ms");

  // a structural redraw of a map off screen waits until it is back in view
  sched.visible("h", false);
  assert.strictEqual(sched.shown("h"), false, "the observer's hidden map says so");
  assert.strictEqual(sched.shown("g"), true, "a map nobody marked hidden is on screen");
  t = 300; sched.mark("h"); frames.shift()();
  assert.strictEqual(drawn.indexOf("h"), -1, "a hidden map does not redraw");
  t = 400; sched.visible("h", true); frames.shift()();
  assert.strictEqual(sched.shown("h"), true, "back in view");
  assert.ok(drawn.indexOf("h") >= 0, "and it draws once");
}

// ---- S4: 20 active maps, 4 in view, 1 focused. Per simulated second the focused map redraws about 60 times, each visible
// grid map at most 15, a hidden map never, and one animation frame never carries more than 8 glide-only draws.
{
  const keys = Array.from({ length: 20 }, (_, i) => "c" + i);
  const visible = keys.slice(0, 4), focused = keys[0];
  const frames = [];
  const raf = (fn) => { frames.push(fn); return frames.length; };
  let t = 0;
  const draws = [];
  const sched = L.scheduler(raf, () => t, (k) => draws.push(k));
  sched.focus(focused);
  keys.forEach((k) => sched.visible(k, false));
  visible.forEach((k) => sched.visible(k, true));
  while (frames.length) frames.shift()(); // the flush of the maps back in view is not part of the measured second
  draws.length = 0;
  let worst = 0;
  for (let i = 0; i < 60; i++) {
    t = i * 16;
    keys.forEach((k) => sched.mark(k, true)); // every active key asks, as app.js's per-frame loop does
    while (frames.length) {
      let inFrame = 0;
      const paint = frames.shift();
      const before = draws.length;
      paint();
      inFrame = draws.length - before;
      if (inFrame > worst) worst = inFrame;
    }
  }
  const per = {};
  draws.forEach((k) => { per[k] = (per[k] || 0) + 1; });
  assert.ok(per[focused] >= 59 && per[focused] <= 61, "the focused map redraws about 60 times a second: " + per[focused]);
  visible.slice(1).forEach((k) => {
    assert.ok((per[k] || 0) >= 14 && (per[k] || 0) <= 15, "a visible grid map redraws at most 15 times a second: " + k + " " + per[k]);
  });
  keys.slice(4).forEach((k) => assert.strictEqual(per[k] || 0, 0, "a hidden map does not redraw: " + k));
  assert.ok(worst <= L.GLIDE_BUDGET, "at most 8 glide-only draws in one frame: " + worst);
}

// ---- S4: the budget is shared round robin: with all 20 maps in view no frame draws more than 8, and every map is served
{
  const keys = Array.from({ length: 20 }, (_, i) => "g" + i);
  const frames = [];
  const raf = (fn) => { frames.push(fn); return frames.length; };
  let t = 0;
  const per = {};
  let worst = 0;
  const sched = L.scheduler(raf, () => t, (k) => { per[k] = (per[k] || 0) + 1; });
  keys.forEach((k) => sched.visible(k, true));
  for (let i = 0; i < 4; i++) {
    t = i * 16;
    keys.forEach((k) => sched.mark(k, true));
    while (frames.length) {
      const before = Object.values(per).reduce((n, x) => n + x, 0);
      frames.shift()();
      const drew = Object.values(per).reduce((n, x) => n + x, 0) - before;
      if (drew > worst) worst = drew;
    }
  }
  assert.ok(worst <= L.GLIDE_BUDGET, "no frame draws more than 8 glide-only maps: " + worst);
  keys.forEach((k) => assert.ok((per[k] || 0) > 0, "the round robin reaches every map: " + k));
  assert.ok(worst >= L.GLIDE_BUDGET, "and the budget is used: " + worst);
}

console.log("live_check.js ok");
