"use strict";
// Run by commandersjs_test.go under node: WipeRecap (web/static/js/wiperecap.js), the pure logic of the Fight debugger's "first 3
// deaths" box: isWipe() (the outcome, or half the bots dead; a live fight has no outcome yet), firstDeaths() (time order, ties by
// name, the recap's own index kept so a click selects it), hpBefore() (the bot's last health a second or more before the death,
// alive, within 10 s) and framesFromSamples() (the live 2 s samples as the frames hpBefore reads), all on hostile input too.
const assert = require("assert");
const path = require("path");
const W = require(path.resolve(process.argv[2]));
const eq = (a, b, msg) => assert.deepStrictEqual(JSON.parse(JSON.stringify(a)), b, msg);

const names = (n) => Array.from({ length: n }, (_, i) => "Bot" + (i + 1));
const botsOf = (n) => names(n).map((nm, i) => ({ name: nm, guid: i + 1, role: i === 0 ? "tank" : i === 1 ? "healer" : "dps" }));
const deathsOf = (n, t0) => names(n).map((nm, i) => ({ name: nm, t_ms: (t0 || 1000) + i * 500, killer: "Boss" }));

// ---- isWipe: the outcome, or at least half the bots (distinct names) dead
assert.strictEqual(W.isWipe({ outcome: "end", bots: botsOf(20), deaths: deathsOf(11) }), true, "11 of 20 dead on an `end` fight is a wipe (the outcome can be wrong)");
assert.strictEqual(W.isWipe({ outcome: "end", bots: botsOf(20), deaths: deathsOf(10) }), true, "exactly half is a wipe");
assert.strictEqual(W.isWipe({ outcome: "end", bots: botsOf(20), deaths: deathsOf(3) }), false, "3 of 20 is not");
assert.strictEqual(W.isWipe({ outcome: "wipe", bots: botsOf(20), deaths: deathsOf(2) }), true, "the outcome alone");
assert.strictEqual(W.isWipe({ outcome: "end", bots: botsOf(5), deaths: deathsOf(3) }), true, "3 of 5 (ceil(5/2) = 3)");
assert.strictEqual(W.isWipe({ outcome: "end", bots: botsOf(5), deaths: deathsOf(2) }), false, "2 of 5");
const twice = deathsOf(1).concat(deathsOf(1, 9000));
assert.strictEqual(W.isWipe({ outcome: "end", bots: botsOf(4), deaths: twice }), false, "one bot dying twice is one dead bot, not two");
// a live fight: outcome "live", the second rule only
assert.strictEqual(W.isWipe({ outcome: "live", live: true, bots: botsOf(20), deaths: deathsOf(10) }), true, "a live fight with 10 of 20 dead");
assert.strictEqual(W.isWipe({ outcome: "live", live: true, bots: botsOf(20), deaths: deathsOf(9) }), false, "9 of 20");
assert.strictEqual(W.isWipe({ outcome: "live", live: true, bots: [], deaths: deathsOf(3) }), false, "no bots known: no rule can say wipe");
// hostile input never throws
[null, undefined, 5, "x", {}, { bots: "x", deaths: 3 }, { bots: [null, 3], deaths: [null, 7, { name: 5 }] }].forEach((f) => assert.strictEqual(typeof W.isWipe(f), "boolean"));

