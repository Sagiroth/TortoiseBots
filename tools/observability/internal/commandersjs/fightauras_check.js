"use strict";
// Run by commandersjs_test.go under node: FightAuras (web/static/js/fightauras.js), the pure logic of the Fight debugger's Auras
// panel: intervals from aura rows, alive spans, raid and per-bot uptime, boss-cast and death markers, who had the aura at each
// marker, per-cycle uptime, application lengths, the boss presets, and (live) the rows FdLive rebuilds from the 2 s samples.
// args: fightauras.js, then fdlive.js.
const assert = require("assert");
const path = require("path");
const A = require(path.resolve(process.argv[2]));
const FdLive = require(path.resolve(process.argv[3]));

const POISON = 24321, SIPHON = 24324;
// A Hakkar fight: A tank, B healer, C and D dps. D dies at 60 s. Two Blood Siphons.
const fight = () => ({
  boss: 14834, dur_ms: 120000,
  bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "healer" }, { name: "C", guid: 3, role: "dps" }, { name: "D", guid: 4, role: "dps" }],
  auras: [[10000, 1, POISON, 1], [40000, 1, POISON, -1], [30000, 2, POISON, 1], [5000, 3, POISON, -1], [1, "x"]],
  log: [{ k: "cast", t: 35000, n: "Hakkar", s: "Blood Siphon", sid: SIPHON }, { k: "cast", t: 95000, n: "Hakkar", s: "Blood Siphon", sid: SIPHON },
    { k: "cast", t: 50000, n: "Hakkar", s: "Cause Insanity", sid: 24327 }, { k: "death", t: 60000, n: "D", g: 4, by: "Hakkar" }],
  deaths: [{ name: "D", t_ms: 60000, killer: "Hakkar" }],
});

// ---- intervals: apply/remove pairs, an open one closes at the end, a remove without an apply was on at the pull, junk is skipped
const f = fight();
const iv = A.intervals(f);
assert.deepStrictEqual(iv.bySid[POISON][1], [[10000, 40000]], "A");
assert.deepStrictEqual(iv.bySid[POISON][2], [[30000, 120000]], "B: still on at the end");
assert.deepStrictEqual(iv.bySid[POISON][3], [[0, 5000]], "C: removed with no apply was on at the pull");
assert.strictEqual(iv.bySid[POISON][4], undefined, "D never had it");
assert.deepStrictEqual(iv.sids, [POISON]);
assert.strictEqual(iv.capped, false);
assert.strictEqual(Object.keys(iv.bySid[POISON]).length, 3, "the junk row made nothing");
// an apply while one is open is ignored; times clamp into the fight; rows sort by time
const messy = A.intervals({ dur_ms: 1000, bots: [], auras: [[500, 1, 9, 1], [200, 1, 9, 1], [900, 1, 9, -1], [5000, 2, 9, 1], [-30, 2, 9, -1]] });
assert.deepStrictEqual(messy.bySid[9][1], [[200, 900]], "the second apply while open is ignored; rows are sorted first");
assert.deepStrictEqual(messy.bySid[9][2], [[0, 0], [1000, 1000]], "clamped to [0, dur]");
// the module's 1000-row cap shows as `capped`; a sampled fight is not that list
const rows = Array.from({ length: 1000 }, (_, k) => [k, 1, 7, k % 2 ? -1 : 1]);
assert.strictEqual(A.intervals({ dur_ms: 5000, bots: [], auras: rows }).capped, true, "1000 rows: capped");
assert.strictEqual(A.intervals({ dur_ms: 5000, bots: [], auras: rows.slice(1) }).capped, false);
assert.strictEqual(A.intervals({ dur_ms: 5000, bots: [], auras: rows, aura_src: "sampled" }).capped, false, "sampled rows are not the module's capped list");
assert.deepStrictEqual(A.intervals({ dur_ms: 1, bots: [] }).sids, [], "no rows at all");

// ---- alive spans: a bot is alive until its first death in fight.deaths
assert.deepStrictEqual(A.aliveSpans(f), { 1: [0, 120000], 2: [0, 120000], 3: [0, 120000], 4: [0, 60000] });

