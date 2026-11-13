"use strict";
// Run by commandersjs_test.go under node: FightMeters (web/static/js/fightmeters.js), the pure logic of the Meters panel: at()
// interpolation inside a bucket, before 0 and after the end, window() of a range, rank() (top 10 of 12, sorted, names kept), deathsBy()
// (cut-off, newest first), and fromSamples(), the live series built from the 2 s samples with the same rules as the daemon's
// internal/meters/meters.go (the fixture below is meters_test.go's: a steady 100 dps and 50 hps for 20 s).
const assert = require("assert");
const path = require("path");
const M = require(path.resolve(process.argv[2]));
const eq = (a, b, msg) => assert.deepStrictEqual(JSON.parse(JSON.stringify(a)), b, msg);

// ---- constants and the step rule (internal/meters StepFor)
assert.strictEqual(M.MAX_BUCKETS, 150);
assert.strictEqual(M.stepFor(1), 2000);
assert.strictEqual(M.stepFor(300000), 2000);
assert.strictEqual(M.stepFor(300001), 3000);
assert.strictEqual(M.stepFor(30 * 60 * 1000), 12000, "a 30 min fight is 150 buckets of 12 s");
assert.strictEqual(M.LIVE_SRC, "live-window");

// ---- at(): linear inside a bucket, 0 before the start, the last value after the end
const S = { v: 1, step_ms: 2000, n: 4, src: "window", bots: [{ g: 7, d: [100, 300, 600, 1000], h: [0, 0, 0, 40] }, { g: 8, d: [0, 0, 0, 0], h: [10, 20, 30, 40], flat: true }] };
eq(M.at(S, 1000), [{ g: 7, dmg: 50, heal: 0 }, { g: 8, dmg: 0, heal: 5 }], "mid-bucket 0 is half of its first value");
eq(M.at(S, 3000)[0].dmg, 200, "mid-bucket 1: 100 + half of 200");
eq(M.at(S, 2000)[0].dmg, 100, "exactly on a bucket end it is that bucket's value");
eq(M.at(S, 0)[0], { g: 7, dmg: 0, heal: 0 }, "at 0 nothing yet");
eq(M.at(S, -5000)[0].dmg, 0, "before 0 it is 0");
eq(M.at(S, 8000)[0].dmg, 1000, "at the end of the last bucket the last value");
eq(M.at(S, 99000)[0].dmg, 1000, "after the end the last value");
eq(M.at(S, NaN)[0].dmg, 0, "a broken time reads 0");
eq(M.at(null, 1000), [], "no series, no rows");
eq(M.at({ bots: "x" }, 1000), []);
// endMs: the fight ends inside its last bucket (7 s of 8 s): the curve reaches its last value there, not a bucket later
eq(M.at(S, 7000, 7000)[0].dmg, 1000, "at the fight's own end the exact total");
eq(M.at(S, 6500, 7000)[0].dmg, 600 + 400 * 0.5 / 1, "inside the cut last bucket the slope is steeper (500 of 1000 ms)");
eq(M.at(S, 6000, 7000)[0].dmg, 600, "a bucket end before the fight's end is unchanged");

// ---- window(): the difference of two places
eq(M.window(S, 2000, 6000)[0], { g: 7, dmg: 500, heal: 0 }, "600 - 100");
eq(M.window(S, 2000, 2000)[0].dmg, 0, "an empty range");
eq(M.window(S, 6000, 2000)[0].dmg, 0, "a range the wrong way round is empty, never negative");
eq(M.window(S, 0, 99999)[0].dmg, 1000, "the whole fight");

// ---- rank(): top 10 of 12, biggest first, names from the fight, ties by name, nothing-rows left out, pct of the top
const bots = [];
const rows = [];
for (let i = 1; i <= 12; i++) {
  bots.push({ name: "Bot" + String.fromCharCode(64 + i), guid: i, role: i === 2 ? "healer" : "dps" });
  rows.push({ g: i, dmg: i * 1000, heal: i === 2 ? 500 : 0 });
}
rows.push({ g: 99, dmg: 0, heal: 0 }, { g: 100, dmg: 12000, heal: 0 }); // 100: not named by the fight; ties with the top
const fight = { bots, deaths: [] };
const top = M.rank(fight, rows, "dmg");
assert.strictEqual(top.length, 10, "ten rows of thirteen");
assert.deepStrictEqual(top.map((r) => r.v), [12000, 12000, 11000, 10000, 9000, 8000, 7000, 6000, 5000, 4000], "biggest first");
assert.strictEqual(top[0].name, "#100", "a bot the fight does not name shows its guid; the tie goes to the name ('#' sorts before letters)");
assert.strictEqual(top[1].name, "BotL");
assert.deepStrictEqual([top[0].pct, top[2].pct, top[9].pct], [100, 92, 33], "percent of the top");
assert.strictEqual(M.rank(fight, rows, "heal").length, 1, "only the bot with healing");
assert.strictEqual(M.rank(fight, rows, "heal")[0].role, "healer");
assert.strictEqual(M.rank(fight, rows, "dmg", 3).length, 3, "n");
eq(M.rank(fight, [{ g: 1, dmg: 0, heal: 0 }], "dmg"), [], "nothing done, nothing listed");
eq(M.rank(null, [{ g: 1, dmg: 5, heal: 0 }], "dmg")[0].name, "#1", "no fight at all");
assert.strictEqual(M.rank(fight, [{ g: 1, dmg: 1234.6, heal: 0 }], "dmg")[0].v, 1235, "values are rounded");

