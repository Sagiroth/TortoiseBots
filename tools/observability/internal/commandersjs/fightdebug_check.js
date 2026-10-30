"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's pure logic (web/static/js/fightdebug.js).
// The fixture is walk 2 (Onyxia's Lair, commander Glacienne), newest fight first like GET /api/v1/combat/fights.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const F = require(path.resolve(process.argv[2]));

const fights = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-fights.json"), "utf8"));
const byId = (id) => fights.find((f) => f.id === id);
const detail = (id, warnings) => ({ fight: byId(id), warnings: warnings || [] });
const LAYOUT = { mt: "Ragemeister", ot: ["Brun", "Irelanthe"] };

// ---- exports and small helpers -------------------------------------------------------------------------------------
assert.deepStrictEqual(F.DEBUFF_CHECKS.map((d) => d.key), ["faerie_fire", "coe", "expose_armor"]);
assert.deepStrictEqual(F.DEBUFF_CHECKS.map((d) => d.name), ["Faerie Fire", "Curse of the Elements", "Expose Armor"]);
assert.deepStrictEqual(F.TAUNTS, ["Taunt", "Growl", "Hand of Reckoning", "Mocking Blow", "Challenging Shout", "Challenging Roar"]);
assert.deepStrictEqual(F.LIMITS, { sunderOk: 1.5, sunderWarn: 0.8, sunderFail: 0.4, sunderFailMinMs: 30000,
  idleCastsPerSec: 0.3, idleMinMs: 20000, takenShare: 0.15, overheal: 0.45, debuffOk: 0.6, minFightMs: 10000,
  shortPullMs: 30000 });
assert.strictEqual(F.fmtInt(175083), "175,083");
assert.strictEqual(F.fmtInt(0), "0");
assert.strictEqual(F.fmtInt(999), "999");
assert.strictEqual(F.roleOf({ role: "tank" }), "tank");
assert.strictEqual(F.roleOf({ role: "healer" }), "heal");
assert.strictEqual(F.roleOf({ role: "dps" }), "dps");
assert.strictEqual(F.spellCount({ spells: [{ n: "Taunt", c: 2 }, { n: "Growl", c: 3 }, { n: "Taunt", c: 1 }] }, "Taunt"), 3);
assert.strictEqual(F.spellCount(null, "Taunt"), 0);

// ---- main tank and off-tanks: the layout, else the fight's own tanks -----------------------------------------------
assert.strictEqual(F.mainTank(byId("5-4"), LAYOUT).name, "Ragemeister");
assert.deepStrictEqual(F.offTanks(byId("5-4"), LAYOUT).map((b) => b.name), ["Brun", "Irelanthe"]);
const noLayoutFight = {
  bots: [
    { name: "DPS", role: "dps" },
    { name: "TB", role: "tank" },
    { name: "TA", role: "tank" },
  ],
};
assert.strictEqual(F.mainTank(noLayoutFight, null).name, "TB");
assert.deepStrictEqual(F.offTanks(noLayoutFight, null).map((b) => b.name), ["TA"]);
// No layout: the tank the mobs hit most is the main tank, not the first in the list. A layout MT still wins.
const takenFight = {
  bots: [
    { name: "TA", role: "tank", taken: 100 },
    { name: "TB", role: "tank", taken: 900 },
    { name: "D", role: "dps", taken: 5000 },
  ],
};
assert.strictEqual(F.mainTank(takenFight, null).name, "TB");
assert.strictEqual(F.mainTank(takenFight, { mt: "TA", ot: [] }).name, "TA");

// ---- the checks of fight 5-4 --------------------------------------------------------------------------------------
const c54 = F.checks(byId("5-4"), LAYOUT, []);
const one = (id) => c54.find((c) => c.id === id);
assert.strictEqual(c54.length, 10);
assert.deepStrictEqual(c54.map((c) => c.id),
  ["tank_death", "debuff:coe", "debuff:expose_armor", "mt_sunder", "taken_outside", "overheal", "idle",
    "debuff:faerie_fire", "ot_taunt:Brun", "ot_taunt:Irelanthe"]);
assert.deepStrictEqual(c54.map((c) => c.lvl), ["fail", "warn", "warn", "ok", "ok", "ok", "ok", "ok", "ok", "ok"]);

assert.strictEqual(one("mt_sunder").title, "Main tank Sunder rate");
assert.strictEqual(one("mt_sunder").num, "2.0/10 s");
assert.strictEqual(one("mt_sunder").lvl, "ok");
assert.strictEqual(one("mt_sunder").ev,
  "Ragemeister cast Sunder Armor 8× in 39 s (Shield Slam 4×). Under 0.8 per 10 s the stack never reaches 5.");

assert.strictEqual(one("tank_death").title, "Tank died: Irelanthe (2×)");
assert.strictEqual(one("tank_death").num, 2);
assert.strictEqual(one("tank_death").death, 0);
assert.strictEqual(one("tank_death").bot, "Irelanthe");
assert.strictEqual(one("tank_death").ev, "Irelanthe at 12.0 s to Onyxian Warder · Irelanthe at 25.1 s to Onyxian Warder");
assert.strictEqual(one("other_death"), undefined);

assert.strictEqual(one("taken_outside").title, "Most damage taken outside the tanks: Kitetastic (damage)");
assert.strictEqual(one("taken_outside").num, "6%");
assert.strictEqual(one("taken_outside").lvl, "ok");
assert.strictEqual(one("taken_outside").bot, "Kitetastic");
assert.strictEqual(one("taken_outside").ev,
  "Tanks took 21,907; Kitetastic took 9,730 of 175,083. Over 15% on one non-tank usually means threat was lost.");

