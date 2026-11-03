"use strict";
// Run by commandersjs_test.go under node: a truncated tactics snapshot (the
// daemon hit a query's row cap) marks every database-answered need unknown and
// warns on the page; the rows before the cut are never used as proof.
const assert = require("assert");
const path = require("path");
const C = require(path.resolve(process.argv[2]));

function ability(sid, name, flags) {
  return Object.assign({
    sid, name, castMs: 0, a: 0, b: 0, c: 0, phase: "", target: "",
    aura: false, self: false, mark: false, ground: false, cone: false, tail: false, int: false, summon: 0, hp: 0,
    ok: "", okNeeds: {},
  }, flags || {});
}

const TABLE = {
  version: 1,
  maps: [{ key: "testraid", mapId: 900, name: "Test Raid", bosses: [90001] }],
  bosses: {
    "90001": {
      entry: 90001, name: "Boss One", map: "testraid", order: 1,
      abilities: [ability(91001, "Spell A", { int: true })],
      phases: [], adds: [], notes: [],
    },
  },
  codeAnswers: [],
};

const ROWS = {
  tactics: [
    { map: 900, boss: 90001, kind: "interrupt", target: 0, spell: 91001, priority: 0, param: 0, comment: "<img onerror=\"alert(1)\">" },
  ],
  stands: [],
  planKillFirst: [],
  source: "world",
  loadedAt: "2026-10-30T12:34:00Z",
};

// ---- complete rows answer the interrupt need -----------------------------------------------------------
const full = C.compute(TABLE, ROWS);
assert.strictEqual(full.raids[0].bosses[0].lines[0].status, "answered", "complete rows keep the interrupt answered");

// ---- a cut snapshot: unknown, no DB-funded answer, and a banner ----------------------------------------
const cut = C.compute(TABLE, Object.assign({}, ROWS, { truncated: true, truncatedAt: 5000 }));
const line = cut.raids[0].bosses[0].lines[0];
assert.strictEqual(line.needs.interrupt, "unknown", "a DB-dependent need is unknown when the rows were cut");
assert.strictEqual(line.status, "unknown", "the line is unknown, not answered or missing: " + line.status);
assert.ok(!line.answers.some((answer) => /interrupt row/.test(answer.text)),
  "a row before the cut does not answer the need: " + JSON.stringify(line.answers));
assert.ok(cut.raids[0].bosses[0].counts.unknown >= 1, "the counts carry the unknown line");
assert.ok(cut.raids[0].bosses[0].counts.answered === 0, "no line reads answered from the cut rows");
assert.ok(cut.raids[0].bosses[0].counts.missing === 0, "no line reads missing from the cut rows");

const page = C.renderRaid(cut, { raid: "testraid", missing: false, boss: null }, {});
assert.ok(page.includes(C._esc("the server's tactic rows were cut off at 5000 rows: coverage from the database is incomplete")),
  "the page names the cap in the incomplete-coverage banner");
assert.ok(!/<img/.test(page), "the cut banner and the row comments stay escaped");

// A code answer is not a database row: the cut cannot take it away.
function codeAnswer(opts) {
  return Object.assign({
    map: "*", boss: 0, key: "", spell: 0, add: 0, flag: "", need: "", level: "", name: "code answer", text: "", source: [],
  }, opts || {});
}

function cutWith(codes, adds) {
  const boss = Object.assign({}, TABLE.bosses["90001"], {
    abilities: [ability(91001, "Spell A", { int: true })],
    adds: adds || [],
  });
  return C.compute(
    Object.assign({}, TABLE, { bosses: { "90001": boss }, codeAnswers: codes || [] }),
    Object.assign({}, ROWS, { truncated: true, truncatedAt: 5000 }),
  );
}

const fullCode = cutWith([codeAnswer({ spell: 91001, need: "interrupt", level: "full" })]);
const fullCodeLine = fullCode.raids[0].bosses[0].lines.find((item) => item.key === "91001");
assert.strictEqual(fullCodeLine.needs.interrupt, "full", "a full code answer survives the cut");
assert.strictEqual(fullCodeLine.status, "answered", "a full code answer still answers the line: " + fullCodeLine.status);

const partlyCode = cutWith([codeAnswer({ spell: 91001, need: "interrupt", level: "partly" })]);
const partlyCodeLine = partlyCode.raids[0].bosses[0].lines.find((item) => item.key === "91001");
assert.strictEqual(partlyCodeLine.needs.interrupt, "partly", "a partly code answer survives the cut");
assert.strictEqual(partlyCodeLine.status, "partly", "a partly code answer still half-answers the line: " + partlyCodeLine.status);

const addPartly = cutWith(
  [codeAnswer({ add: 90002, need: "kill", level: "partly" })],
  [{ entry: 90002, name: "Add One", priority: 1 }],
);
const addPartlyLine = addPartly.raids[0].bosses[0].lines.find((item) => item.key === "add:90002");
assert.strictEqual(addPartlyLine.needs.kill, "partly", "a partly code answer survives the cut on an add line");
assert.strictEqual(addPartlyLine.status, "partly", "a partly code answer half-answers the add line: " + addPartlyLine.status);

const addBare = cutWith([], [{ entry: 90002, name: "Add One", priority: 1 }]);
const addBareLine = addBare.raids[0].bosses[0].lines.find((item) => item.key === "add:90002");
assert.strictEqual(addBareLine.needs.kill, "unknown", "an add line with no code answer is unknown when the rows were cut");
assert.strictEqual(addBareLine.status, "unknown", "a DB-only add line is unknown, not missing: " + addBareLine.status);

// ---- the banner names whichever table hit its cap ------------------------------------------------------
const plans = C.compute(TABLE, Object.assign({}, ROWS, { truncated: true, truncatedAt: 1000 }));
const plansPage = C.renderRaid(plans, { raid: "testraid", missing: false, boss: null }, {});
assert.ok(plansPage.includes("cut off at 1000 rows"), "a plan-table cap is named too");

// ---- a complete snapshot keeps the old page -------------------------------------------------------------
const plain = C.renderRaid(full, { raid: "testraid", missing: false, boss: null }, {});
assert.ok(!plain.includes("cut off"), "a complete snapshot has no incomplete-coverage banner");
assert.ok(plain.includes('<span class="cov-need cov-need-full">interrupt ✓</span>'), "the complete page still ticks the need");

console.log("coverage truncated ok");