// ---- uptime: the raid sums every bot (D alive 60 s, never had it); the range clips intervals and the alive span
const u = A.uptime(iv, POISON, f, null);
assert.strictEqual(u.raid.upMs, 30000 + 90000 + 5000);
assert.strictEqual(u.raid.aliveMs, 120000 * 3 + 60000);
assert.ok(Math.abs(u.raid.pct - (125000 / 420000) * 100) < 1e-9, "raid pct " + u.raid.pct);
assert.deepStrictEqual(u.bots.map((b) => b.name), ["B", "A", "C"], "pct desc: 75, 25, 4.2; D (no uptime, no applications) is not listed");
assert.deepStrictEqual(u.bots.find((b) => b.name === "A"), { guid: "1", name: "A", role: "tank", upMs: 30000, aliveMs: 120000, pct: 25, apps: 1 });
assert.strictEqual(A.uptime(iv, POISON, f, { from: 30000, to: 40000 }).bots.find((b) => b.name === "A").pct, 100, "A over 30-40 s");
const ranged = A.uptime(iv, POISON, f, { from: 50000, to: 100000 });
assert.strictEqual(ranged.bots.find((b) => b.name === "D"), undefined);
assert.strictEqual(ranged.raid.aliveMs, 50000 * 3 + 10000, "D is alive for 10 s of the range");
assert.strictEqual(ranged.bots.find((b) => b.name === "A"), undefined, "A's interval ended before the range and starts no application in it");
assert.strictEqual(A.uptime(iv, 555, f, null).raid.pct, 0, "an aura nobody had");
assert.strictEqual(A.uptime(iv, POISON, { dur_ms: 0, bots: [] }, null).raid.pct, 0, "no bots, no division by zero");
// a bot that died with the aura on stops counting at its death
const dead = { dur_ms: 100000, bots: [{ name: "X", guid: 9, role: "dps" }], deaths: [{ name: "X", t_ms: 40000, killer: "k" }], auras: [[10000, 9, 5, 1]] };
assert.deepStrictEqual(A.uptime(A.intervals(dead), 5, dead, null).bots[0], { guid: "9", name: "X", role: "dps", upMs: 30000, aliveMs: 40000, pct: 75, apps: 1 });
// at most 40 bots are listed
const many = { dur_ms: 1000, bots: Array.from({ length: 50 }, (_, k) => ({ name: "b" + k, guid: k + 1, role: "dps" })), auras: Array.from({ length: 50 }, (_, k) => [0, k + 1, 3, 1]) };
assert.strictEqual(A.uptime(A.intervals(many), 3, many, null).bots.length, 40);

// ---- markers: the casts by spell id, or by name when the id is missing; deaths with their killer
assert.deepStrictEqual(A.markers(f, { sid: SIPHON, name: "Blood Siphon" }).map((m) => [m.t, m.label]), [[35000, "Blood Siphon"], [95000, "Blood Siphon"]]);
assert.deepStrictEqual(A.markers({ log: [{ k: "cast", t: 7, s: "BLOOD siphon" }, { k: "cast", t: 8, s: "Other" }, { k: "death", t: 9, s: "Blood Siphon" }] }, { name: "blood siphon" }).map((m) => m.t), [7], "by name only, case-insensitive; a death event is not a cast");
assert.deepStrictEqual(A.markers(f, { sid: 24327 }).map((m) => m.t), [50000], "a pick with only a spell id");
const dm = A.markers(f, "deaths");
assert.deepStrictEqual(dm.map((m) => [m.t, m.label, m.guid]), [[60000, "D (Hakkar)", "4"]]);
assert.deepStrictEqual(A.markers(f, null), []);

// ---- who had it at each marker
const marks = A.markers(f, { sid: SIPHON, name: "Blood Siphon" });
const at = A.atMarkers(iv, POISON, f, marks);
assert.deepStrictEqual([at[0].on, at[0].alive, at[0].names], [2, 4, ["A", "B"]], "at 35 s: A and B of the four alive");
assert.deepStrictEqual([at[1].on, at[1].alive, at[1].names], [1, 3, ["B"]], "at 95 s: only B, and D is dead");
const atD = A.atMarkers(iv, POISON, f, dm);
assert.deepStrictEqual([atD[0].on, atD[0].alive], [0, 1], "the dead bot D did not have it when it died");
const f2 = fight(); f2.auras.push([50000, 4, POISON, 1]);
assert.strictEqual(A.atMarkers(A.intervals(f2), POISON, f2, dm)[0].on, 1, "an interval that runs to the death counts (from <= t <= to)");

// ---- cycles: [prev cast or 0, cast], the raid's uptime over it, the deaths in it, then "after"
const cy = A.cycles(iv, POISON, f, marks);
assert.deepStrictEqual(cy.map((c) => [c.i, c.from, c.to]), [[1, 0, 35000], [2, 35000, 95000], ["after", 95000, 120000]]);
assert.strictEqual(cy[0].deaths.length, 0);
assert.strictEqual(cy[1].deaths.length, 1, "D died in the second cycle");
assert.strictEqual(cy[2].on, undefined, "the stretch after the last cast has no had-it count");
assert.deepStrictEqual([cy[0].on, cy[0].alive, cy[1].on, cy[1].alive], [2, 4, 1, 3]);
assert.ok(Math.abs(cy[0].raidPct - (A.uptime(iv, POISON, f, { from: 0, to: 35000 }).raid.pct)) < 1e-9);
assert.deepStrictEqual(A.cycles(iv, POISON, f, dm), [], "deaths are not cycles");

