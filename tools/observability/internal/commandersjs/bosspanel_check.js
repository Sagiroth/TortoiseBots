"use strict";
// Run by commandersjs_test.go under node: the boss abilities panel's pure half (web/static/js/bosspanel.js), fed with the
// Venoxis/Jeklik rows the spec names. The next window comes from the table's timers, an ability in another phase is hidden,
// a cast event matches by spell id even when its name differs, an hp phase latches and an aura rule listed first wins.
const assert = require("assert");
const path = require("path");
const B = require(path.resolve(process.argv[2]));

// The converter's shape: one boss per entry, the CSV's a/b/c/d named.
const DATA = {
  bosses: [
    {
      entry: 14507, name: "Venoxis", map: 309, order: 0,
      abilities: [
        { spell: 23861, name: "Poison Cloud", first: 10000, repeatMin: 15000, repeatMax: 20000, flags: "p=snake;ground" },
        { spell: 8269, name: "Frenzy", first: 0, repeatMin: 0, repeatMax: 0, flags: "p=snake;hp20" },
        { spell: 23859, name: "Dispell", first: 0, repeatMin: 0, repeatMax: 0, flags: "" },
        { spell: 23966, name: "Bat Form", first: 0, repeatMin: 0, repeatMax: 0, flags: "p=bat;self" },
      ],
      phases: [
        { name: "snake", rule: "hp<50", order: 0 },
        { name: "p1", rule: "hp<100", order: 1 },
      ],
      adds: [{ entry: 14884, role: "summon", priority: 0 }],
      notes: ["Poison Cloud needs the snake phase."],
    },
    {
      entry: 14517, name: "Jeklik", map: 309, order: 1,
      abilities: [{ spell: 23966, name: "Bat Form", first: 0, repeatMin: 0, repeatMax: 0, flags: "self" }],
      phases: [{ name: "bat", rule: "aura=23966", order: 0 }, { name: "troll", rule: "hp<50", order: 1 }],
      adds: [],
      notes: [],
    },
  ],
};
const T = B.normalize(DATA);
const venoxis = B.bossFor(T, 14507);
const jeklik = B.bossFor(T, 14517);

// ---- the table loads from either the converter's object or raw CSV-shaped rows --------------------------------
assert.strictEqual(T.bosses.length, 2);
assert.strictEqual(B.normalize({ "14507": { name: "Venoxis", abilities: [] } }).bosses[0].entry, 14507, "a table keyed by entry loads too");
assert.strictEqual(B.normalize({ entry: 14507, name: "Venoxis", abilities: [] }).bosses[0].name, "Venoxis", "a single boss object loads too");
assert.strictEqual(venoxis.name, "Venoxis");
assert.deepStrictEqual(venoxis.abilities[0].flags, { phase: "snake", ground: true });
assert.strictEqual(B.bossFor(T, "14507").entry, 14507, "an entry from JSON is matched as a number");
const raw = B.normalize({ bosses: [{ entry: 1, abilities: [{ key: "24314", a: 33000, b: 0, c: 0, d: "aura;mark" }], phases: [{ key: "p1", a: "hp<100", b: 0 }], adds: [{ key: "14988", a: "linked", b: 0 }] }] });
assert.deepStrictEqual(raw.bosses[0].abilities[0], { sid: 24314, name: "spell 24314", first: 33000, repeatMin: 0, repeatMax: 0, rawFlags: "aura;mark", flags: { aura: true, mark: true }, source: "" });
assert.strictEqual(raw.bosses[0].phases[0].rule, "hp<100");
assert.strictEqual(raw.bosses[0].adds[0].entry, 14988);

// ---- next: seen -> last + b..c; never seen and a=0 -> null (not NaN) -----------------------------------------
const mob = { id: "5", entry: 14507, name: "Venoxis", hp: 60, x: 0, y: 0, rank: "b", au: [] };
const ev = [{ k: "cast", t: 30000, sid: 23861, n: "Venoxis", s: "Poison Cloud" }];
let p = B.model(T, mob, ev, 0, 50000);
const cloud = p.abilities.find((a) => a.sid === 23861);
assert.deepStrictEqual({ lo: cloud.next.lo, hi: cloud.next.hi }, { lo: 45000, hi: 50000 });
assert.strictEqual(cloud.lastMs, 20000, "30 s seen, 50 s now");
assert.strictEqual(cloud.seen, true);
const frenzy = p.abilities.find((a) => a.sid === 8269);
assert.strictEqual(frenzy.next, null, "a=0 and never seen: no next, not NaN");
const dispel = p.abilities.find((a) => a.sid === 23859);
assert.strictEqual(dispel.next, null);
const overdue = B.model(T, mob, [], 0, 100000);
assert.ok(B.render(T, overdue, {}).includes("≈ 10 s (late)"), "a first-cast window 90 s past is late, not silently current");

