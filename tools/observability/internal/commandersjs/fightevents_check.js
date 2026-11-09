"use strict";
// Run by commandersjs_test.go under node: FightEvents (web/static/js/fightevents.js), the one normalised, time-ordered event list of
// a fight: eight kinds from the log, the aura rows, the boss's victim rows and each death's last hits; roles looked up from the
// fight's bots; malformed rows skipped; the 5000 cap (a filed fight keeps the first events, a running fight the newest); the value
// lists; and the running fight object of FdLive (no victims, deaths without hits) builds without target or hit events.
// args: fightevents.js, then fdlive.js.
const assert = require("assert");
const path = require("path");
const E = require(path.resolve(process.argv[2]));
const FdLive = require(path.resolve(process.argv[3]));

const SIPHON = 24324, POISON = 24321;
const bots = [{ name: "Tanky", guid: 11, role: "tank" }, { name: "Healy", guid: 12, role: "healer" }, { name: "Dpsy", guid: 13, role: "dps" }];
const fixture = () => ({
  boss_name: "Hakkar", dur_ms: 60000, bots,
  log: [
    { k: "cast", t: 5000, n: "Hakkar", e: 14834, s: "Blood Siphon", sid: SIPHON, tg: "12", tn: "Healy" },
    { k: "taunt", t: 3000, g: 11, n: "Tanky", s: "Taunt", tn: "Hakkar" },
    { k: "interrupt", t: 7000, g: 13, n: "Dpsy", s: "Kick", tn: "Hakkar" },
    { k: "death", t: 40000, g: 13, n: "Dpsy", by: "Hakkar" },
  ],
  auras: [[10000, 11, POISON, 1], [20000, 11, POISON, -1]],
  victims: [[1000, 11, "Tanky", "tank"], [2000, 13, "Dpsy", "none"]],
  deaths: [{ name: "Dpsy", t_ms: 40000, hits: [{ a: "Hakkar", d: 1500, t_ms: 39000 }] }],
});