// ---- deathsBy(): up to the time, newest first
const deaths = [{ name: "A", t_ms: 5000 }, { name: "B", t_ms: 20000 }, { name: "C", t_ms: 12000 }];
eq(M.deathsBy({ deaths }, 12000).map((d) => d.name), ["C", "A"], "t_ms <= the time, newest first");
eq(M.deathsBy({ deaths }, 4999), [], "none yet");
eq(M.deathsBy({ deaths }, 99999).map((d) => d.name), ["B", "C", "A"]);
eq(M.deathsBy({}, 5), [], "a fight without deaths");
eq(M.deathsBy(null, 5), []);

// ---- fromSamples(): the live series (the same fixture as the Go test: reports every 2 s from 2 s to 24 s, 100 dps, 50 hps, window 10 s)
const START = 1000000;
const steady = [];
for (let s = 2; s <= 24; s += 2) steady.push({ t: START + s * 1000, bots: { 7: { dps: 100, hps: 50 } }, mobs: [], w: 10 });
const live = M.fromSamples(steady, START, 20000);
assert.strictEqual(live.src, "live-window", "the source says it is the live estimate");
assert.strictEqual(live.v, 1);
assert.strictEqual(live.step_ms, 2000);
assert.strictEqual(live.n, 10);
eq(live.bots.map((b) => b.g), [7]);
eq(live.bots[0].d, [200, 400, 600, 800, 1000, 1200, 1400, 1600, 1800, 2000], "ten counted reports of 200 each: the Go test's cumulative");
eq(live.bots[0].h, [100, 200, 300, 400, 500, 600, 700, 800, 900, 1000]);
assert.ok(Math.abs(live.bots[0].d[9] - 2000) <= 2000, "the last is the integral (within one step's worth of the ideal 2000)");
assert.ok(!("flat" in live.bots[0]), "nothing is scaled, so nothing is flat");
eq(M.at(live, 10000, 20000)[0].dmg, 1000, "the middle of the fight is half");

// the first damage is shifted half a window (5 s) earlier: one report at 6 s lands at 1 s (bucket 0), one at 14 s at 9 s (bucket 4)
let one = M.fromSamples([{ t: START + 6000, bots: { 7: { dps: 100, hps: 0 } }, w: 10 }], START, 20000);
eq(one.bots[0].d.slice(0, 2), [200, 200], "a report at 6 s puts its damage in bucket 0, not bucket 3");
one = M.fromSamples([{ t: START + 14000, bots: { 7: { dps: 100, hps: 0 } }, w: 10 }], START, 20000);
assert.strictEqual(one.bots[0].d.findIndex((v) => v > 0), 4, "a report at 14 s first counts in bucket 4");
assert.strictEqual(M.fromSamples([{ t: START + 4000, bots: { 7: { dps: 100 } }, w: 10 }], START, 20000).bots.length, 0, "a report centred before the pull (4 s - 5 s) is not counted");
// no window named: the shift is 5 s too
eq(M.fromSamples([{ t: START + 6000, bots: { 7: { dps: 100 } } }], START, 20000).bots[0].d[0], 200, "no window: 10 s assumed");
// a 10 s hole stands for 4 s
const hole = M.fromSamples([{ t: START + 6000, bots: { 7: { dps: 100 } }, w: 10 }, { t: START + 16000, bots: { 7: { dps: 100 } }, w: 10 }], START, 40000);
eq(hole.bots[0].d[0], 200, "the first report stands for 2 s");
eq(hole.bots[0].d[hole.n - 1], 600, "the second for 4 s, not 10");
// bots come sample by sample, a bot listed later starts at 0, malformed samples are skipped
const mixed = M.fromSamples([null, { t: START + 6000, bots: { 9: { dps: 10 }, 3: { dps: 20 } }, w: 10 }, { t: START + 8000, bots: { 5: { dps: 30 } }, w: 10 }, { t: "x" }], START, 20000);
eq(mixed.bots.map((b) => b.g), [3, 9, 5], "bots of one sample in guid order (object keys), later ones after them");
assert.strictEqual(M.fromSamples(steady, START, 0), null, "no duration, no series");
assert.strictEqual(M.fromSamples(steady, START, -5), null);
eq(M.fromSamples([], START, 20000).bots, [], "no samples: a series with no bots");
eq(M.fromSamples(null, START, 20000).bots, []);
// bounded: a long fight is never more than 150 buckets
const longS = [];
for (let s = 6; s <= 1805; s += 2) longS.push({ t: START + s * 1000, bots: { 7: { dps: 100, hps: 0 } }, w: 10 });
const long = M.fromSamples(longS, START, 30 * 60 * 1000);
assert.ok(long.n <= 150 && long.bots[0].d.length === long.n && long.step_ms === 12000, "30 min: " + long.n + " buckets of " + long.step_ms);
console.log("fightmeters_check.js ok");