// ---- phase filter: p=bat is out of phase while the phase is troll -------------------------------------------
const troll = B.model(T, { entry: 14507, hp: 40, au: [] }, [], 0, 0);
assert.strictEqual(troll.phase, "snake");
assert.strictEqual(troll.abilities.find((a) => a.sid === 23966).inPhase, false, "Bat Form is a bat-phase ability");
assert.strictEqual(troll.abilities.find((a) => a.sid === 23861).inPhase, true, "Poison Cloud is a snake-phase ability");

// ---- a cast event matches by spell id even when the localized name differs -----------------------------------
const renamed = B.model(T, { entry: 14507, hp: 60, au: [] }, [{ k: "cast", t: 1000, sid: 23861, s: "Gift des Giftes" }], 0, 5000);
assert.strictEqual(renamed.abilities.find((a) => a.sid === 23861).seen, true, "the name did not decide the match");
assert.strictEqual(renamed.abilities.find((a) => a.name === "Frenzy").seen, false);

// ---- phase evaluation: hp 60 -> p1; 49 -> snake; back to 55 with prior snake stays snake; an aura rule first wins --
assert.strictEqual(B.phaseOf(venoxis, { hp: 60 }).phase, "p1");
assert.strictEqual(B.phaseOf(venoxis, { hp: 49 }).phase, "snake");
assert.strictEqual(B.phaseOf(venoxis, { hp: 55 }, "snake").phase, "snake", "the hp<50 rule latches");
assert.strictEqual(B.phaseOf(venoxis, { hp: 49 }, "p1").phase, "snake", "a more severe hp threshold still applies after p1");
assert.strictEqual(B.phaseOf(venoxis, { hp: 100 }, "snake").phase, "snake", "a heal back does not undo the phase");
assert.strictEqual(B.phaseOf(jeklik, { hp: 100, au: [{ id: 23966 }] }).phase, "bat");
assert.strictEqual(B.phaseOf(jeklik, { hp: 40, au: [{ id: 23966 }] }).phase, "bat", "the aura rule is listed before the hp rule");
const auraFirst = { phases: [{ name: "air", rule: "flag=h", order: 0 }, { name: "p1", rule: "hp<100", order: 1 }] };
assert.strictEqual(B.phaseOf(auraFirst, { hp: 60, uf: "h" }).phase, "air");
assert.strictEqual(B.phaseOf(auraFirst, { hp: 60, uf: "" }).phase, "p1");

// ---- auras, flags, adds and the current cast ----------------------------------------------------------------
p = B.model(T, Object.assign({}, mob, {
  hp: 48, uf: "nh", au: [{ id: 24169, n: "Tiger Form", st: 2, l: 12000 }, { id: 1234, n: "Passive", l: null }],
  cid: 23861, cleft: 1500, ctot: 3000, add: [{ entry: 14884, hp: 55 }],
}), ev, 0, 50000, "snake");
assert.deepStrictEqual(p.auras, [{ id: 24169, name: "Tiger Form", stacks: 2, leftMs: 12000 }, { id: 1234, name: "Passive", stacks: 1, leftMs: null }]);
assert.deepStrictEqual(p.flagWords, ["untargetable", "flying"]);
assert.strictEqual(p.adds[0].entry, 14884);
assert.strictEqual(p.adds[0].alive, true);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).seen, true);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).lastMs, 20000);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).casting.frac, 0.5);
const freshCast = B.model(T, Object.assign({}, mob, { cid: 23861, cleft: 1500, ctot: 3000 }), [], 0, 50000, "snake");
assert.strictEqual(freshCast.abilities.find((a) => a.sid === 23861).seen, true, "a cast the report carries is a sighting");
assert.strictEqual(freshCast.abilities.find((a) => a.sid === 23861).lastMs, 0);
assert.strictEqual(B.model(T, Object.assign({}, mob, { cid: 23861, cleft: 1500, ctot: 3000, ctargetName: "Healbot" }), [], 0, 50000, "snake").abilities.find((a) => a.sid === 23861).casting.target, "Healbot", "the cast names its target");
assert.deepStrictEqual([freshCast.abilities.find((a) => a.sid === 23861).next.lo, freshCast.abilities.find((a) => a.sid === 23861).next.hi], [65000, 70000]);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).casting.leftMs, 1500);