// ---- firstDeaths: the first n recaps by time, ties by name, each carrying the recap's own index
const fight = { bots: botsOf(20) };
const recap = (i, name, t, extra) => Object.assign({ i, name, guid: Number(name.slice(3)) || undefined, t_ms: t, killer: "Boss", hits: [], total: 0, spell: null, likely: false }, extra || {});
const recaps = [
  recap(0, "Bot9", 5000), recap(1, "Bot4", 2000, { hits: [{ d: 100 }, { d: 200 }], total: 300, spell: { name: "Blood Siphon", likely: false } }),
  recap(2, "Bot2", 2000, { spell: { name: "Mind Control", likely: true }, likely: true }), recap(3, "Bot1", 9000), recap(4, "Bot7", 2000),
];
const first = W.firstDeaths(fight, recaps);
eq(first.map((r) => r.name), ["Bot2", "Bot4", "Bot7"], "time order; the three at 2000 ms are ordered by name, the later ones are cut");
eq(first.map((r) => r.i), [2, 1, 4], "the recap's own index, so a click selects it");
eq(first[0], { i: 2, name: "Bot2", role: "healer", t_ms: 2000, killer: "Boss", spell: "Mind Control", likely: true, total: 0, hits: 0, guid: 2 }, "the row's fields (role from fight.bots)");
eq({ s: first[1].spell, t: first[1].total, h: first[1].hits, l: first[1].likely }, { s: "Blood Siphon", t: 300, h: 2, l: false }, "spell, total and the number of hits");
eq(W.firstDeaths(fight, recaps, 2).map((r) => r.name), ["Bot2", "Bot4"], "n");
eq(W.firstDeaths(fight, [], 3), [], "no deaths");
eq(W.firstDeaths(fight, null), [], "no list");
eq(W.firstDeaths(null, recaps).length, 3, "no fight: the roles are just empty");
eq(W.firstDeaths(fight, [recap(0, "Ghost", 10)])[0].role, "", "a name that is not a bot has no role");
eq(W.firstDeaths(fight, [null, 5, { name: 5, t_ms: "x" }, recap(1, "Bot3", 1)]).map((r) => r.name), ["Bot3"], "broken entries are dropped");
assert.strictEqual(W.firstDeaths(fight, recaps, 1e9).length, 5, "n is bounded by the list");

// ---- hpBefore: the last health a second or more before the death, alive, within 10 s (frames in the replay's shape: t in seconds)
const START = 1000000; // fight start in ms
const frame = (rel, hp, alive) => ({ t: (START + rel) / 1000, bots: { 7: { hp, alive: alive !== false } } });
const track = { frames: [frame(-3000, 40), frame(-500, 5)], names: { 7: "Bot7" } };
// the death at tMs = 10000: frames at 7000 (40 %) and 9500 (5 %): the one 1 s or more before is the 40 %
const tr3 = { frames: [frame(7000, 40), frame(9500, 5)], names: { 7: "Bot7" } };
assert.strictEqual(W.hpBefore(tr3, "Bot7", 10000, START), 40, "40 % at -3 s, 5 % at -0.5 s: the 5 % is too close to the death");
assert.strictEqual(W.hpBefore(tr3, "Bot7", 10000, START, 7), 40, "by guid too");
assert.strictEqual(W.hpBefore(tr3, "Nobody", 10000, START), null, "a name the track does not know");
assert.strictEqual(W.hpBefore(tr3, "Bot7", 10000, START, 99), null, "a guid with no row");
assert.strictEqual(W.hpBefore({ frames: [] }, "Bot7", 10000, START, 7), null, "no frames");
assert.strictEqual(W.hpBefore(null, "Bot7", 10000, START, 7), null, "no track");
assert.strictEqual(W.hpBefore(tr3, "Bot7", 9000, START), 40, "the target is 8000: the 7000 frame");
assert.strictEqual(W.hpBefore({ frames: [frame(8000, 33), frame(9500, 5)], names: { 7: "Bot7" } }, "Bot7", 9000, START), 33, "a frame exactly at tMs - 1000 counts");
assert.strictEqual(W.hpBefore({ frames: [frame(-20000, 80)], names: { 7: "Bot7" } }, "Bot7", 10000, START), null, "older than 10 s before the target: nothing");
assert.strictEqual(W.hpBefore({ frames: [frame(500, 60), frame(2500, 0, false)], names: { 7: "Bot7" } }, "Bot7", 6000, START), 60, "a frame where the bot was dead is skipped for the earlier living one");
assert.strictEqual(W.hpBefore({ frames: [frame(2500, 0, false)], names: { 7: "Bot7" } }, "Bot7", 6000, START), null, "only dead frames: nothing");
assert.strictEqual(W.hpBefore({ frames: [frame(7000, 40.6)], names: { 7: "Bot7" } }, "Bot7", 10000, START), 41, "rounded to a whole percent");
// hostile frames
const bad = { frames: [null, 4, { t: "x", bots: {} }, { t: NaN, bots: { 7: { hp: 1 } } }, { t: (START + 7000) / 1000, bots: null }, { t: (START + 7000) / 1000, bots: { 7: { hp: "x" } } }, { t: (START + 7000) / 1000, bots: { 7: { hp: Infinity } } }, frame(6000, 22)], names: { 7: "Bot7" } };
assert.strictEqual(W.hpBefore(bad, "Bot7", 10000, START), 22, "broken frames are skipped, the good one is read");
[NaN, Infinity, "x", null].forEach((t) => assert.strictEqual(W.hpBefore(tr3, "Bot7", t, START), null, "a broken time: " + t));
assert.strictEqual(W.hpBefore({ frames: "x", names: 5 }, "Bot7", 10000, START), null);
const huge = { frames: Array.from({ length: 200000 }, () => frame(7000, 40)), names: { 7: "Bot7" } };
const t0 = Date.now();
assert.strictEqual(W.hpBefore(huge, "Bot7", 10000, START, 7), 40, "a huge list answers");
assert.ok(Date.now() - t0 < 500, "a huge frame list is scanned within a bound");