// one of each source: eight kinds, sorted by time
{
  const { events, cut } = E.build(fixture());
  assert.strictEqual(cut, false);
  assert.deepStrictEqual(Array.from(new Set(events.map((e) => e.k))).sort(), E.KINDS.map((k) => k.k).sort(), "all eight kinds");
  assert.strictEqual(E.KINDS.length, 8);
  assert.strictEqual(events.length, 9);
  for (let i = 1; i < events.length; i++) assert.ok(events[i - 1].t <= events[i].t, "sorted by t");
  const taunt = events.find((e) => e.k === "taunt");
  assert.strictEqual(taunt.srcRole, "tank", "a taunt by a tank gets srcRole tank");
  assert.strictEqual(taunt.srcG, 11);
  const cast = events.find((e) => e.k === "cast");
  assert.deepStrictEqual([cast.src, cast.srcRole, cast.ab, cast.sid, cast.tgt, cast.tgtG, cast.tgtRole], ["Hakkar", "enemy", "Blood Siphon", SIPHON, "Healy", 12, "healer"], "a cast: the boss is the enemy, its target is a bot by guid");
  const strangerTarget = E.build({ bots, log: [{ k: "cast", t: 1, n: "B", s: "x", tg: "999", tn: "Somebody" }] }).events[0];
  assert.deepStrictEqual([strangerTarget.tgt, strangerTarget.tgtG, strangerTarget.tgtRole], ["Somebody", null, ""], "a target guid that is no bot: named, no guid, no role");
  const death = events.find((e) => e.k === "death");
  assert.deepStrictEqual([death.src, death.tgt, death.tgtG, death.tgtRole], ["Hakkar", "Dpsy", 13, "dps"]);
  const on = events.find((e) => e.k === "aura_on");
  assert.deepStrictEqual([on.tgt, on.tgtG, on.sid, on.ab, on.tgtRole], ["Tanky", 11, POISON, "Poisonous Blood", "tank"]);
  assert.strictEqual(events.find((e) => e.k === "aura_off").t, 20000);
  const tgt = events.filter((e) => e.k === "target");
  assert.deepStrictEqual([tgt[0].src, tgt[0].srcRole, tgt[0].tgt, tgt[0].tgtRole, tgt[1].tgtRole], ["Hakkar", "enemy", "Tanky", "tank", ""], "a victim row: the boss targets a bot; role none is empty");
  const hit = events.find((e) => e.k === "hit");
  assert.deepStrictEqual([hit.t, hit.src, hit.tgt, hit.amt, hit.tgtRole], [39000, "Hakkar", "Dpsy", 1500, "dps"], "a last hit before a death");
  assert.strictEqual(E.build({ log: [{ k: "death", t: 1, n: "x", by: "y" }], victims: [[1, 5, "x", "tank"]] }).events[1].src, "boss", "no boss name: boss");
}
// the boss lane also draws "ab" log events (boss abilities): they are listed as casts, so a mark and its row agree
{
  const { events } = E.build({ log: [{ k: "ab", t: 1000, n: "Boss", e: 5, s: "Slam", sid: 9, tn: "Tanky" }, { k: "mystery", t: 2000 }] });
  assert.deepStrictEqual(events.map((e) => [e.k, e.src, e.srcRole, e.ab, e.sid, e.tgt]), [["cast", "Boss", "enemy", "Slam", 9, "Tanky"]], "an ab event is a cast; an unknown kind is still skipped");
}
// an aura row for a bot that is not in the fight: "#<guid>"; names the live view learned are used
{
  const { events } = E.build({ bots, auras: [[100, 999, 555, 1]] }, { names: { 555: "Learned" } });
  assert.deepStrictEqual([events[0].tgt, events[0].tgtG, events[0].tgtRole, events[0].ab], ["#999", 999, "", "Learned"]);
  assert.strictEqual(E.build({ auras: [[100, 999, 555, 1]] }).events[0].ab, "spell 555");
}
// malformed rows are skipped, the rest survive
{
  const f = {
    bots,
    log: [null, "x", { k: "cast", t: NaN, s: "a" }, { k: "cast", t: "5", s: "b" }, { k: "mystery", t: 1 }, { k: "cast", t: 10, n: "B", s: "good" }],
    auras: [[1, 2, 3], "x", [1, 2, 3, 0], [1, 11, 5, 2], [NaN, 11, 5, 1], [30, 11, 5, -1]],
    victims: [[1, 2], null, [NaN, 1, "n", "tank"], [20, 11, "Tanky", "tank"]],
    deaths: [null, { name: "x" }, { name: "Dpsy", hits: [null, { a: "b", d: "9", t_ms: 5 }, { a: "b", d: 9, t_ms: NaN }, { a: "b", d: 9, t_ms: 25 }] }],
  };
  const { events } = E.build(f);
  assert.deepStrictEqual(events.map((e) => e.k + "@" + e.t), ["cast@10", "target@20", "hit@25", "aura_off@30"], "only the well-formed rows");
  assert.deepStrictEqual(E.build(null), { events: [], cut: false });
  assert.deepStrictEqual(E.build({}), { events: [], cut: false });
}
// the cap: 6000 rows give 5000 and cut; a filed fight keeps the first, a running fight the newest
{
  const log = Array.from({ length: 6000 }, (_, i) => ({ k: "cast", t: i, n: "B", s: "s" + i }));
  const r = E.build({ log });
  assert.strictEqual(r.events.length, 5000);
  assert.strictEqual(r.cut, true);
  assert.strictEqual(r.events[0].t, 0);
  assert.strictEqual(r.events[4999].t, 4999, "the first 5000 of a filed fight");
  const lv = E.build({ log, live: true });
  assert.strictEqual(lv.events.length, 5000);
  assert.strictEqual(lv.events[4999].t, 5999, "a running fight keeps the newest, the ones that matter");
  assert.strictEqual(E.build({ log: log.slice(0, 5000) }).cut, false, "exactly 5000 is not cut");
}
// equal times keep the order the sources were read in (a stable sort)
{
  const { events } = E.build({ bots, log: [{ k: "cast", t: 5, n: "x", s: "first" }, { k: "cast", t: 5, n: "x", s: "second" }], auras: [[5, 11, 1, 1]] });
  assert.deepStrictEqual(events.map((e) => e.ab), ["first", "second", "spell 1"]);
}
// distinct: sorted, unique, non-empty; by filter field or event property
{
  const { events } = E.build(fixture());
  assert.deepStrictEqual(E.distinct(events, "source"), ["Dpsy", "Hakkar", "Tanky"], "source: sorted unique");
  assert.deepStrictEqual(E.distinct(events, "src"), ["Dpsy", "Hakkar", "Tanky"], "the property name works too");
  assert.deepStrictEqual(E.distinct(events, "kind"), E.KINDS.map((k) => k.k).sort());
  assert.deepStrictEqual(E.distinct(events, "srcRole"), ["dps", "enemy", "tank"]);
  assert.deepStrictEqual(E.distinct(null, "source"), []);
}
// LIVE: the running fight object FdLive draws (log with casts, taunts, interrupts and deaths; sampled aura rows; deaths without
// hits; no victims) builds without target or hit events, and with the learned aura names
{
  const st = FdLive.create("Cmd");
  FdLive.applyEventsFrame(st, { key: "K\u0000s\u0000f", from: 0, cur: 4, fight: { start_ms: 1000, boss: 14834, boss_name: "Hakkar", fight_ms: 9000 }, events: [
    { k: "cast", t: 5000, n: "Hakkar", e: 14834, s: "Blood Siphon", sid: SIPHON, tg: 12, tn: "Healy" },
    { k: "taunt", t: 3000, g: 11, n: "Tanky", s: "Taunt", tn: "Hakkar" },
    { k: "death", t: 8000, g: 13, n: "Dpsy", by: "Hakkar", x: 1, y: 2 },
    { k: "engage", t: 100, e: 14834, c: 1 },
  ] }, 1000);
  const mkBind = (db) => ({ bots: bots.map((b) => ({ name: b.name, guid: b.guid, role: b.role, hp: 100, max_hp: 100, alive: true, db })), combat: { win_s: 10, bots: [] } });
  FdLive.applySample(st, mkBind([]), 1000 + 2000);
  FdLive.applySample(st, mkBind([{ id: POISON, n: "Poisonous Blood" }, { id: 777, n: "Learned Debuff" }]), 1000 + 4000);
  const lf = FdLive.fight(st, mkBind([]), 20000);
  assert.strictEqual(lf.live, true);
  assert.ok(lf.deaths.length === 1 && lf.deaths[0].hits.length === 0, "the live death has no hits");
  const { events, cut } = E.build(lf, { names: st.auraNames });
  assert.strictEqual(cut, false);
  assert.ok(!events.some((e) => e.k === "target" || e.k === "hit"), "live has no target changes and no last hits: " + JSON.stringify(events.map((e) => e.k)));
  assert.deepStrictEqual(Array.from(new Set(events.map((e) => e.k))).sort(), ["aura_on", "cast", "death", "taunt"]);
  assert.ok(events.some((e) => e.k === "aura_on" && e.ab === "Learned Debuff" && e.tgt === "Tanky"), "sampled aura rows become aura events with the learned name");
  assert.strictEqual(events.find((e) => e.k === "death").tgtRole, "dps");
  // no bind: no roles, aura rows name the guid
  const nb = E.build(FdLive.fight(st, null, 20000)).events;
  assert.ok(nb.every((e) => e.k !== "aura_on" || /^#/.test(e.tgt)), "without bots an aura row names the guid");
}
console.log("fightevents_check.js ok");