// ---- render: escaped, phase and ticks, unseen grey, other phases folded -------------------------------------
const html = B.render(T, p, { open: true });
assert.ok(/<details class="fd-boss-panel" data-entry="14507" open>/.test(html), html.slice(0, 200));
assert.ok(html.includes("Venoxis") && html.includes('class="fd-boss-phase">snake<'), "the phase label");
assert.ok(html.includes('class="fd-boss-tick" style="left:50%"'), "an hp<50 tick");
assert.ok(/class="fd-boss-ability unseen/.test(html), "a never-seen ability is grey");
assert.ok(html.includes("Other phases"), "an ability of another phase is folded");
assert.ok(html.includes("untargetable") && html.includes("flying"), "the flag words");
assert.ok(B.render(T, Object.assign({}, p, { flags: "v", flagWords: B.flagsText("v") }), {}).includes("not visible"), "a vanished boss says so");
assert.ok(html.includes("Poison Cloud needs the snake phase."), "the note");
const evil = B.render(T, Object.assign({}, p, { boss: { entry: 14507 }, notes: ["<img src=x>"] }), {});
assert.ok(!/<img/.test(evil) && evil.includes("&lt;img"), evil);

// ---- add links: sum wins over the data's entry link ----------------------------------------------------------
const bossRow = { id: "5", entry: 14507, rank: "b", hp: 40, x: 10, y: 10 };
const dataAdd = { id: "7", entry: 14884, hp: 80, x: 20, y: 20 };
const summoned = { id: "8", entry: 99999, hp: 80, x: 30, y: 30, sum: "5" };
const links = B.addLinks(T, [bossRow, dataAdd, summoned]);
assert.strictEqual(links.length, 2);
assert.strictEqual(links.find((l) => l.from.id === "7").to.id, "5", "the data's add entry links to the boss");
assert.strictEqual(links.find((l) => l.from.id === "8").to.id, "5", "the summoner guid links an unknown add by itself");
assert.strictEqual(links.find((l) => l.from.id === "7").priority, 0);

// ---- marked: from a mark flag in the table, never a spell id in the page --------------------------------------
const markTable = B.normalize({ bosses: [{ entry: 1, abilities: [{ spell: 24314, name: "Threatening Gaze", flags: "aura;mark" }] }] });
assert.deepStrictEqual(B.markIds(markTable), { 24314: "Threatening Gaze" });
assert.strictEqual(B.markedBy(markTable, { db: [{ id: 24314, n: "Threatening Gaze" }] }), "Threatening Gaze");
assert.strictEqual(B.markedBy(markTable, { db: [{ id: 1, n: "Sunder Armor" }] }), "");

// ---- missing table / unknown boss: no panel rather than a guess ---------------------------------------------
assert.strictEqual(B.model(null, mob, ev, 0, 1), null);
assert.strictEqual(B.render(null, p), "");
assert.strictEqual(B.render(B.normalize({ bosses: [] }), p), "");

// The committed table (tools/boss_data.py writes rules as objects): its phases must read as rules, not "[object Object]".
const REAL = B.normalize(require(path.resolve(path.dirname(process.argv[2]), "../../data/boss_abilities.json")));
const ven = (REAL.byEntry || REAL.bosses || REAL)["14507"];
assert.deepStrictEqual(ven.phases.map((x) => x.rule), ["aura=23849", "hp<50", "start"]);
assert.strictEqual(B.phaseOf(ven, { hp: 60, au: [] }).phase, "p1");
assert.strictEqual(B.phaseOf(ven, { hp: 40, au: [] }).phase, "snake");
assert.strictEqual(B.phaseOf(ven, { hp: 80, au: [{ id: 23849 }] }).phase, "snake");

console.log("boss panel ok");