assert.strictEqual(one("overheal").title, "Healer overheal");
assert.strictEqual(one("overheal").num, "24%");
assert.strictEqual(one("overheal").lvl, "ok");
assert.strictEqual(one("overheal").ev, "103,742 effective, 32,478 overheal across 10 healers.");

assert.strictEqual(one("idle").title, "Every bot acted");
assert.strictEqual(one("idle").num, ""); // no number to show when nobody idled
assert.strictEqual(one("idle").lvl, "ok");
assert.strictEqual(one("idle").ev, "All 39 bots cast, hit or healed.");

assert.strictEqual(one("debuff:coe").title, "Curse of the Elements up");
assert.strictEqual(one("debuff:coe").num, "57%");
assert.strictEqual(one("debuff:coe").lvl, "warn");
assert.strictEqual(one("debuff:coe").ev, "24 of 42 sampled seconds.");
assert.strictEqual(one("debuff:expose_armor").title, "Expose Armor up");
assert.strictEqual(one("debuff:expose_armor").num, "45%");
assert.strictEqual(one("debuff:expose_armor").lvl, "warn");
assert.strictEqual(one("debuff:faerie_fire").title, "Faerie Fire up");
assert.strictEqual(one("debuff:faerie_fire").num, "62%");
assert.strictEqual(one("debuff:faerie_fire").lvl, "ok");

assert.strictEqual(one("ot_taunt:Brun").title, "Off-tank Brun taunted");
assert.strictEqual(one("ot_taunt:Brun").num, "2×");
assert.strictEqual(one("ot_taunt:Brun").lvl, "ok");
assert.strictEqual(one("ot_taunt:Brun").ev, "paladin: Hand of Reckoning 2; took 6,500.");
assert.strictEqual(one("ot_taunt:Irelanthe").num, "3×");
assert.strictEqual(one("ot_taunt:Irelanthe").lvl, "ok");
assert.strictEqual(one("ot_taunt:Irelanthe").ev, "druid: Growl 3; took 10,879.");

// 5-5: Expose Armor exactly 50 % (warn); 5-3: a weak Sunder, CoE 35 %, Irelanthe never taunted.
assert.strictEqual(F.checks(byId("5-5"), LAYOUT, []).find((c) => c.id === "debuff:expose_armor").num, "50%");
const c53 = F.checks(byId("5-3"), LAYOUT, []);
assert.strictEqual(c53.find((c) => c.id === "mt_sunder").num, "1.3/10 s");
assert.strictEqual(c53.find((c) => c.id === "mt_sunder").lvl, "warn");
assert.strictEqual(c53.find((c) => c.id === "debuff:coe").num, "35%");
// 5-2 Faerie Fire is exactly 24/40 = 0.60: the limit is inclusive.
assert.strictEqual(F.checks(byId("5-2"), LAYOUT, []).find((c) => c.id === "debuff:faerie_fire").lvl, "ok");
assert.strictEqual(c53.find((c) => c.id === "ot_taunt:Irelanthe").num, "0×");
assert.strictEqual(c53.find((c) => c.id === "ot_taunt:Irelanthe").lvl, "warn");
assert.strictEqual(c53.find((c) => c.id === "ot_taunt:Irelanthe").ev, "druid: no taunt; took 13,457.");

// ---- verdicts, worst pick -----------------------------------------------------------------------------------------
const details = fights.map((f) => ({ fight: f, warnings: [] }));
assert.deepStrictEqual(details.map((d) => F.verdict(d.fight, LAYOUT, [])), ["warn", "fail", "fail", "warn", "ok"]);
assert.strictEqual(F.verdict({ bots: [], deaths: [], debuffs: {}, dur_ms: 1 }, null, []), "ok");
assert.strictEqual(F.worstIndex(details, LAYOUT), 2); // 5-3: 1 fail, 3 warns
assert.strictEqual(F.worstIndex([null, null], LAYOUT), -1);
const partial = details.slice();
partial[1] = null;
const worstPartial = F.worstIndex(partial, LAYOUT);
assert.notStrictEqual(worstPartial, 1);
assert.strictEqual(F.verdict(details[worstPartial].fight, LAYOUT, []), "fail");
// Equal scores keep the earliest (newest) fight.
assert.strictEqual(F.worstIndex([detail("5-5"), detail("5-5")], LAYOUT), 0);

// ---- death recaps: a second death never repeats the hits of the first ---------------------------------------------
const recaps = F.deathRecaps(byId("5-4"));
assert.strictEqual(recaps.length, 2);
assert.strictEqual(recaps[0].total, 6422);
assert.strictEqual(recaps[0].spanMs, 5016);
assert.strictEqual(recaps[0].hits.length, 5);
assert.strictEqual(recaps[0].name, "Irelanthe");
assert.strictEqual(recaps[0].guid, 814); // the fight's bots carry the guid its profile opens
assert.strictEqual(recaps[0].killer, "Onyxian Warder");
assert.strictEqual(recaps[0].x, -163);
assert.strictEqual(recaps[1].hits.length, 1);
assert.strictEqual(recaps[1].hits[0].d, 3091);
assert.strictEqual(recaps[1].spanMs, 0);
assert.deepStrictEqual(F.deathRecaps({ deaths: [] }), []);

// ---- pack numbers count inside a day and a run, not across the whole list ------------------------------------------
const secAt = (y, m, d, hh, mm) => Math.floor(Date.UTC(y, m - 1, d, hh, mm, 0) / 1000);
const packRows = [
  { key: "new-2", session: 9, start: secAt(2026, 10, 29, 23, 40) },
  { key: "new-1", session: 9, start: secAt(2026, 10, 29, 23, 10) },
  { key: "old-3", session: 4, start: secAt(2026, 10, 29, 22, 0) },
  { key: "old-2", session: 4, start: secAt(2026, 10, 29, 21, 30) },
  { key: "old-1", session: 4, start: secAt(2026, 10, 29, 21, 0) },
  { key: "prev-night", session: 4, start: secAt(2026, 10, 28, 23, 0) },
];
assert.deepStrictEqual(F.packNumbers(packRows), [2, 1, 3, 2, 1, 1],
  "a new run (session) restarts at Pack 1, and so does a new UTC day");