// ---- applications: per bot, median and longest, longest first
const app = A.applications(iv, POISON, f);
assert.deepStrictEqual(app.map((a) => [a.name, a.n, a.longestMs]), [["B", 1, 90000], ["A", 1, 30000], ["C", 1, 5000]]);
const two = { dur_ms: 100000, bots: [{ name: "T", guid: 1, role: "tank" }], auras: [[0, 1, 4, 1], [10000, 1, 4, -1], [20000, 1, 4, 1], [50000, 1, 4, -1], [60000, 1, 4, 1], [90000, 1, 4, -1]] };
assert.deepStrictEqual(A.applications(A.intervals(two), 4, two)[0], { guid: "1", name: "T", n: 3, medianMs: 30000, longestMs: 30000 }, "median of 10, 30, 30 s");

// ---- defaults and the cast choices
assert.deepStrictEqual(A.defaults(f, iv), { sid: POISON, marker: { sid: SIPHON, name: "Blood Siphon" } }, "Hakkar: Poisonous Blood, marked by Blood Siphon");
const other = fight(); other.boss = 1;
assert.deepStrictEqual(A.defaults(other, iv), { sid: POISON, marker: null }, "another boss: the first aura seen");
assert.deepStrictEqual(A.defaults(f, A.intervals({ dur_ms: 1, bots: [] })), { sid: null, marker: null }, "no aura rows: nothing to pick");
const jin = { boss: 11380, dur_ms: 1000, bots: [], auras: [[0, 1, 24306, 1], [0, 1, 17172, 1]] };
assert.deepStrictEqual(A.defaults(jin, A.intervals(jin)), { sid: 17172, marker: { sid: 17172, name: "Hex" } }, "Jin'do: Hex (the first of his auras that was seen)");
assert.deepStrictEqual(A.castChoices(f), [{ sid: SIPHON, name: "Blood Siphon" }, { sid: 24327, name: "Cause Insanity" }], "distinct, by name");
assert.deepStrictEqual(A.castChoices({ log: [{ k: "cast", t: 1, s: "Zap" }, { k: "cast", t: 2, s: "zap" }, { k: "cast", t: 3 }] }), [{ sid: null, name: "Zap" }]);

// ---- names: the table, then the live view's learned names, then "spell <id>"
assert.strictEqual(A.auraName(POISON), "Poisonous Blood");
assert.strictEqual(A.auraName(999, { 999: "Burn" }), "Burn", "a name the live view learned");
assert.strictEqual(A.auraName(999), "spell 999");
assert.strictEqual(A.auraName(999, { 998: "x" }), "spell 999");
assert.strictEqual(A.auraName(POISON, { [POISON]: "Other" }), "Poisonous Blood", "the table wins");

// ---- live: the rows FdLive rebuilds from three 2 s samples feed intervals (the aura came on at the second, went at the third)
{
  const start = 1000000;
  const sample = (t, db) => ({ t: start + t, bots: { 1: { db }, 2: { db: [] } }, mobs: [], w: 10 });
  const samples = [sample(0, []), sample(2000, [POISON]), sample(4000, [])];
  const rowsLive = FdLive.auraRowsFrom(samples, start);
  assert.deepStrictEqual(rowsLive, [[2000, 1, POISON, 1], [4000, 1, POISON, -1]]);
  const lf = { dur_ms: 6000, bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "dps" }], auras: rowsLive, aura_src: "sampled" };
  assert.deepStrictEqual(A.intervals(lf).bySid[POISON][1], [[2000, 4000]], "the sampled rows give the interval");
  // the same through applyTrackX, as the post-fight fallback does it
  const st = FdLive.create("x");
  FdLive.applyTrackX(st, { frames: [{ t: 1000, bots: [[1, 0, 0, 100, 1, 0, 0, 0, []]], mobs: [], w: 10 }, { t: 1002, bots: [[1, 0, 0, 100, 1, 0, 0, 0, [POISON]]], mobs: [], w: 10 }, { t: 1004, bots: [[1, 0, 0, 100, 1, 0, 0, 0, []]], mobs: [], w: 10 }] });
  assert.deepStrictEqual(FdLive.auraRowsFrom(st.samples, 1000000), [[2000, 1, POISON, 1], [4000, 1, POISON, -1]], "track rows give the same aura rows");
}
console.log("fightauras_check.js ok");
