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
assert.strictEqual(messy.bySid[9][2], undefined, "(r1 L1) rows clamped to the fight leave no interval of zero length: [0, 0] and [1000, 1000] are skipped");
assert.deepStrictEqual(A.intervals({ dur_ms: 1000, bots: [], auras: [[400, 1, 9, 1], [2000, 1, 9, -1]] }).bySid[9][1], [[400, 1000]], "a time past the end clamps to dur");
assert.strictEqual(A.intervals({ dur_ms: 1000, bots: [], auras: [[1000, 1, 9, 1]] }).bySid[9], undefined, "an apply at the very end is an instant");
// the module's 1000-row cap shows as `capped`; a sampled fight is not that list
const rows = Array.from({ length: 1000 }, (_, k) => [k, 1, 7, k % 2 ? -1 : 1]);
assert.strictEqual(A.intervals({ dur_ms: 5000, bots: [], auras: rows }).capped, true, "1000 rows: capped");
assert.strictEqual(A.intervals({ dur_ms: 5000, bots: [], auras: rows.slice(1) }).capped, false);
assert.strictEqual(A.intervals({ dur_ms: 5000, bots: [], auras: rows, aura_src: "sampled" }).capped, false, "sampled rows are not the module's capped list");
assert.deepStrictEqual(A.intervals({ dur_ms: 1, bots: [] }).sids, [], "no rows at all");

// only the first four values of a row are read (a fifth, of any kind, is ignored); a row whose first four are not all numbers is skipped (null is not a number)
{
  const iv5 = A.intervals({ dur_ms: 10000, bots: [], auras: [[1000, 1, 9, 1, "extra"], [5000, 1, 9, -1, null, {}], [1000, 2, 9, 1], [2000, 2, 9], [1000, 3, 9, "x"], [1000, null, 9, 1]] });
  assert.deepStrictEqual(iv5.bySid[9][1], [[1000, 5000]], "extra values are ignored");
  assert.deepStrictEqual(iv5.bySid[9][2], [[1000, 10000]], "the three-value row is skipped");
  assert.strictEqual(iv5.bySid[9][3], undefined);
  assert.deepStrictEqual(Object.keys(iv5.bySid[9]), ["1", "2"], "a null guid is no guid");
}
// a cast with a negative time is not a marker (the daemon drops them as well)
assert.deepStrictEqual(A.markers({ log: [{ k: "cast", t: -5, s: "Blood Siphon", sid: 24324 }, { k: "cast", t: 0, s: "Blood Siphon", sid: 24324 }, { k: "cast", t: 7, s: "Blood Siphon", sid: 24324 }] }, { sid: 24324, name: "Blood Siphon" }).map((m) => m.t), [0, 7]);

// ---- alive spans: a bot is alive until its first death in fight.deaths
assert.deepStrictEqual(A.aliveSpans(f), { 1: [0, 120000], 2: [0, 120000], 3: [0, 120000], 4: [0, 60000] });