assert.deepStrictEqual(F.packNumbers([]), []);
assert.deepStrictEqual(F.packNumbers([{ key: "a" }, { key: "b" }]), [2, 1], "rows without a session still count per day");
// The rail reads the same numbers: two runs on one night do not continue each other's count.
const multiRail = F.railCards(packRows, {}, null, null);
assert.deepStrictEqual(multiRail.map((c) => c.label), ["Pack 2", "Pack 1", "Pack 3", "Pack 2", "Pack 1", "Pack 1"],
  "the rail's Pack N restarts with the run and the day");

// ---- the killer: the last hit that is not the dead bot; only self-damage known is "unknown" ------------------------
const SELF_FIGHT = {
  bots: [{ name: "Benedwin", guid: 7, class: "druid", role: "dps", dmg: 0, taken: 900, casts: 2, spells: [] }],
  deaths: [{
    name: "Benedwin", t_ms: 40200, x: 1, y: 2, killer: "Benedwin",
    hits: [
      { t_ms: 40000, a: "Benedwin", d: 120 },  // its own spell, the newest event
      { t_ms: 39800, a: "Onyxian Warder", d: 820 },
      { t_ms: 39000, a: "Benedwin", d: 90 },
    ],
  }],
};
assert.strictEqual(F.deathRecaps(SELF_FIGHT)[0].killer, "Onyxian Warder", "the last hit from another unit names the killer");
assert.strictEqual(F.killerOf(SELF_FIGHT.deaths[0]), "Onyxian Warder");
const selfOnly = { name: "Kild", t_ms: 59200, killer: "Kild", hits: [{ t_ms: 59000, a: "Kild", d: 44 }, { t_ms: 59100, a: "Kild", d: 40 }] };
assert.strictEqual(F.killerOf(selfOnly), "unknown", "only self-damage known is unknown, never the victim");
const noHits = { name: "Erelidael", t_ms: 30000, killer: "Erelidael", hits: [] };
assert.strictEqual(F.killerOf(noHits), "unknown");
assert.strictEqual(F.killerOf({ name: "A", t_ms: 1, killer: "Onyxia", hits: [] }), "Onyxia", "the module's killer stands when it is not the victim");
// The hits are stored newest-first: the killer is the non-self hit with the greatest t_ms, whatever the array order.
const newestFirst = {
  name: "V", t_ms: 20000, killer: "OldMob",
  hits: [
    { t_ms: 19000, a: "NewMob", d: 500 },
    { t_ms: 15000, a: "OldMob", d: 900 },
    { t_ms: 18000, a: "V", d: 50 },
  ],
};
assert.strictEqual(F.killerOf(newestFirst), "NewMob", "a backward scan would return the oldest attacker");
assert.strictEqual(F.killerOf({ name: "V", t_ms: 20000, killer: "x", hits: newestFirst.hits.slice().reverse() }), "NewMob",
  "array order never decides");
// The checks' death line reads the same killer.
const selfChecks = F.checks(SELF_FIGHT, null, [], null);
const deathLine = selfChecks.find((c) => c.id === "other_death");
assert.ok(deathLine && deathLine.ev.includes("to Onyxian Warder"), deathLine && deathLine.ev);
assert.ok(!deathLine.ev.includes("to Benedwin"), "the dead bot is never its own killer: " + deathLine.ev);

// ---- timeline -----------------------------------------------------------------------------------------------------
const tl = F.timeline(byId("5-4"));
assert.strictEqual(tl.durMs, 39111);
assert.deepStrictEqual(tl.ticks.map((t) => t.label), ["0 s", "5 s", "10 s", "15 s", "20 s", "25 s", "30 s", "35 s"]);
assert.strictEqual(tl.ticks.length, 8);
assert.strictEqual(tl.deaths[0].pct.toFixed(2), "30.78");
assert.strictEqual(tl.deaths[1].pct.toFixed(2), "64.16");
assert.strictEqual(tl.hits.length, 6);
assert.strictEqual(tl.hits[5].d, 3091);
assert.strictEqual(tl.hits[5].h, 44);
assert.ok(tl.hits.every((h) => h.pct >= 0 && h.pct <= 100));

// ---- role tables ----------------------------------------------------------------------------------------------------
const rt = F.roleTables(byId("5-4"), LAYOUT);
assert.deepStrictEqual(rt.tanks.map((r) => r.name), ["Ragemeister", "Brun", "Irelanthe"]);
assert.strictEqual(rt.tanks[0].main, true);
assert.strictEqual(rt.tanks[1].main, false);
assert.strictEqual(rt.tanks[0].sunder, 8);
assert.strictEqual(rt.tanks[0].taunts, 1);
assert.strictEqual(rt.tanks[2].died, 2);
assert.strictEqual(rt.tanks[2].taken, 10879);
assert.strictEqual(rt.tanks[0].cls, "warrior");
assert.strictEqual(rt.tanks[0].guid, 813);
assert.strictEqual(rt.healers[0].guid, 820);
assert.strictEqual(rt.dps[0].guid, 828);
assert.strictEqual(rt.healers.length, 10);
assert.strictEqual(rt.healers[0].name, "Magnarr");
assert.ok(Math.abs(rt.healers[0].hps - 25134 / (39111 / 1000)) < 1e-9);
assert.strictEqual(rt.healers[0].overPct, Math.round(rt.healers[0].overPct));
assert.strictEqual(rt.maxHps, rt.healers[0].hps);
assert.strictEqual(rt.dps.length, 26);
assert.strictEqual(rt.dps[0].name, "Dawnfist");
assert.strictEqual(rt.dps[0].dps, 15120 / (39111 / 1000));
assert.strictEqual(rt.maxDps, rt.dps[0].dps);
assert.ok(rt.dps.every((r, i) => i === 0 || rt.dps[i - 1].dps >= r.dps));
const laidOut = {
  bots: [
    { name: "TankA", role: "tank", class: "warrior", taken: 10 },
    { name: "TankB", role: "tank", class: "warrior", taken: 10 },
    { name: "HealA", role: "healer", class: "priest", heal: 10, over: 10, casts: 1 },
    { name: "HealB", role: "healer", class: "priest", heal: 10, over: 10, casts: 1 },
  ],
  dur_ms: 10000,
};
const laid = F.roleTables(laidOut, { mt: "TankB", ot: ["TankA"] });
assert.deepStrictEqual(laid.tanks.map((r) => r.name), ["TankB", "TankA"]);
assert.deepStrictEqual(laid.healers.map((r) => r.name), ["HealA", "HealB"]);
assert.deepStrictEqual(F.roleTables({ dur_ms: 0, bots: [] }, null), { tanks: [], healers: [], dps: [], maxHps: 0, maxDps: 0 });