// ---- framesFromSamples: dash-0's samples (t in ms, bots by guid: hp %, a 1/0) as the frames hpBefore reads
const samples = [
  { t: START - 2000, bots: { 7: { hp: 90, a: 1, dps: 5 } }, mobs: [] },
  { t: START + 2000, bots: { 7: { hp: 40, a: 1 }, 8: { hp: 100, a: 1 } }, mobs: [] },
  { t: START + 4000, bots: { 7: { hp: 0, a: 0 } }, mobs: [] },
];
const fr = W.framesFromSamples(samples, START);
eq(fr.frames.map((f) => f.t), [(START - 2000) / 1000, (START + 2000) / 1000, (START + 4000) / 1000], "seconds, like the replay's frames");
eq(fr.frames[1].bots, { 7: { hp: 40, alive: true }, 8: { hp: 100, alive: true } });
assert.strictEqual(fr.frames[2].bots[7].alive, false, "a = 0 is dead");
// three samples -> the expected percent: the death of bot 7 at 5000 ms (rel): the 4000 one is 1 s before and dead, the 2000 one counts
assert.strictEqual(W.hpBefore(fr, "Bot7", 5000, START, 7), 40, "framesFromSamples + hpBefore: 40 % (the dead 0 % sample is skipped)");
assert.strictEqual(W.hpBefore(fr, "Bot7", 2500, START, 7), 90, "the sample from before the pull is still read (within 10 s)");
assert.strictEqual(W.hpBefore(W.framesFromSamples([], START), "Bot7", 5000, START, 7), null, "no samples");
eq(W.framesFromSamples(null, START).frames, [], "no list");
const hostile = W.framesFromSamples([null, 3, { t: "x" }, { t: 5, bots: 7 }, { t: START, bots: JSON.parse('{"__proto__": {"hp": 1}, "3": null, "4": {"hp": "x"}, "5": {"hp": 50, "a": 1}}') }], START);
assert.ok(Array.isArray(hostile.frames) && hostile.frames.length <= 1, "hostile samples give at most the one good frame");
assert.strictEqual(hostile.frames.length ? hostile.frames[0].bots[5].hp : 50, 50, "the good row is read");
assert.ok(!hostile.frames.length || (hostile.frames[0].bots[3] === undefined && hostile.frames[0].bots[4] === undefined), "rows without a number are dropped");
assert.strictEqual(({}).hp, undefined, "no prototype was polluted");
const many = Array.from({ length: 50000 }, (_, i) => ({ t: START + i, bots: { 7: { hp: 1, a: 1 } } }));
assert.ok(W.framesFromSamples(many, START).frames.length <= 2000, "the frames made from a huge list are bounded");

