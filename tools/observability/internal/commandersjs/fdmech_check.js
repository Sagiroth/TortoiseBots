"use strict";
// Run by commandersjs_test.go under node: FdMech (web/static/js/fdmech.js), the per-fight mechanic row. The dash-2 Hakkar fixture (Blood
// Siphon at 35 s and 95 s; A, B, C, D alive; D dies at 60 s) gives `dosed 0/2` (2 of 4 alive at 35 s, 1 of 3 at 95 s); the same plus an
// apply at 30 s for C and D gives `dosed 1/2` (4 of 4); Son samples in the second window only give `Son pulled 1/2`; no samples `?/2`;
// Jin'do's Hex time on the layout's main tank, Shade deaths by killer, the other bosses' deaths, hostile input.
// args: fdmech.js, fightauras.js, fdlive.js.
const assert = require("assert");
const path = require("path");
const M = require(path.resolve(process.argv[2]));
const A = require(path.resolve(process.argv[3]));
const FdLive = require(path.resolve(process.argv[4]));

const POISON = 24321, SIPHON = 24324, HEX = 17172;
const START_S = 1000, T0 = START_S * 1000; // fight start (unix s) and in ms
const hakkar = (extra) => ({
  boss: 14834, start: START_S, dur_ms: 120000,
  bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "healer" }, { name: "C", guid: 3, role: "dps" }, { name: "D", guid: 4, role: "dps" }],
  auras: [[10000, 1, POISON, 1], [40000, 1, POISON, -1], [30000, 2, POISON, 1], [5000, 3, POISON, -1]].concat(extra || []),
  log: [{ k: "cast", t: 35000, n: "Hakkar", s: "Blood Siphon", sid: SIPHON }, { k: "cast", t: 95000, n: "Hakkar", s: "Blood Siphon", sid: SIPHON },
    { k: "cast", t: 50000, n: "Hakkar", s: "Cause Insanity", sid: 24327 }, { k: "death", t: 60000, n: "D", g: 4, by: "Hakkar" }],
  deaths: [{ name: "D", t_ms: 60000, killer: "Hakkar" }],
});
const row = (f, samples, layout) => M.row(f, A.intervals(f), samples, layout);

// ---- constants
assert.strictEqual(M.DOSED_SHARE, 0.8);
assert.deepStrictEqual([M.HAKKAR, M.SON, M.JINDO, M.SHADE_NAME], [14834, 11357, 11380, "Shade of Jin'do"]);

// ---- Hakkar: dosed cycles (the failing case on dash/wipe-recap: there is no FdMech)
let r = row(hakkar(), []);
assert.strictEqual(r.text, "Hakkar: dosed 0/2 cycles · Son pulled ?/2", "2 of 4 alive at 35 s, 1 of 3 at 95 s: no cycle reaches 80 %");
assert.deepStrictEqual([r.boss, r.parts.cycles, r.parts.dosed, r.parts.sonPulled, r.parts.lower], [14834, 2, 0, null, false]);
r = row(hakkar([[30000, 3, POISON, 1], [30000, 4, POISON, 1]]), null);
assert.strictEqual(r.text, "Hakkar: dosed 1/2 cycles · Son pulled ?/2", "4 of 4 at 35 s is dosed; 2 of 3 at 95 s is not");
assert.strictEqual(r.parts.dosed, 1);

// the 80 % boundary: 4 of 5 counts, 3 of 5 does not (the share is per cast)
const five = (n) => {
  const f = hakkar();
  f.bots = [1, 2, 3, 4, 5].map((g) => ({ name: "B" + g, guid: g, role: "dps" }));
  f.deaths = [];
  f.log = [{ k: "cast", t: 35000, s: "Blood Siphon", sid: SIPHON }];
  f.auras = Array.from({ length: n }, (_, i) => [1000, i + 1, POISON, 1]);
  return f;
};
assert.strictEqual(row(five(4), []).parts.dosed, 1, "4 of 5 = 0.8 counts");
assert.strictEqual(row(five(3), []).parts.dosed, 0, "3 of 5 does not");
assert.strictEqual(row(five(5), []).text, "Hakkar: dosed 1/1 cycles · Son pulled ?/1");

// no cast yet
assert.strictEqual(row(Object.assign(hakkar(), { log: [] }), []).text, "Hakkar: no Blood Siphon yet");
// the cast is found by name when the event carries no id, and the boss by its phase entry
const byName = Object.assign(hakkar(), { boss: 0, boss_phase_entry: 14834, log: [{ k: "cast", t: 35000, s: "Blood Siphon" }] });
assert.strictEqual(row(byName, []).text, "Hakkar: dosed 0/1 cycles · Son pulled ?/1");