// ---- one bot --------------------------------------------------------------------------------------------------------
const bp = F.botPanel(byId("5-4"), "Ragemeister");
assert.strictEqual(bp.name, "Ragemeister");
assert.strictEqual(bp.cls, "warrior");
assert.strictEqual(bp.sub, 0);
assert.strictEqual(bp.casts, 16);
assert.deepStrictEqual(bp.spells.map((s) => s.n),
  ["Sunder Armor", "Shield Slam", "Bloodrage", "Food", "Heroic Strike", "Taunt"]);
assert.strictEqual(F.botPanel(byId("5-4"), "Nobody"), null);

// ---- the fight rail -------------------------------------------------------------------------------------------------
const list = [
  { key: "k5", dur: 35123, deaths: 0, step: 5, boss_name: "" },
  { key: "k4", dur: 39111, deaths: 2, step: 22, boss_name: "" },
  { key: "k3", dur: 52477, deaths: 0, step: 9, boss_name: "" },
];
const dmap = { k5: detail("5-5"), k4: detail("5-4"), k3: detail("5-3") };
const cards = F.railCards(list, dmap, LAYOUT);
assert.deepStrictEqual(cards.map((c) => c.key), ["k5", "k4", "k3"]);
assert.deepStrictEqual(cards.map((c) => c.verdict), ["warn", "fail", "fail"]);
assert.strictEqual(cards[0].label, "Pack 3");
assert.strictEqual(cards[0].mob, "Onyxian Warder");
assert.strictEqual(cards[1].label, "Pack 2");
assert.strictEqual(cards[2].label, "Pack 1");
assert.strictEqual(cards[1].durMs, 39111);
assert.strictEqual(cards[1].deaths, 2);
assert.strictEqual(cards[1].step, 22);
assert.strictEqual(cards[0].lenPct, (35123 / 52477) * 100);
assert.strictEqual(cards[2].lenPct, 100);
const unloaded = F.railCards([list[0]], {}, LAYOUT);
assert.strictEqual(unloaded[0].verdict, null);
assert.strictEqual(unloaded[0].label, "Pack 1");
assert.strictEqual(unloaded[0].mob, "trash");
assert.strictEqual(unloaded[0].lenPct, 100);
assert.deepStrictEqual(F.railCards(null, {}, LAYOUT), []);

// ---- raid frames ----------------------------------------------------------------------------------------------------
const bind = {
  bots: [
    { name: "A", guid: 1, class: "warrior", role: "tank", sub: 0, hp: 50, max_hp: 100, power: 10, max_power: 100, power_type: "rage", alive: true },
    { name: "B", guid: 2, class: "priest", role: "healer", sub: 0, hp: 100, max_hp: 100, power: 100, max_power: 100, power_type: "mana", alive: true },
    { name: "C", guid: 3, class: "mage", role: "dps", sub: 1, hp: 0, max_hp: 0, alive: true },
    { name: "D", guid: 4, class: "rogue", role: "dps", sub: 7, hp: 20, max_hp: 40, power: 0, max_power: 100, power_type: "energy", alive: false },
    { name: "E", guid: 5, class: "hunter", role: "dps", sub: 9, hp: 10, max_hp: 10, alive: true },
    { name: "F", guid: 6, class: "shaman", role: "dps", hp: 1, max_hp: 2, alive: true },
  ],
  layout: { mt: "A", ot: ["D"] },
};
const frames = F.raidFrames(bind);
assert.deepStrictEqual(frames.map((f) => f.label), ["Group 1", "Group 2", "Group 8"]);
assert.deepStrictEqual(frames.map((f) => f.tiles.length), [2, 1, 3]);
assert.strictEqual(frames[0].tiles[0].hpPct, 50);
assert.strictEqual(frames[0].tiles[0].powerPct, 10);
assert.strictEqual(frames[0].tiles[0].powerKind, "rage");
assert.strictEqual(frames[0].tiles[0].tag, "MT");
assert.strictEqual(frames[0].tiles[0].alive, true);
assert.strictEqual(frames[0].tiles[0].role, "tank");
assert.strictEqual(frames[0].tiles[1].role, "healer");
assert.deepStrictEqual(frames[2].tiles.map((t) => t.name), ["D", "E", "F"]);
assert.strictEqual(frames[2].tiles[0].role, "dead");
assert.strictEqual(frames[2].tiles[0].tag, "OT");
assert.strictEqual(frames[2].tiles[0].hpPct, 50);
assert.strictEqual(frames[2].tiles[0].powerPct, 0);
assert.strictEqual(frames[2].tiles[0].powerKind, "energy");
assert.strictEqual(frames[2].tiles[1].tag, "");
assert.strictEqual(frames[1].tiles[0].hpPct, 0);
assert.strictEqual(frames[1].tiles[0].powerPct, 0);
assert.strictEqual(frames[1].tiles[0].powerKind, "mana");
assert.deepStrictEqual(F.raidFrames({ bots: [{ name: "A" }, { name: "B" }] }).map((f) => f.label), ["Party"]);
assert.deepStrictEqual(F.raidFrames({ bots: [] }), []);

