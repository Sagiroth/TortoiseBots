"use strict";
// Run by commandersjs_test.go under node: FdView (web/static/js/fdview.js), the pure arithmetic and state of the Fight debugger's
// time range: the pixel-to-time grid, the clamp (swap, hold inside the fight, null under 1 s), membership and overlap, the label,
// the band style, and the view's transitions (set, clear, follow, hand-over, re-clamp).
const assert = require("assert");
const path = require("path");
const FdView = require(path.resolve(process.argv[2]));
const eq = (a, b, msg) => assert.deepStrictEqual(JSON.parse(JSON.stringify(a)), b, msg);

assert.strictEqual(FdView.MIN_RANGE_MS, 1000);
assert.strictEqual(FdView.STEP_MS, 100);

// clampRange: swapped ends, held inside the fight, null for a click-sized or malformed range
eq(FdView.clampRange(5000, 2000, 60000), { from: 2000, to: 5000 }, "the ends swap");
eq(FdView.clampRange(-50, 70000, 60000), { from: 0, to: 60000 }, "held inside the fight");
assert.strictEqual(FdView.clampRange(1000, 1900, 60000), null, "under 1 s is a click");
eq(FdView.clampRange(1000, 2000, 60000), { from: 1000, to: 2000 }, "exactly 1 s is a range");
assert.strictEqual(FdView.clampRange(NaN, 5000, 60000), null, "NaN");
assert.strictEqual(FdView.clampRange(0, Infinity, 60000), null, "not finite");
assert.strictEqual(FdView.clampRange("a", 5000, 60000), null, "not a number");
assert.strictEqual(FdView.clampRange(0, 5000, 0), null, "a fight with no length");
assert.strictEqual(FdView.clampRange(0, 5000, -1), null, "a negative length");
assert.strictEqual(FdView.clampRange(70000, 80000, 60000), null, "both ends past the end collapse to nothing");

// pxToMs: the 100 ms grid, held inside the bar
assert.strictEqual(FdView.pxToMs(50, 200, 60000), 15000);
assert.strictEqual(FdView.pxToMs(-5, 200, 60000), 0);
assert.strictEqual(FdView.pxToMs(500, 200, 60000), 60000);
assert.strictEqual(FdView.pxToMs(1, 1000, 60000), 100, "60 ms rounds to the grid");
assert.strictEqual(FdView.pxToMs(10, 0, 60000), 0, "a bar with no width");
assert.strictEqual(FdView.pxToMs(10, 100, 0), 0, "a fight with no length");

// inRange / overlapMs
assert.strictEqual(FdView.inRange(5, null), true, "no range keeps everything");
assert.strictEqual(FdView.inRange(5000, { from: 5000, to: 6000 }), true, "the ends are inside");
assert.strictEqual(FdView.inRange(6001, { from: 5000, to: 6000 }), false);
assert.strictEqual(FdView.overlapMs(0, 10000, { from: 5000, to: 20000 }), 5000);
assert.strictEqual(FdView.overlapMs(0, 10000, null), 10000, "no range: all of it");
assert.strictEqual(FdView.overlapMs(0, 1000, { from: 5000, to: 6000 }), 0);

// label / bandStyle
assert.strictEqual(FdView.label({ from: 72000, to: 125000 }), "1:12–2:05 (53 s)");
assert.strictEqual(FdView.label({ from: 1000, to: 4500 }), "0:01–0:04 (3.5 s)", "tenths under 10 s");
assert.strictEqual(FdView.label(null), "");
assert.strictEqual(FdView.bandStyle({ from: 30000, to: 60000 }, 120000), "left:25.00%;width:25.00%");
assert.strictEqual(FdView.bandStyle({ from: 30000, to: 90000 }, 90000), "left:33.33%;width:66.67%");
assert.strictEqual(FdView.bandStyle({ from: 0, to: 200000 }, 100000), "left:0.00%;width:100.00%", "a band never leaves the bar");

// the view's transitions
let v = FdView.create("k");
eq(v, { key: "k", range: null, follow: true, reclamp: false });
assert.strictEqual(FdView.effective(v, false, null), null, "post-fight, no range: the whole fight");
eq(FdView.effective(v, true, { from: 1, to: 2 }), { from: 1, to: 2 }, "live and following: the follow window");
assert.strictEqual(FdView.effective(v, true, null), null, "live, following, Whole fight");
v = FdView.setRange(v, 5000, 2000, 60000, false);
eq(v.range, { from: 2000, to: 5000 });
assert.strictEqual(v.follow, true, "a post-fight drag does not touch follow");
eq(FdView.effective(v, false, null), { from: 2000, to: 5000 });
const before = v;
assert.strictEqual(FdView.setRange(v, 1000, 1500, 60000, false), before, "a click-sized drag changes nothing");
v = FdView.setRange(FdView.create("L"), 30000, 60000, 90000, true);
assert.strictEqual(v.follow, false, "a live drag stops following");
eq(FdView.effective(v, true, { from: 1, to: 2 }), { from: 30000, to: 60000 }, "live, fixed: the fixed range, not the window");
v = FdView.clear(v);
eq(v, { key: "L", range: null, follow: true, reclamp: false }, "Clear / Follow live: no range, following");

// the hand-over: a following view becomes the whole fight, a fixed one keeps its range and is fitted on the first render
const fixed = FdView.setRange(FdView.create("live:K"), 30000, 200000, 400000, true);
let h = FdView.handover(fixed, "filed");
eq(h, { key: "filed", range: { from: 30000, to: 200000 }, follow: false, reclamp: true });
h = FdView.reclamp(h, 100000);
eq(h, { key: "filed", range: { from: 0, to: 100000 }, follow: false, reclamp: false }, "a range wider than the filed fight: its width cannot be kept, so it is held to the whole fight");
assert.strictEqual(FdView.reclamp(h, 50000), h, "only once");
eq(FdView.reclamp(FdView.handover(fixed, "f"), 500), { key: "f", range: null, follow: false, reclamp: false }, "a filed fight shorter than 1 s cannot hold a range: dropped");
// the live clock can lead the filed length: a range near the end keeps its width (shifted left), it is not clipped end by end
const near = (a, b, dur) => FdView.reclamp(FdView.handover(FdView.setRange(FdView.create("live:K"), a, b, 100000, true), "f"), dur).range;
eq(near(88000, 92000, 87000), { from: 83000, to: 87000 }, "the whole range past the filed end: shifted left, 4 s kept");
eq(near(80000, 88000, 87000), { from: 79000, to: 87000 }, "only its end past the filed end: shifted left, 8 s kept");
eq(near(86000, 87000, 86500), { from: 85500, to: 86500 }, "a 1 s range stays 1 s");
eq(near(30000, 40000, 87000), { from: 30000, to: 40000 }, "a range inside the filed fight is unchanged");
const waiting = FdView.handover(fixed, "f");
assert.strictEqual(FdView.reclamp(waiting, 0), waiting, "no fight length yet: wait for the first render that has one");
eq(FdView.handover(FdView.create("live:K"), "filed"), { key: "filed", range: null, follow: true, reclamp: false }, "a following view becomes the whole fight");
eq(FdView.handover({ key: "live:K", range: { from: 0, to: 5000 }, follow: true, reclamp: false }, "filed"), { key: "filed", range: null, follow: true, reclamp: false }, "a following view drops a stale range: the follow window is no fixed range");
eq(FdView.handover(null, "filed"), { key: "filed", range: null, follow: true, reclamp: false }, "no view at all");
console.log("fdview_check.js ok");
