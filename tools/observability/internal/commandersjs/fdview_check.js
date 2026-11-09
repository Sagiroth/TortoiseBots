"use strict";
// Run by commandersjs_test.go under node: FdView (web/static/js/fdview.js), the pure arithmetic and state of the Fight debugger's
// time range: the pixel-to-time grid, the clamp (swap, hold inside the fight, null under 1 s), membership and overlap, the label,
// the band style, and the view's transitions (set, clear, follow, hand-over, re-clamp).
const assert = require("assert");
const path = require("path");
const FdView = require(path.resolve(process.argv[2]));
const FdFilter = require(path.join(path.dirname(path.resolve(process.argv[2])), "fdfilter.js"));
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

// ---- the share link: toQuery / fromQuery / stripFd ------------------------------------------------------------------------------------
assert.deepStrictEqual(FdView.KEYS, ["r", "f", "au", "mk", "n", "rp", "sp", "lw"], "the fixed output order");
const FILTER = FdFilter.addAt(FdFilter.empty(), "", { f: "ability", op: "is", v: "Blood Siphon", not: false });
const CODE = FdFilter.encode(FILTER);
const FULL = { range: { from: 72000, to: 125000 }, filter: FILTER, au: 24321, marker: { sid: 24324 }, night: "2026-11-06", rp: 95000, sp: 2 };
const FULLQ = "r=72000-125000&f=" + CODE + "&au=24321&mk=s24324&n=2026-11-06&rp=95000&sp=2";
assert.strictEqual(FdView.toQuery(FULL), FULLQ, "a full view in KEYS order");
let back = FdView.fromQuery(new URLSearchParams(FULLQ));
eq(back.range, { from: 72000, to: 125000 });
eq(back.filter, FILTER, "the filter decodes to the same tree");
assert.deepStrictEqual([back.au, back.night, back.rp, back.sp, back.lw], [24321, "2026-11-06", 95000, 2, null]);
eq(back.marker, { sid: 24324 });
eq(back.bad, [], "nothing dropped");
assert.strictEqual(FdView.toQuery(back), FULLQ, "the round trip gives the same link");
assert.strictEqual(FdView.fromQuery(FULLQ.split("&").reduce((o, kv) => { const i = kv.indexOf("="); o[kv.slice(0, i)] = kv.slice(i + 1); return o; }, {})).rp, 95000, "a plain object reads too");
assert.strictEqual(FdView.toQuery({}), "", "an empty view writes nothing");
assert.strictEqual(FdView.toQuery(null), "");
assert.strictEqual(FdView.toQuery({ range: null, filter: FdFilter.empty(), au: null, marker: null, night: "", rp: null, sp: 1 }), "", "defaults write nothing (an empty filter, speed 1)");
assert.strictEqual(FdView.toQuery({ sp: 1.5, rp: 5 }), "rp=5", "a speed the replay does not have is not written");
assert.strictEqual(FdView.toQuery({ marker: "deaths" }), "mk=deaths");
assert.strictEqual(FdView.toQuery({ marker: { name: "Blood Siphon" } }), "mk=nBlood%20Siphon", "a spell name is URL-encoded");
eq(FdView.fromQuery(new URLSearchParams("mk=nBlood%20Siphon")).marker, { name: "Blood Siphon" });
assert.strictEqual(FdView.toQuery({ marker: { name: "x".repeat(81) } }), "", "a marker name of 81 characters is not written");
assert.strictEqual(FdView.toQuery({ range: { from: 9, to: 3 } }), "", "a range the wrong way round is not written");

// every key is checked alone; a bad one is dropped and named, the good ones stay
const bad = (q, o) => FdView.fromQuery(new URLSearchParams(q), o);
let b = bad("r=9-3&au=24321");
assert.strictEqual(b.range, null, "r=9-3 is dropped");
eq(b.bad, ["r"], "and listed");
assert.strictEqual(b.au, 24321, "the other key survives");
eq(bad("mk=n" + "y".repeat(81)).bad, ["mk"], "mk=n + 81 characters is dropped");
assert.strictEqual(bad("mk=n" + "y".repeat(80)).marker.name.length, 80, "80 characters is the limit");
eq(bad("sp=3").bad, ["sp"], "sp=3 is not a replay speed");
eq(bad("sp=0.5").sp, 0.5);
eq(bad("sp=8").sp, 8, "8x is a replay speed (meters make long fights worth watching fast)");
eq(bad("sp=16").sp, 16, "and 16x");
eq(bad("sp=32").bad, ["sp"], "32x is not");
assert.strictEqual(FdView.toQuery({ sp: 16, rp: 5 }), FdView.toQuery(FdView.fromQuery(new URLSearchParams("rp=5&sp=16"))), "16x survives the round trip");
assert.ok(/(^|&)sp=16(&|$)/.test(FdView.toQuery({ sp: 16, rp: 5 })), "and is written");
eq(bad("mk=n").bad, ["mk"], "mk=n with no name");
eq(bad("mk=s0").bad, ["mk"], "a spell id of 0");
eq(bad("au=0&au=5").bad, ["au"], "the first value counts");
eq(bad("au=99999999").bad, ["au"], "past 10^7");
eq(bad("au=1e3").bad, ["au"], "no exponent forms");
eq(bad("r=0-86400001").bad, ["r"], "past a day");
eq(bad("r=5-5").bad, ["r"], "an empty range");
eq(bad("r=-5-9").bad, ["r"]);
eq(bad("r=1.5-9").bad, ["r"]);
eq(bad("n=2026-13-1").bad, ["n"], "a night is YYYY-MM-DD");
eq(bad("n=2026-11-06").bad, [], "a night");
eq(bad("rp=86400001").bad, ["rp"]);
eq(bad("rp=-1").bad, ["rp"]);
eq(bad("f=1.!!!").bad, ["f"], "a filter that does not decode");
eq(bad("f=2.abc").bad, ["f"], "a filter with the wrong prefix");
eq(bad("f=1." + "A".repeat(2000)).bad, ["f"], "a filter text past 2000 characters");
eq(bad("f=" + "A".repeat(5000)).bad, ["f"], "a huge value is not read");
eq(bad("au=").bad, ["au"], "an empty value");
eq(bad("mk=n<img src=x onerror=alert(1)>").marker, { name: "<img src=x onerror=alert(1)>" }, "a name is data (the page escapes it where it shows it)");
eq(bad("mk=n%00x").bad, ["mk"], "a control character in a name");
eq(bad("zzz=1&bot=5").bad, [], "keys that are not the view's are left alone");
eq(FdView.fromQuery(null).bad, [], "no query at all");
eq(FdView.fromQuery({ au: {}, sp: "2" }).bad, ["au"], "a plain object with a non-text value");
assert.strictEqual(FdView.fromQuery({ r: 7 }).range, null, "a number without its pair is no range");