// ---- live fight: summary, checks, tables -----------------------------------------------------------------------------
assert.strictEqual(F.liveSummary({}), null);
assert.strictEqual(F.liveSummary({ bots: [], combat: null }), null);
const liveBind = {
  bots: [{ name: "A", guid: 1, role: "tank", alive: true }, { name: "B", guid: 2, role: "healer", alive: false }],
  combat: { win_s: 10, fight_ms: 23000, bots: [{ g: 1, dps: 120, hps: 0, c: 3, ls: "Maul" }] },
};
assert.deepStrictEqual(F.liveSummary(liveBind), { fightMs: 23000, winS: 10, dps: 120, hps: 0, active: 1, alive: 1 });

assert.deepStrictEqual(F.liveChecks({}), []);
const early = {
  bots: [{ name: "A", guid: 1, role: "tank", alive: true }],
  combat: { win_s: 10, fight_ms: 5000, bots: [] },
};
assert.ok(!F.liveChecks(early).some((c) => c.id === "live_idle"));
const full = {
  bots: [{ name: "A", guid: 1, role: "tank", alive: true }, { name: "B", guid: 2, role: "healer", alive: true }],
  combat: { win_s: 10, fight_ms: 23000, bots: [{ g: 1, dps: 10, hps: 0, c: 2, ls: "x" }] },
};
const idleLive = F.liveChecks(full).find((c) => c.id === "live_idle");
assert.strictEqual(idleLive.lvl, "warn");
assert.ok(idleLive.ev.includes("B"));
assert.strictEqual(idleLive.bot, "B");
const busy = {
  bots: [{ name: "A", guid: 1, role: "tank", alive: true }],
  combat: { win_s: 10, fight_ms: 23000, bots: [{ g: 1, dps: 10, hps: 0, c: 2, ls: "x" }] },
};
assert.strictEqual(F.liveChecks(busy).find((c) => c.id === "live_idle").lvl, "ok");
assert.strictEqual(F.liveChecks(busy).find((c) => c.id === "live_idle").num, "");
const deadTank = {
  bots: [{ name: "MT", guid: 1, role: "tank", alive: false }, { name: "B", guid: 2, role: "healer", alive: true }],
  layout: { mt: "MT", ot: [] },
  combat: { win_s: 10, fight_ms: 23000, bots: [{ g: 2, dps: 0, hps: 9, c: 1, ls: "Heal" }] },
};
const tankDown = F.liveChecks(deadTank).find((c) => c.id === "live_tank_dead");
assert.strictEqual(tankDown.lvl, "fail");
assert.ok(tankDown.title.includes("MT"));
assert.strictEqual(tankDown.bot, "MT");

const liveRows = F.liveTables(full);
assert.deepStrictEqual(liveRows.map((r) => r.name), ["A", "B"]);
assert.strictEqual(liveRows[0].dps, 10);
assert.strictEqual(liveRows[0].casts, 2);
assert.strictEqual(liveRows[0].last, "x");
assert.strictEqual(liveRows[0].role, "tank");
assert.strictEqual(liveRows[1].dps, 0);
assert.strictEqual(liveRows[1].casts, 0);
assert.strictEqual(liveRows[1].last, "");

// ---- a synthetic fight: a warrior main tank over- or under-taunting, and the guards -------------------------------
const fight = (over) => Object.assign({
  dur_ms: 30000, bots: [], debuffs: { sampled_s: 0, up_s: {} }, deaths: [], engaged: [], boss_name: "",
}, over);
const bot = (over) => Object.assign({
  name: "B", guid: 1, class: "warrior", role: "dps", dmg: 100, heal: 0, over: 0, taken: 10, casts: 2, deaths: 0, spells: [],
}, over);
const warriorMT = (n, dur) => fight({
  dur_ms: dur,
  bots: [bot({ name: "T", role: "tank", taken: 100, spells: [{ n: "Sunder Armor", c: n }] })],
});
const MTLAYOUT = { mt: "T", ot: [] };
const sunderOf = (f) => F.checks(f, MTLAYOUT, []).find((c) => c.id === "mt_sunder");
// Below 0.4 per 10 s is a problem only in a fight of 30 s or more; 0.4-0.8 is a warn at any length.
assert.strictEqual(sunderOf(warriorMT(1, 30000)).lvl, "fail");
assert.strictEqual(sunderOf(warriorMT(1, 30000)).num, "0.3/10 s");
assert.strictEqual(sunderOf(warriorMT(3, 100000)).lvl, "fail"); // exactly 0.3/10 s
assert.strictEqual(sunderOf(warriorMT(2, 30000)).lvl, "warn");
assert.strictEqual(sunderOf(warriorMT(2, 30000)).num, "0.7/10 s");
assert.strictEqual(sunderOf(warriorMT(3, 30000)).lvl, "warn");
assert.strictEqual(sunderOf(warriorMT(3, 30000)).num, "1.0/10 s");
assert.strictEqual(sunderOf(warriorMT(0, 25000)).lvl, "warn"); // a 25 s pull cannot reach 0.4
assert.strictEqual(sunderOf(warriorMT(0, 30000)).lvl, "fail");
assert.strictEqual(sunderOf(warriorMT(2, 9000)), undefined);
assert.strictEqual(sunderOf(fight({ dur_ms: 30000, bots: [bot({ name: "T", role: "tank", class: "druid" })] })), undefined);
assert.strictEqual(sunderOf(fight({ dur_ms: 30000, bots: [bot({ name: "T", role: "dps", class: "warrior" })] })), undefined);

