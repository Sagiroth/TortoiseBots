"use strict";
// Run by commandersjs_test.go under node: the Fights panel's pure logic (web/static/js/fights.js).
const assert = require("assert");
const F = require(require("path").resolve(process.argv[2]));

const fight = () => ({
  id: "55-1", dur_ms: 30000, outcome: "kill", boss_name: "Magmadar", leg: "l1", step: 3,
  planned: [11502],
  engaged: [{ e: 11502, n: "Magmadar", c: 1 }, { e: 11673, n: "Core Hound", c: 2 }],
  bots: [
    { name: "Tanky", guid: 1, class: "warrior", role: "tank", sub: 0, dmg: 1200, heal: 0, over: 0, taken: 3000, casts: 6, deaths: 0, spells: [{ n: "Sunder Armor", c: 4 }, { n: "Heroic Strike", c: 6 }, { n: "Shield Block", c: 2 }, { n: "Taunt", c: 1 }] },
    { name: "Zed", guid: 9, class: "mage", role: "dps", sub: 1, dmg: 5000, heal: 0, over: 0, taken: 200, casts: 5, deaths: 0, spells: [{ n: "Fireball", c: 5 }] },
    { name: "Healy", guid: 2, class: "priest", role: "healer", sub: 0, dmg: 0, heal: 2500, over: 500, taken: 100, casts: 3, deaths: 1, spells: [{ n: "Heal", c: 3 }] },
  ],
  debuffs: { sampled_s: 10, up_s: { sunder5: 6, sunder: 9, faerie_fire: 10 } },
});

// ---- list rows
let rows = F.fightRows([{ id: "55-1", start: 9, dur: 30000, outcome: "kill", boss_name: "Magmadar", leg: "l1", step: 3, deaths: 1, warnings: 2 }]);
assert.strictEqual(rows.length, 1);
assert.strictEqual(rows[0].boss, "Magmadar");
assert.strictEqual(rows[0].deaths, 1);
assert.strictEqual(rows[0].warnings, 2);
assert.deepStrictEqual(F.fightRows(null), []);

// ---- bot table: subgroup then name, dps math
let table = F.botTable(fight());
assert.deepStrictEqual(table.map((r) => r.name), ["Healy", "Tanky", "Zed"]);
assert.strictEqual(Math.round(table.find((r) => r.name === "Zed").dps), 167);
assert.strictEqual(table.find((r) => r.name === "Healy").overhealPct.toFixed(1), (500 / 3000 * 100).toFixed(1));
assert.strictEqual(table.find((r) => r.name === "Tanky").spells.length, 3);
assert.strictEqual(table.find((r) => r.name === "Tanky").spells[0].name, "Heroic Strike");
assert.ok(Math.abs(table.find((r) => r.name === "Tanky").spells[0].perMin - 12) < 1e-9);
table = F.botTable(fight(), "dps");
assert.strictEqual(table[0].name, "Zed");
table = F.botTable(fight(), "hps");
assert.strictEqual(table[0].name, "Healy");
// no duration: dps and hps are 0, not NaN
table = F.botTable({ dur_ms: 0, bots: [{ name: "A", dmg: 5, heal: 5 }] });
assert.strictEqual(table[0].dps, 0);
assert.strictEqual(table[0].hps, 0);

// ---- debuff bars: percent per key, panel order
let bars = F.debuffBars(fight());
assert.deepStrictEqual(bars.map((b) => b.key), ["sunder5", "sunder", "faerie_fire", "cor", "coe", "cos", "demo", "thunder_clap", "expose_armor"]);
assert.strictEqual(Math.round(bars.find((b) => b.key === "sunder5").pct), 60);
assert.strictEqual(Math.round(bars.find((b) => b.key === "faerie_fire").pct), 100);
assert.strictEqual(bars.find((b) => b.key === "cor").pct, 0);
// no samples: every bar 0
bars = F.debuffBars({ debuffs: { sampled_s: 0, up_s: {} } });
assert.ok(bars.every((b) => b.pct === 0));

// ---- warnings strip: red first
let strip = F.warningStrip([
  { rule: "a", level: "info", bot: "Zed", text: "z" },
  { rule: "b", level: "red", bot: "Tanky", text: "t" },
]);
assert.strictEqual(strip[0].bot, "Tanky");
assert.strictEqual(strip[1].bot, "Zed");

// ---- pull post-mortem: planned vs engaged
assert.strictEqual(F.pullPostMortem(fight()), "planned 11502; engaged 11502 x1, 11673 x2 (adds)");
assert.strictEqual(F.pullPostMortem({ planned: [], engaged: [] }), "planned –; engaged nothing");

console.log("fights_check.js: all assertions passed");