// ---- uptime: the raid sums every bot (D alive 60 s, never had it); the range clips intervals and the alive span
const u = A.uptime(iv, POISON, f, null);
assert.strictEqual(u.raid.upMs, 30000 + 90000 + 5000);
assert.strictEqual(u.raid.aliveMs, 120000 * 3 + 60000);
assert.ok(Math.abs(u.raid.pct - (125000 / 420000) * 100) < 1e-9, "raid pct " + u.raid.pct);
assert.deepStrictEqual(u.bots.map((b) => b.name), ["B", "A", "C", "D"], "(r1) every fight bot, pct desc: 75, 25, 4.2, then D at 0");
assert.deepStrictEqual(u.bots.find((b) => b.name === "D"), { guid: "4", name: "D", role: "dps", upMs: 0, aliveMs: 60000, pct: 0, apps: 0, lower: false });
assert.deepStrictEqual(u.bots.find((b) => b.name === "A"), { guid: "1", name: "A", role: "tank", upMs: 30000, aliveMs: 120000, pct: 25, apps: 1, lower: false });
assert.strictEqual(A.uptime(iv, POISON, f, { from: 30000, to: 40000 }).bots.find((b) => b.name === "A").pct, 100, "A over 30-40 s");
const ranged = A.uptime(iv, POISON, f, { from: 50000, to: 100000 });
assert.strictEqual(ranged.bots.find((b) => b.name === "D").pct, 0, "D is listed with 0");
assert.strictEqual(ranged.raid.aliveMs, 50000 * 3 + 10000, "D is alive for 10 s of the range");
assert.deepStrictEqual([ranged.bots.find((b) => b.name === "A").upMs, ranged.bots.find((b) => b.name === "A").apps], [0, 0], "A's interval ended before the range and starts no application in it");
assert.strictEqual(A.uptime(iv, 555, f, null).raid.pct, 0, "an aura nobody had");
assert.strictEqual(A.uptime(iv, POISON, { dur_ms: 0, bots: [] }, null).raid.pct, 0, "no bots, no division by zero");
// a bot that died with the aura on stops counting at its death
const dead = { dur_ms: 100000, bots: [{ name: "X", guid: 9, role: "dps" }], deaths: [{ name: "X", t_ms: 40000, killer: "k" }], auras: [[10000, 9, 5, 1]] };
assert.deepStrictEqual(A.uptime(A.intervals(dead), 5, dead, null).bots[0], { guid: "9", name: "X", role: "dps", upMs: 30000, aliveMs: 40000, pct: 75, apps: 1, lower: false });
// every bot of a raid is listed (40), those with nothing at 0 %
const many = { dur_ms: 1000, bots: Array.from({ length: 40 }, (_, k) => ({ name: "b" + k, guid: k + 1, role: "dps" })), auras: Array.from({ length: 10 }, (_, k) => [0, k + 1, 3, 1]) };
const manyU = A.uptime(A.intervals(many), 3, many, null);
assert.deepStrictEqual([manyU.bots.length, manyU.bots.filter((b) => b.pct === 0).length], [40, 30]);
// (r2 M1) a capped list: the oldest rows are gone, so for EVERY bot nothing is known before the earliest kept row (T0); a removal whose apply
// was lost means up from T0 to it; a bot with no kept row of the aura may have held it throughout (unknown for the whole fight)
const capFix = () => {
  const rows = Array.from({ length: 996 }, (_, k) => [20000 + k, 2, 8, k % 2 ? -1 : 1]); // filler: bot B, another aura (T0 = 20 s)
  rows.push([30000, 1, POISON, -1], [50000, 1, POISON, 1], [70000, 1, POISON, -1]); // A: a removal with no apply, then 50-70 s
  rows.push([40000, 3, POISON, 1]); // C: first row an apply after T0, never removed
  return { boss: 14834, dur_ms: 100000, aura_src: undefined,
    bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "healer" }, { name: "C", guid: 3, role: "dps" }, { name: "D", guid: 4, role: "dps" }],
    auras: rows, deaths: [{ name: "D", t_ms: 15000, killer: "k" }], log: [{ k: "cast", t: 10000, s: "Blood Siphon", sid: SIPHON }, { k: "cast", t: 25000, s: "Blood Siphon", sid: SIPHON }, { k: "cast", t: 35000, s: "Blood Siphon", sid: SIPHON }] };
};
{
  const cap = capFix();
  assert.strictEqual(cap.auras.length, 1000);
  const civ = A.intervals(cap);
  assert.strictEqual(civ.capped, true);
  assert.strictEqual(civ.capFrom, 20000, "T0 is the earliest kept row");
  assert.deepStrictEqual(civ.bySid[POISON][1], [[20000, 30000], [50000, 70000]], "a leading removal at 30 s: up over [T0, 30 s] (known), no [0, 30 s] made up");
  assert.deepStrictEqual(civ.bySid[POISON][3], [[40000, 100000]]);
  assert.deepStrictEqual(civ.sids, [8, POISON]);
  assert.deepStrictEqual(A.unknownSpans(civ, POISON, 1, cap), [[0, 20000]], "A: unknown before T0");
  assert.deepStrictEqual(A.unknownSpans(civ, POISON, 3, cap), [[0, 20000]], "C: its first row is an apply after T0, and it is unknown before T0 all the same");
  assert.deepStrictEqual(A.unknownSpans(civ, POISON, 2, cap), [[0, 100000]], "B has no kept row of the aura: unknown throughout");
  assert.deepStrictEqual(A.unknownSpans(A.intervals({ dur_ms: 1000, bots: [], auras: [[0, 1, 1, 1]] }), 1, 1, { dur_ms: 1000 }), [], "an uncapped list has no unknown stretch");
  const cu = A.uptime(civ, POISON, cap, null);
  const by = (n) => cu.bots.find((b) => b.name === n);
  assert.deepStrictEqual([by("A").upMs, by("A").lower, by("A").pct], [30000, true, 30], "A: at least 30 %");
  assert.deepStrictEqual([by("C").upMs, by("C").lower], [60000, true], "C: an apply first still reads as a minimum");
  assert.deepStrictEqual([by("B").upMs, by("B").lower], [0, true], "B: no rows, nothing known up: '-' (lower with 0 up)");
  assert.strictEqual(by("D").lower, true);
  assert.strictEqual(cu.raid.lower, true);
  // the same rows under the cap are exact: on from the pull, no unknown stretch
  const few = Object.assign(capFix(), { auras: capFix().auras.slice(-4) });
  assert.deepStrictEqual(A.intervals(few).bySid[POISON][1], [[0, 30000], [50000, 70000]]);
  assert.strictEqual(A.uptime(A.intervals(few), POISON, few, null).raid.lower, false);
  // (r2 Codex) a cast and a death inside the unknown stretch are "unknown", not "no"; a cast after T0 is known except for the bots with no rows
  const marks = A.markers(cap, { sid: SIPHON });
  const at = A.atMarkers(civ, POISON, cap, marks);
  assert.deepStrictEqual([at[0].t, at[0].on, at[0].alive, at[0].unknown], [10000, 0, 4, 4], "10 s, before T0: every bot unknown");
  assert.deepStrictEqual([at[1].t, at[1].on, at[1].alive, at[1].unknown, at[1].names], [25000, 1, 3, 1, ["A"]], "25 s: A had it; C is known not to; B is unknown (D is dead)");
  assert.deepStrictEqual([at[2].on, at[2].unknown], [0, 1], "35 s: A's [20, 30 s] is over, C starts at 40 s: known no; only B unknown");
  const dm = A.markers(cap, "deaths");
  const atD = A.atMarkers(civ, POISON, cap, dm);
  assert.deepStrictEqual([atD[0].on, atD[0].unknown], [0, 1], "D died at 15 s, inside the unknown stretch: '?', not no");
  // cycles carry the lower bound; the after row too
  const cy = A.cycles(civ, POISON, cap, marks);
  assert.ok(cy.every((c) => c.lower === true), "every cycle of a capped fight is a minimum here: " + JSON.stringify(cy.map((c) => c.lower)));
  assert.deepStrictEqual([cy[0].on, cy[0].unknown], [0, 4]);
  assert.ok(A.cycles(A.intervals(fight()), POISON, fight(), A.markers(fight(), { sid: SIPHON })).every((c) => c.lower === false), "an exact fight's cycles are exact");
}