// The main tank's own activity: under 0.3 casts per second over 20 s or more is a problem, whatever it cast.
const idleT = (casts, dur) => fight({ dur_ms: dur, bots: [bot({
  name: "T", role: "tank", taken: 100, casts,
  spells: casts === 2 ? [{ n: "Sunder Armor", c: 1 }, { n: "Taunt", c: 1 }] : [{ n: "Sunder Armor", c: casts }],
})]});
const idleOf = (f) => F.checks(f, MTLAYOUT, []).find((c) => c.id === "mt_idle");
const idle30 = idleOf(idleT(2, 30000));
assert.strictEqual(idle30.lvl, "fail");
assert.strictEqual(idle30.title, "Main tank barely acted");
assert.strictEqual(idle30.num, "2 casts in 30 s");
assert.strictEqual(idle30.ev, "T cast 2× in 30 s; top spells: Sunder Armor 1, Taunt 1.");
assert.strictEqual(idle30.bot, "T");
assert.strictEqual(idleOf(idleT(9, 30000)), undefined); // exactly 0.3/s is not under the limit
assert.strictEqual(idleOf(idleT(15, 30000)), undefined);
assert.strictEqual(idleOf(idleT(0, 15000)), undefined); // too short to judge
assert.strictEqual(idleOf(idleT(0, 20000)).lvl, "fail");
assert.strictEqual(idleOf(idleT(0, 20000)).ev, "T cast 0× in 20 s; top spells: none.");
assert.strictEqual(idleOf(fight({ dur_ms: 30000, bots: [bot({ name: "D", role: "dps", spells: [{ n: "Fireball", c: 1 }] })] })), undefined);
// A big fight's payload cuts the spell rows to a few (or none); the bot's casts total is what counts.
const trimmedT = (spells) => fight({ dur_ms: 30000, bots: [bot({ name: "T", role: "tank", taken: 100, casts: 20, spells })] });
assert.strictEqual(idleOf(trimmedT([])), undefined);
assert.strictEqual(idleOf(trimmedT([{ n: "Sunder Armor", c: 1 }, { n: "Revenge", c: 1 }, { n: "Taunt", c: 1 }])), undefined);

const outside = fight({ bots: [bot({ name: "T", role: "tank", taken: 100 }), bot({ name: "D", role: "dps", taken: 25 })] });
const outsideCheck = F.checks(outside, { mt: "T", ot: [] }, []).find((c) => c.id === "taken_outside");
assert.strictEqual(outsideCheck.lvl, "warn");
assert.strictEqual(outsideCheck.num, "20%");
assert.strictEqual(outsideCheck.title, "Most damage taken outside the tanks: D (damage)");
const healOutside = fight({ bots: [bot({ name: "T", role: "tank", taken: 100 }), bot({ name: "H", role: "healer", taken: 25 })] });
assert.strictEqual(F.checks(healOutside, { mt: "T", ot: [] }, []).find((c) => c.id === "taken_outside").title,
  "Most damage taken outside the tanks: H (healer)");

const overhealFight = fight({ bots: [bot({ name: "H", role: "healer", heal: 100, over: 100, casts: 3 })] });
const overhealCheck = F.checks(overhealFight, null, []).find((c) => c.id === "overheal");
assert.strictEqual(overhealCheck.lvl, "warn");
assert.strictEqual(overhealCheck.num, "50%");

const idleFight = fight({ bots: [bot({ name: "Idle", dmg: 0, heal: 0, casts: 0 }), bot({ name: "Busy" })] });
const idleCheck = F.checks(idleFight, null, []).find((c) => c.id === "idle");
assert.strictEqual(idleCheck.lvl, "warn");
assert.strictEqual(idleCheck.bot, "Idle");
assert.ok(idleCheck.title.includes("1"));

const otFight = fight({ bots: [bot({ name: "T", role: "tank" }), bot({ name: "OT", role: "tank", class: "druid" })] });
const otCheck = F.checks(otFight, { mt: "T", ot: ["OT"] }, []).find((c) => c.id === "ot_taunt:OT");
assert.strictEqual(otCheck.lvl, "warn");
assert.strictEqual(otCheck.num, "0×");

const deathFight = fight({
  bots: [bot({ name: "T", role: "tank" })],
  deaths: [{ name: "D", t_ms: 4000, killer: "mob", x: 1, y: 2, hits: [] }],
});
const otherDeath = F.checks(deathFight, { mt: "T", ot: [] }, []).find((c) => c.id === "other_death");
assert.strictEqual(otherDeath.lvl, "warn"); // one death in a clean fight
assert.strictEqual(otherDeath.num, 1);
assert.strictEqual(otherDeath.death, 0);
const deathsOf = (list, outcome) => F.checks(fight({ outcome, bots: [bot({ name: "T", role: "tank" })],
  deaths: list }), { mt: "T", ot: [] }, []).find((c) => c.id === "other_death");
const twoDeaths = [{ name: "A", t_ms: 1000, hits: [] }, { name: "B", t_ms: 2000, hits: [] }];
const threeDeaths = twoDeaths.concat([{ name: "C", t_ms: 3000, hits: [] }]);
assert.strictEqual(deathsOf(twoDeaths).lvl, "warn");
assert.strictEqual(deathsOf(threeDeaths).lvl, "fail");
assert.strictEqual(deathsOf(twoDeaths, "wipe").lvl, "fail"); // a wipe fails at any death count
assert.strictEqual(deathsOf(twoDeaths, "end").lvl, "warn");

