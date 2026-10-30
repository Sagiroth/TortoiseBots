"use strict";
// Run by commandersjs_test.go under node: the coverage view's markup (web/static/js/coverage.js renderRaid). "Missing only"
// hides the answered and not-needed lines and keeps the counts, a boss card left without a line is hidden, an untrusted row
// comment is escaped, the unread-rows banner says what is unknown, a chip carries its status in the aria label, and the
// boss= route opens that card.
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
  maps: [{ key: "testraid", mapId: 900, name: "Test Raid", bosses: [90001, 90002] }],
  bosses: {
    "90001": {
      entry: 90001, name: "Boss One", map: "testraid", order: 1,
      abilities: [
        ability(91001, "Spell A", { int: true }),
        ability(91002, "Spell B"),
        ability(91003, "Spell C", { int: true, okNeeds: { interrupt: "nokick" } }),
        ability(91004, "Spell D", { ground: true }),
      ],
      phases: [], adds: [], notes: [],
    },
    "90002": {
      entry: 90002, name: "Boss Two", map: "testraid", order: 2,
      abilities: [ability(92001, "Spell E", { int: true })],
      phases: [], adds: [], notes: [],
    },
  },
  codeAnswers: [
    { map: "*", boss: 0, key: "flag=ground", spell: 0, add: 0, flag: "ground", need: "avoid", level: "partly", name: "avoid aoe", text: "generic: steps out", source: [] },
  ],
};

const ROWS = {
  tactics: [
    { map: 900, boss: 90001, kind: "interrupt", target: 0, spell: 91001, priority: 0, param: 0, comment: "<img onerror=\"alert(1)\">" },
    { map: 900, boss: 90002, kind: "interrupt", target: 0, spell: 92001, priority: 0, param: 0, comment: "Boss Two: Heal" },
  ],
  stands: [],
  planKillFirst: [],
  source: "world",
  loadedAt: "2026-10-30T12:34:00Z",
};

const result = C.compute(TABLE, ROWS);
const plain = C.renderRaid(result, { raid: "testraid", missing: false, boss: null }, {});
const only = C.renderRaid(result, { raid: "testraid", missing: true, boss: null }, {});

// Every line is there without the filter.
["91001", "91002", "91003", "91004"].forEach((sid) => {
  assert.ok(plain.includes('data-key="' + sid + '"'), "line " + sid + " is drawn");
});

// ---- missing only hides answered and not-needed, keeps partly/missing, counts unchanged ----------------
assert.ok(only.includes('data-key="91002"'), "a classify-missing line stays");
assert.ok(only.includes('data-key="91004"'), "a partly line stays");
assert.ok(!only.includes('data-key="91001"'), "an answered line is hidden");
assert.ok(!only.includes('data-key="91003"'), "a not-needed line is hidden");
assert.ok(only.includes('<span class="cov-counts">2 answered, 1 partly, 1 missing, 1 not needed</span>'), "the raid counts do not change: " + only);
assert.ok(only.includes('<span class="cov-counts">1 answered, 1 partly, 1 missing, 1 not needed</span>'), "the boss counts do not change either: " + only);
assert.ok(plain.includes('<span class="cov-counts">2 answered, 1 partly, 1 missing, 1 not needed</span>'), "same counts without the filter");

// ---- a boss card with no lines left is hidden ---------------------------------------------------------
assert.ok(plain.includes('data-entry="90002"'), "Boss Two is drawn without the filter");
assert.ok(!only.includes('data-entry="90002"'), "Boss Two has only an answered line and is hidden");

// ---- an untrusted row comment is escaped ---------------------------------------------------------------
assert.ok(!/<img/.test(plain), "the comment never becomes markup");
assert.ok(plain.includes("&lt;img onerror=&quot;alert(1)&quot;&gt;"), "the comment is shown escaped");

// ---- the unread-rows banner ---------------------------------------------------------------------------
assert.ok(plain.includes('<span class="cov-need cov-need-full">interrupt ✓</span>'), "an answered need ticks");
assert.ok(plain.includes('<span class="cov-need cov-need-none">classify ✗</span>'), "a need with no answer is crossed");
assert.ok(plain.includes('<span class="cov-answers">ok: nokick</span>'), "a waived need shows its reason");
const unread = C.compute(TABLE, null);
const banner = C.renderRaid(unread, { raid: "testraid", missing: false, boss: null }, {});
assert.ok(banner.includes("World database not reachable: answers from tactic and stand rows are unknown; code answers are shown."),
  "the rows=null banner names what is unknown");
assert.ok(banner.includes("1 unknown") || banner.includes("unknown"), "the counts carry the unknown lines");

// ---- a chip's aria label and the boss= card -----------------------------------------------------------
assert.ok(/<span class="cov-st cov-st-missing" title="[^"]*" aria-label="coverage: missing">✗<\/span>/.test(plain), "the line chip carries its status");
const focused = C.renderRaid(result, { raid: "testraid", missing: false, boss: 90002 }, {});
assert.ok(/<details class="cov-boss" data-entry="90002" open>/.test(focused), "the boss= card is open");

console.log("coverage ui ok");