// ---- startMs: the fight's own start_ms when it has one, else the whole-second start
assert.strictEqual(W.startMs({ start: 1000, start_ms: 1000450 }), 1000450, "start_ms wins");
assert.strictEqual(W.startMs({ start: 1000 }), 1000000, "else start * 1000");
assert.strictEqual(W.startMs({ start: 1000, start_ms: 0 }), 1000000, "a zero start_ms is none");
assert.strictEqual(W.startMs({ start: 1000, start_ms: "x" }), 1000000);
assert.strictEqual(W.startMs({}), null);
assert.strictEqual(W.startMs(null), null);

// ---- signature: what the box would draw
{
  const f = { outcome: "live", live: true, bots: botsOf(20), deaths: deathsOf(10), log: [] };
  const sig = W.signature(f);
  assert.strictEqual(W.signature(JSON.parse(JSON.stringify(f))), sig, "equal content, equal signature");
  assert.notStrictEqual(W.signature(Object.assign({}, f, { deaths: f.deaths.slice(0, 9) })), sig, "not a wipe any more");
  const d2 = f.deaths.map((d, i) => (i === 1 ? Object.assign({}, d, { name: "Bot20" }) : d));
  assert.notStrictEqual(W.signature(Object.assign({}, f, { deaths: d2 })), sig, "a death replaced, same count");
  const d3 = f.deaths.map((d, i) => (i === 2 ? Object.assign({}, d, { killer: "Other" }) : d));
  assert.notStrictEqual(W.signature(Object.assign({}, f, { deaths: d3 })), sig, "another killer");
  const d4 = f.deaths.map((d, i) => (i === 0 ? Object.assign({}, d, { t_ms: d.t_ms + 1 }) : d));
  assert.notStrictEqual(W.signature(Object.assign({}, f, { deaths: d4 })), sig, "another time");
  const b2 = f.bots.map((b) => (b.name === "Bot2" ? Object.assign({}, b, { role: "tank" }) : b));
  assert.notStrictEqual(W.signature(Object.assign({}, f, { bots: b2 })), sig, "a role changed, same roster size");
  const slid = f.deaths.slice(1).concat([{ name: "Bot19", t_ms: 99000, killer: "Boss" }]);
  assert.notStrictEqual(W.signature(Object.assign({}, f, { deaths: slid })), sig, "the oldest dropped as a new one arrives, same count");
  const later = f.deaths.concat([{ name: "Bot20", t_ms: 99000, killer: "Boss" }]);
  assert.strictEqual(W.signature(Object.assign({}, f, { deaths: later })), sig, "an eleventh death changes nothing the box draws");
  assert.notStrictEqual(W.signature(Object.assign({}, f, { outcome: "wipe" })), sig, "the outcome");
  const cast = [{ k: "cast", t: f.deaths[0].t_ms - 500, n: "Boss", s: "X" }];
  assert.notStrictEqual(W.signature(Object.assign({}, f, { log: cast })), sig, "a boss cast just before a shown death");
  assert.strictEqual(W.signature(Object.assign({}, f, { log: [{ k: "cast", t: 99999999, n: "Boss" }] })), sig, "a cast far from every shown death changes nothing");
  [null, 5, "x", {}, { bots: "x", deaths: [null, 3] }, { outcome: "wipe", deaths: [null, { name: 4 }], log: [5, null] }].forEach((x) => assert.strictEqual(typeof W.signature(x), "string"));
}

// ---- the clock text
assert.strictEqual(W.clock(0), "0:00");
assert.strictEqual(W.clock(65000), "1:05");
assert.strictEqual(W.clock(-5), "0:00");
assert.strictEqual(W.clock(NaN), "0:00");
console.log("wiperecap_check: ok");