const ruled = F.checks(byId("5-4"), LAYOUT,
  [{ rule: "healer_no_heals", level: "red", bot: "X", text: "X (healer) cast no heals" }]);
const ruleCheck = ruled.find((c) => c.id === "rule:healer_no_heals:X");
assert.strictEqual(ruleCheck.lvl, "fail");
assert.strictEqual(ruleCheck.bot, "X");
assert.strictEqual(ruleCheck.title, "X (healer) cast no heals");
assert.strictEqual(ruleCheck.ev, "Server rule healer_no_heals");
assert.strictEqual(ruleCheck.num, "");
// A red rule on a pull under 30 s is a warn: the bot barely had a global to act in.
const shortRule = (dur, level) => F.checks(fight({ dur_ms: dur, bots: [bot({ name: "T", role: "tank" })] }), { mt: "T", ot: [] },
  [{ rule: "dps_no_damage", level, bot: "X", text: "X did nothing" }]).find((c) => c.id === "rule:dps_no_damage:X");
assert.strictEqual(shortRule(20000, "red").lvl, "warn");
assert.strictEqual(shortRule(20000, "red").num, "(short pull)");
assert.strictEqual(shortRule(30000, "red").lvl, "fail");
assert.strictEqual(shortRule(20000, "yellow").lvl, "warn");
assert.strictEqual(shortRule(20000, "yellow").num, "");

// ---- no HTML in data: names come back verbatim, the renderer escapes them -------------------------------------------
const markupFight = fight({
  bots: [bot({ name: "<b>x</b>", role: "tank", taken: 5 })],
  deaths: [{ name: "<b>x</b>", t_ms: 1000, killer: "<i>k</i>", x: 0, y: 0, hits: [] }],
});
const markupCheck = F.checks(markupFight, { mt: "<b>x</b>", ot: [] }, []).find((c) => c.id === "tank_death");
assert.strictEqual(markupCheck.title, "Tank died: <b>x</b>");
assert.strictEqual(markupCheck.ev, "<b>x</b> at 1.0 s to <i>k</i>");
assert.strictEqual(F.railCards([{ key: "k", dur: 1, deaths: 0, step: 1, boss_name: "<img src=x>" }], {}, null)[0].mob,
  "<img src=x>");

// ---- lane_nontank and plan_strays: the rows read what the card already has -------------------------------------------
const jsDir = path.dirname(path.resolve(process.argv[2]));
const Aggro = require(path.join(jsDir, "aggrolanes.js"));
const P = require(path.join(jsDir, "planoverlay.js"));
const a2 = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "lanes-a2.json"), "utf8"));
const a2Lanes = Aggro.lanes(a2.track, a2.fight, a2.fight.bots);
const A2EXTRA = { lanes: a2Lanes, strays: null, packI: 0, prevPackI: 0 };
const a2Checks = F.checks(a2.fight, LAYOUT, [], A2EXTRA);
const laneRow = a2Checks.find((c) => c.id === "lane_nontank");
assert.strictEqual(laneRow.lvl, "fail");
assert.strictEqual(laneRow.title, "Enemy on a non-tank: Onyxia");
assert.strictEqual(laneRow.num, "12 s");
assert.strictEqual(laneRow.bot, "Quelin");
assert.strictEqual(laneRow.ev,
  "Onyxia was on Quelin, Sael, Aelia, Venomwind, Shadowfall, Pozzle, Ironcaller, Beardster, Hald, Lorenthil, Garic, Frostz,"
  + " Shadowbrand, Soulmane, Blackwalker, Impz, Ravenbringer, Brrtastic, Basicie, Kaelis, Beastcrest for 12 s in a row"
  + " (23 s in all) within 30 yd of its target. Under 8 s is a taunt bounce the 2 s samples cannot show.");
assert.strictEqual(F.verdict(a2.fight, LAYOUT, [], A2EXTRA), "fail");
assert.strictEqual(F.worstIndex([{ fight: a2.fight, extra: A2EXTRA }, detail("5-3")], LAYOUT), 0);
assert.strictEqual(F.railCards([{ key: "a2", dur: 323000, step: 0, deaths: 0 }],
  { a2: { fight: a2.fight, warnings: [], extra: A2EXTRA } }, null)[0].verdict, "fail");
// Without the extra (the old call, or a fight whose positions never loaded) there is no lane row at all.
assert.strictEqual(F.checks(a2.fight, LAYOUT, [], null).some((c) => c.id === "lane_nontank"), false);
assert.deepStrictEqual(F.checks(byId("5-4"), LAYOUT, [], { lanes: null, strays: null, packI: 0, prevPackI: 0 }).map((c) => c.id),
  c54.map((c) => c.id));

// The thresholds are inclusive: exactly 8000 ms is a warn (attempt 1's longest stint), exactly 12000 ms a fail (attempt 2).
const laneAt = (ms) => ({
  lanes: [{ id: "x" }],
  ntWorst: { id: "x", label: "Warder", ntMs: ms, ntLongestMs: ms, badNames: ["Tanky"] },
  lane: Aggro.LANE,
});
const atWarn = F.checks(fight({ dur_ms: 20000 }), null, [], { lanes: laneAt(8000), strays: null, packI: 0, prevPackI: 0 })
  .find((c) => c.id === "lane_nontank");
assert.strictEqual(atWarn.lvl, "warn");
assert.strictEqual(atWarn.num, "8 s");
assert.strictEqual(atWarn.bot, "Tanky");
const below = F.checks(fight({ dur_ms: 20000 }), null, [], { lanes: laneAt(7999), strays: null, packI: 0, prevPackI: 0 })
  .find((c) => c.id === "lane_nontank");
assert.strictEqual(below.lvl, "ok");
// A short fight is not judged: the samples cannot show a hold.
assert.strictEqual(F.checks(fight({ dur_ms: 5000 }), null, [], { lanes: laneAt(12000), strays: null, packI: 0, prevPackI: 0 })
  .some((c) => c.id === "lane_nontank"), false);