// a hostile link never throws, whatever the bytes
["%", "%E0%A4%A", "r=%ff", "f=%00", "mk=%ud800", "__proto__=1", "constructor=1&au=1", "r=1-2-3", "sp=NaN", "sp=Infinity", "lw=NaN", "rp=0x10"].forEach((q) => {
  const r = FdView.fromQuery(new URLSearchParams(q));
  assert.ok(Array.isArray(r.bad), q);
});

// stripFd keeps what is not the view's
eq(Array.from(FdView.stripFd(new URLSearchParams("bot=5&r=1-9&p=gear&f=1.x&au=3&mk=deaths&n=2026-11-06&rp=1&sp=2&lw=30&demo=1")).entries()),
  [["bot", "5"], ["p", "gear"], ["demo", "1"]], "stripFd keeps bot, p and the rest");
eq(FdView.stripFd({ bot: "5", r: "1-9", lw: "30" }), { bot: "5" }, "and works on a plain object");
const src = new URLSearchParams("bot=5&r=1-9");
FdView.stripFd(src);
assert.strictEqual(src.toString(), "bot=5&r=1-9", "it returns a copy");

// LIVE: lw is the window in seconds, written only when it is not the default 60; a following range is not written; no replay or night
const LIVEQ = FdView.toQuery({ live: true, lw: 120, filter: FILTER, range: { from: 1000, to: 9000 }, follow: true, night: "2026-11-06", rp: 5, sp: 2 });
assert.strictEqual(LIVEQ, "f=" + CODE + "&lw=120", "a live view with follow writes no r, and never rp, sp or n: " + LIVEQ);
assert.strictEqual(FdView.toQuery({ live: true, lw: 60 }), "", "the default window is not written");
assert.strictEqual(FdView.toQuery({ live: true, lw: 0 }), "lw=0", "0 is the whole fight");
assert.strictEqual(FdView.toQuery({ live: true, lw: 300, range: { from: 1000, to: 9000 }, follow: false }), "r=1000-9000&lw=300", "a fixed live range is written");
assert.strictEqual(FdView.toQuery({ live: true, lw: 45 }), "", "a window the picker does not have is not written");
const lw = FdView.fromQuery(new URLSearchParams("lw=120"));
assert.strictEqual(lw.lw, 120, "lw=120 reads");
assert.strictEqual(FdView.toQuery(lw), "lw=120", "lw=120 round trip");
eq(FdView.fromQuery(new URLSearchParams("lw=45")).bad, ["lw"], "lw=45 is dropped into bad");
eq(FdView.fromQuery(new URLSearchParams("lw=45")).lw, null);
[30, 60, 120, 300, 0].forEach((w) => assert.strictEqual(FdView.fromQuery(new URLSearchParams("lw=" + w)).lw, w, "window " + w));
eq(FdView.fromQuery(new URLSearchParams("lw=abc")).bad, ["lw"]);
const lv = FdView.fromQuery(new URLSearchParams("lw=30&r=1000-9000&n=2026-11-06&rp=5&sp=2&f=" + CODE), { live: true });
assert.deepStrictEqual([lv.lw, lv.follow, lv.night, lv.rp, lv.sp], [30, false, null, null, null], "a live link: a range fixes it, n, rp and sp mean nothing");
eq(lv.bad, ["n", "rp", "sp"]);
eq(lv.range, { from: 1000, to: 9000 });
assert.strictEqual(FdView.toQuery(lv), "r=1000-9000&f=" + CODE + "&lw=30", "a live link round trips");
eq(FdView.fromQuery(new URLSearchParams("lw=30&r=1000-9000"), { live: false }).bad, ["lw"], "a finished fight's link has no window");
console.log("fdview_check.js ok");