// ---- markers: the casts by spell id, or by name when the id is missing; deaths with their killer
assert.deepStrictEqual(A.markers(f, { sid: SIPHON, name: "Blood Siphon" }).map((m) => [m.t, m.label]), [[35000, "Blood Siphon"], [95000, "Blood Siphon"]]);
assert.deepStrictEqual(A.markers({ log: [{ k: "cast", t: 7, s: "BLOOD siphon" }, { k: "cast", t: 8, s: "Other" }, { k: "death", t: 9, s: "Blood Siphon" }] }, { name: "blood siphon" }).map((m) => m.t), [7], "by name only, case-insensitive; a death event is not a cast");
assert.deepStrictEqual(A.markers(f, { sid: 24327 }).map((m) => m.t), [50000], "a pick with only a spell id");
const dm = A.markers(f, "deaths");
assert.deepStrictEqual(dm.map((m) => [m.t, m.label, m.guid]), [[60000, "D (Hakkar)", "4"]]);
assert.deepStrictEqual(A.markers(f, null), []);

// ---- (r1 L5) with a spell id the id decides; the name only stands in for an event that carries no id
assert.deepStrictEqual(A.markers({ log: [{ k: "cast", t: 1, s: "Blood Siphon", sid: 999 }, { k: "cast", t: 2, s: "Blood Siphon" }, { k: "cast", t: 3, s: "x", sid: SIPHON }] }, { sid: SIPHON, name: "Blood Siphon" }).map((m) => m.t), [2, 3], "another spell of the same name does not match; an event without an id matches by name");