// A trash fight: a 4 s bounce is under the 8 s warn, so the row passes.
const tb = (guid, x, alive) => [guid, x, 0, 100, alive, 249];
const trashFrames = [];
for (let t = 2000; t <= 2030; t += 2) {
  const bounce = t === 2010 || t === 2012;
  trashFrames.push({ t, map: 249, step: 1, fight: true, bots: [tb(1, 0, 1), tb(2, 5, 1), tb(3, 40, 1)],
    mobs: [{ id: "w1", e: 7, x: 0, y: 0, hp: 100, v: bounce ? 2 : 1 }, { id: "w2", e: 7, x: 0, y: 0, hp: 100, v: 1 }] });
}
const trashLanes = Aggro.lanes({ frames: trashFrames, names: { 7: "Warder" } },
  { start: 2000, dur_ms: 30000, bots: [{ name: "T", guid: 1, role: "tank" }, { name: "H", guid: 2, role: "healer" }, { name: "D", guid: 3, role: "dps" }] }, []);
const trashRow = F.checks(fight({ dur_ms: 30000, bots: [bot({ name: "T", role: "tank" })] }), null, [],
  { lanes: trashLanes, strays: null, packI: 0, prevPackI: 0 }).find((c) => c.id === "lane_nontank");
assert.strictEqual(trashRow.lvl, "ok");
assert.strictEqual(trashRow.title, "Enemies stayed on the tanks");
assert.strictEqual(trashRow.num, "4 s");
assert.strictEqual(trashRow.ev, "Longest stretch on a non-tank within 30 yd: 4 s (Warder 1).");

// ---- plan_strays: the move steps between the previous pack and the fight's own ---------------------------------------
const plan = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "onyxias_lair.plan.json"), "utf8"));
const mc = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "molten_core.plan.json"), "utf8"));
const planCopy = () => JSON.parse(JSON.stringify(plan));
const emptyFight = fight({ dur_ms: 20000 });
const straysRow = (extra) => F.checks(emptyFight, null, [], extra).find((c) => c.id === "plan_strays");

// The move step before pack 21, moved 10 yd from the warder of pack 23: the route crosses its 23 yd ring on the way there.
const strayPlan = planCopy();
Object.assign(strayPlan.steps.find((s) => s.i === 18), { x: -166.62, y: -186, z: -66.26 });
const strayStrays = P.strays(strayPlan, 60);
assert.deepStrictEqual(strayStrays.map((s) => [s.i, s.pack]), [[18, 23]]);
const strayRow = straysRow({ lanes: null, strays: strayStrays, packI: 21, prevPackI: 17 });
assert.strictEqual(strayRow.lvl, "warn");
assert.strictEqual(strayRow.title, "Route crosses 1 ring(s) on the way here");
assert.strictEqual(strayRow.num, 1);
assert.strictEqual(strayRow.ev, "step 18 is 10.0 yd from Onyxian Warder (ring 23 yd)");

// The same step inside Onyxia's scripted wake (62 yd): a wake is a fail, not a warn.
const wakePlan = planCopy();
Object.assign(wakePlan.steps.find((s) => s.i === 26), { x: 10.7, y: -211.97, z: -86.08 });
const wakeStrays = P.strays(wakePlan, 60);
assert.deepStrictEqual(wakeStrays.map((s) => [s.i, s.pack, s.wake]), [[26, 29, true]]);
const wakeRow = straysRow({ lanes: null, strays: wakeStrays, packI: 29, prevPackI: 25 });
assert.strictEqual(wakeRow.lvl, "fail");
assert.strictEqual(wakeRow.ev, "step 26 is 10.0 yd from Onyxia (ring 62 yd, scripted wake)");

// Only the move steps strictly between the previous pack and this one count: the pack's own step does not.
const ignored = straysRow({ lanes: null, strays: [{ i: 3, pack: 21, name: "x", dist: 1, ring: 5, wake: true },
  { i: 21, pack: 29, name: "x", dist: 1, ring: 62, wake: true }], packI: 21, prevPackI: 17 });
assert.strictEqual(ignored.lvl, "ok");
assert.strictEqual(ignored.title, "Route to this pack stays out of other packs' rings");
assert.strictEqual(straysRow({ lanes: null, strays: null, packI: 0, prevPackI: 0 }), undefined);

// A non-Onyxia, multi-leg plan (Molten Core): a move step inside a later pack's ring, between its own packs.
const mcPlan = JSON.parse(JSON.stringify(mc));
mcPlan.mobs.push({ step: 44, guid: 99001, e: 999, n: "Core Hound", x: 1126.09, y: -1009.41, z: -187.25, lvl: 63, det: 20, wake_yd: 30 });
const mcStrays = P.strays(mcPlan, 60);
assert.deepStrictEqual(mcStrays.map((s) => [s.i, s.pack, s.wake]), [[43, 44, true]]);
const mcFight = fight({ dur_ms: 30000, step: 43, plan: "molten core" });
assert.strictEqual(P.fightPack(mcPlan, mcFight), 44);
const mcRow = straysRow({ lanes: null, strays: mcStrays, packI: 44, prevPackI: 42 });
assert.strictEqual(mcRow.lvl, "fail");
assert.strictEqual(mcRow.ev, "step 43 is 10.0 yd from Core Hound (ring 30 yd, scripted wake)");
// A fight without a plan (R1's pocket fights) has no ring row: no strays are passed.
assert.strictEqual(F.checks(mcFight, null, [], { lanes: null, strays: null, packI: P.fightPack(mcPlan, mcFight), prevPackI: 42 })
  .some((c) => c.id === "plan_strays"), false);

console.log("fightdebug_check: ok");