// ---- Son pulled: a sample in a cycle's window with a Son of Hakkar on a member
const sample = (relMs, mobs) => ({ t: T0 + relMs, bots: {}, mobs });
const son = (v) => ({ id: "m", e: M.SON, hp: 100, v });
// in the second window [35 s, 95 s) only
r = row(hakkar(), [sample(10000, [{ id: "x", e: 1, hp: 100, v: "1" }]), sample(70000, [son("1")])]);
assert.strictEqual(r.parts.sonPulled, 1);
assert.strictEqual(r.text, "Hakkar: dosed 0/2 cycles · Son pulled 1/2");
// both windows
assert.strictEqual(row(hakkar(), [sample(10000, [son("2")]), sample(70000, [son("3")])]).parts.sonPulled, 2);
// a Son on a non-member (a Son nobody pulled), another mob entry on a member, a sample on the cast's instant, after the last cast: none
assert.strictEqual(row(hakkar(), [sample(10000, [son("999")]), sample(20000, [{ id: "x", e: 5, hp: 1, v: "1" }]), sample(35000, [son("1")]), sample(100000, [son("1")])]).parts.sonPulled, 1,
  "the 35 s sample is in the second window ([35 s, 95 s)); 10 s non-member, 20 s other entry and 100 s (after the last cast) count for nothing");
assert.strictEqual(row(hakkar(), [sample(10000, [son("999")]), sample(20000, [{ id: "x", e: 5, hp: 1, v: "1" }]), sample(100000, [son("1")])]).parts.sonPulled, 0, "samples but no Son pull: 0, not unknown");
// the victim as a number (a track row) matches the member guid
assert.strictEqual(row(hakkar(), [sample(10000, [son(1)])]).parts.sonPulled, 1);
// no samples at all: unknown
assert.strictEqual(row(hakkar(), []).parts.sonPulled, null);
assert.strictEqual(row(hakkar(), undefined).text.endsWith("Son pulled ?/2"), true);
// a running fight's exact start (start_ms) places the samples, not its whole seconds: one cast at 35 s, a Son sample 35.5 s after the whole second
const oneCast = (extra) => Object.assign(hakkar(), { log: [{ k: "cast", t: 35000, s: "Blood Siphon", sid: SIPHON }] }, extra);
assert.strictEqual(row(oneCast({ start_ms: T0 + 900 }), [sample(35500, [son("1")])]).parts.sonPulled, 1, "the sample is 34.6 s into a fight that began 0.9 s after its whole second: before the cast");
assert.strictEqual(row(oneCast(), [sample(35500, [son("1")])]).parts.sonPulled, 0, "without start_ms the same sample is 35.5 s: after the cast");

// ---- a capped aura list makes dosed a minimum
{
  const f = hakkar();
  f.auras = Array.from({ length: 1000 }, (_, i) => [2000 + i * 10, 1, POISON, i % 2 ? -1 : 1]);
  const t = row(f, []).text;
  assert.ok(/dosed ≥\d+\/2/.test(t), "a bot the capped list does not mention is unknown: " + t);
}

// ---- running fight object of FdLive carries start_ms
{
  const st = FdLive.create("Cmdr");
  FdLive.applyEventsFrame(st, { key: "Cmdr\u00001\u00002", from: 0, cur: 1, events: [{ k: "start", t: 0 }], fight: { start_ms: T0 + 250, boss: 14834, boss_name: "Hakkar", fight_ms: 1000 } }, T0 + 1250);
  const lf = FdLive.fight(st, { bots: [{ name: "A", guid: 1, role: "tank" }] }, T0 + 1250);
  assert.strictEqual(lf.start_ms, T0 + 250);
  assert.strictEqual(M.startMs(lf), T0 + 250);
  assert.strictEqual(M.startMs({ start: 7 }), 7000);
  assert.strictEqual(M.startMs({ start: 7, start_ms: 0 }), 7000);
  assert.strictEqual(M.row(lf, FdLive.fight && A.intervals(lf), [], null).text, "Hakkar: no Blood Siphon yet");
  assert.ok(M.row(lf, null, [], null).title.includes("2 s"), "a sampled fight's title says the edges are within 2 s");
}
assert.ok(row(hakkar(), []).title.includes("aura rows"), "a filed fight's title");