// ---- who had it at each marker
const marks = A.markers(f, { sid: SIPHON, name: "Blood Siphon" });
const at = A.atMarkers(iv, POISON, f, marks);
assert.deepStrictEqual([at[0].on, at[0].alive, at[0].names], [2, 4, ["A", "B"]], "at 35 s: A and B of the four alive");
assert.deepStrictEqual([at[1].on, at[1].alive, at[1].names], [1, 3, ["B"]], "at 95 s: only B, and D is dead");
const atD = A.atMarkers(iv, POISON, f, dm);
assert.deepStrictEqual([atD[0].on, atD[0].alive], [0, 1], "the dead bot D did not have it when it died");
const f2 = fight(); f2.auras.push([50000, 4, POISON, 1]);
assert.strictEqual(A.atMarkers(A.intervals(f2), POISON, f2, dm)[0].on, 1, "an interval that runs to the death counts (from <= t <= to)");

// ---- (r1 L2) the Hex preset looks 2 s past the cast (the aura lands when the cast finishes), 2 s more for sampled rows
{
  const hex = (src, applyAt) => ({ boss: 11380, dur_ms: 60000, aura_src: src, bots: [{ name: "T", guid: 1, role: "tank" }, { name: "H", guid: 2, role: "healer" }],
    log: [{ k: "cast", t: 10000, n: "Jin'do", s: "Hex", sid: 17172, tn: "T" }], deaths: [], auras: [[applyAt, 1, 17172, 1], [applyAt + 8000, 1, 17172, -1]] });
  const pick = A.defaults(hex(undefined, 10500), A.intervals(hex(undefined, 10500))).marker;
  assert.deepStrictEqual(pick, { sid: 17172, name: "Hex" });
  const check = (f) => A.atMarkers(A.intervals(f), 17172, f, A.markers(f, pick))[0];
  assert.deepStrictEqual([check(hex(undefined, 10500)).on, check(hex(undefined, 10500)).alive, check(hex(undefined, 10500)).names], [1, 2, ["T"]], "Hex landed 0.5 s after the cast: the tank had it");
  assert.strictEqual(check(hex(undefined, 12500)).on, 0, "an exact row 2.5 s after the cast is past the window");
  assert.strictEqual(check(hex("sampled", 14000)).on, 1, "a sampled row up to 4 s after the cast counts");
  assert.strictEqual(check(hex("sampled", 14500)).on, 0);
  // Blood Siphon has no window: the aura must hold at the cast itself
  const bs = { boss: 14834, dur_ms: 60000, bots: [{ name: "T", guid: 1, role: "tank" }], deaths: [], log: [{ k: "cast", t: 10000, s: "Blood Siphon", sid: SIPHON }], auras: [[10500, 1, POISON, 1]] };
  assert.strictEqual(A.atMarkers(A.intervals(bs), POISON, bs, A.markers(bs, { sid: SIPHON }))[0].on, 0);
  assert.strictEqual(A.cycles(A.intervals(hex(undefined, 10500)), 17172, hex(undefined, 10500), A.markers(hex(undefined, 10500), pick))[0].on, 1, "the cycle row uses the same window");
}
// ---- (r1 L3) a victim's aura may be logged just before its death row (50 ms for exact rows, 2 s for sampled ones)
{
  const dies = (src, removedAt) => ({ dur_ms: 60000, aura_src: src, bots: [{ name: "V", guid: 1, role: "dps" }], log: [], deaths: [{ name: "V", t_ms: 30000, killer: "k" }], auras: [[10000, 1, 5, 1], [removedAt, 1, 5, -1]] });
  const had = (f) => A.atMarkers(A.intervals(f), 5, f, A.markers(f, "deaths"))[0].on;
  assert.strictEqual(had(dies(undefined, 29970)), 1, "removed 30 ms before the death");
  assert.strictEqual(had(dies(undefined, 29900)), 0, "100 ms is too long ago for an exact row");
  assert.strictEqual(had(dies("sampled", 28500)), 1, "a sampled row: 2 s");
  assert.strictEqual(had(dies("sampled", 27500)), 0);
}

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