// ---- Jin'do: Hex on the main tank, Shade deaths by killer
const jindo = (extra) => Object.assign({
  boss: 11380, start: START_S, dur_ms: 120000,
  bots: [{ name: "T1", guid: 1, role: "tank", taken: 100 }, { name: "T2", guid: 2, role: "tank", taken: 900 }, { name: "H", guid: 3, role: "healer" }],
  auras: [[10000, 1, HEX, 1], [30000, 1, HEX, -1], [10000, 2, HEX, 1], [25000, 2, HEX, -1]],
  deaths: [{ name: "H", t_ms: 1, killer: "Shade of Jin'do" }, { name: "X", t_ms: 2, killer: "Shade of Jin'do" }, { name: "Y", t_ms: 3, killer: "Jin'do the Hexxer" }], log: [],
}, extra || {});
r = row(jindo(), [], { mt: "T1" });
assert.strictEqual(r.text, "Jin'do: Hex on MT 20 s · Shade deaths 2", "layout.mt names T1: 10-30 s");
assert.deepStrictEqual([r.parts.mt, r.parts.hexS, r.parts.shades], ["T1", 20, 2]);
r = row(jindo(), [], null);
assert.strictEqual(r.text, "Jin'do: Hex on MT 15 s · Shade deaths 2", "no layout: the tank with the largest damage taken (T2), 10-25 s = 15 s");
assert.strictEqual(row(jindo(), [], { mt: "Nobody" }).parts.mt, "T2", "a layout name that is not in the fight falls back");
assert.strictEqual(row(jindo(), [], { mt: "T2" }).parts.hexS, 15);
// a live fight has no damage taken: the first tank by name
const liveJ = jindo({ bots: [{ name: "T9", guid: 9, role: "tank" }, { name: "T2", guid: 2, role: "tank" }], auras: [[10000, 2, HEX, 1], [25000, 2, HEX, -1]] });
assert.strictEqual(row(liveJ, [], null).text, "Jin'do: Hex on MT 15 s · Shade deaths 2");
// an open Hex runs to the end of the fight; a rounded number of seconds
assert.strictEqual(row(jindo({ auras: [[100000, 2, HEX, 1]] }), [], null).text, "Jin'do: Hex on MT 20 s · Shade deaths 2");
// no aura rows at all: unknown, not 0 s; rows of another aura only still mean the watch ran
assert.strictEqual(row(jindo({ auras: [] }), [], null).text, "Jin'do: Hex on MT ? · Shade deaths 2");
assert.strictEqual(row(jindo({ auras: [[1000, 2, 24306, 1]] }), [], null).text, "Jin'do: Hex on MT 0 s · Shade deaths 2");
// no tank at all
assert.strictEqual(row(jindo({ bots: [{ name: "H", guid: 3, role: "healer" }] }), [], null).text, "Jin'do: Hex on MT ? · Shade deaths 2");
// Shade deaths are by killer, exactly
assert.strictEqual(row(jindo({ deaths: [{ name: "a", killer: "shade of jin'do" }, { name: "b", killer: "Shade of Jin'do " }] }), [], null).parts.shades, 0);

// ---- any other boss, and trash
assert.strictEqual(row({ boss: 4000, boss_name: "Some Boss", dur_ms: 1, deaths: [{ name: "a" }, { name: "b" }] }, []).text, "Some Boss: deaths 2");
assert.strictEqual(row({ boss: 4000, dur_ms: 1, deaths: [] }, []).text, "Boss: deaths 0");
assert.strictEqual(row({ boss: 0, dur_ms: 1, deaths: [{ name: "a" }] }, []).text, "deaths 1");
assert.strictEqual(row({ dur_ms: 1 }, []).text, "deaths 0");

// ---- hostile input never throws
[null, undefined, 5, "x", [], {}, { boss: 14834, bots: "x", log: 5, deaths: [null, 7], auras: "x" }, { boss: 11380, bots: [null, 3, { guid: 1 }], deaths: [null, { killer: 5 }] },
  { boss: "14834", dur_ms: "x" }, { boss: NaN }].forEach((f) => {
  const out = M.row(f, null, null, null);
  assert.ok(out && typeof out.text === "string" && out.parts && typeof out.title === "string");
});
assert.strictEqual(row(hakkar(), [null, 5, { t: "x" }, { t: T0 + 70000, mobs: "x" }, { t: T0 + 70000, mobs: [null, 7, { e: M.SON }] }]).parts.sonPulled, 0, "broken samples count for nothing");
assert.strictEqual(M.row(hakkar(), A.intervals(hakkar()), { length: 3 }, { mt: 5 }).parts.sonPulled, null, "samples that are not a list are no samples");
const many = Array.from({ length: 5000 }, (_, i) => sample(i * 20, [son("1")]));
const t0 = Date.now();
assert.strictEqual(row(hakkar(), many).parts.sonPulled, 2);
assert.ok(Date.now() - t0 < 1000, "a full sample buffer answers within a bound");
console.log("fdmech ok");
