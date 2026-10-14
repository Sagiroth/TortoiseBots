"use strict";
// Run by commandersjs_test.go under node: the Commanders party table. The header and every row must have the same number of cells,
// and no helper in app.js may be declared twice (a later declaration silently replaces the earlier one: #976 declared cmdIcon twice,
// the role cell rendered as nothing and every cell moved one column left).
const assert = require("assert");
const fs = require("fs");
const vm = require("vm");
const src = fs.readFileSync(require("path").resolve(process.argv[2]), "utf8");

// 1. no duplicated function declaration at the same indentation (top level of the dashboard's closure)
const seen = {};
for (const m of src.matchAll(/^( {2})function ([A-Za-z0-9_$]+)\(/gm)) {
  assert.ok(!seen[m[2]], `function ${m[2]}() is declared twice in app.js (lines ${seen[m[2]]} and ${src.slice(0, m.index).split("\n").length})`);
  seen[m[2]] = src.slice(0, m.index).split("\n").length;
}

// 2. render the party table with the real builders and count the cells
function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}
const names = ["cmdIcon", "cmdBotLink", "cmdBarHtml", "cmdDoingHtml", "cmdPartyHtml"];
const rows = [
  { guid: 1, name: "Rikaric", cls: "Warrior", level: 46, role: "tank", roleLabel: "Tank", alive: true, lead: true, hp: 77, hpText: "1/2", hasPower: true, power: 12, powerKind: "rage", powerText: "12", dist: 0, far: false },
  { guid: 2, name: "Leltidre", cls: "Priest", level: 46, role: "healer", roleLabel: "Healer", alive: true, hp: 81, hpText: "1/2", hasPower: true, power: 27, powerKind: "mana", powerText: "27", dist: 5, far: false },
  { guid: 3, name: "Dead", cls: "Mage", level: 46, role: "dps", roleLabel: "DPS", alive: false, hp: 0, hpText: "0", dist: 9 },
  { guid: 4, name: "Away", cls: "Hunter", level: 46, role: "dps", roleLabel: "DPS", alive: true, otherMap: true, hp: 50, hpText: "1/2", hasPower: false },
];
const ctx = {
  esc: (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c])),
  CmdView: { partyRows: () => rows, botHref: (g) => "#bot/" + g, mobsOf: () => [], LOW_HP_PCT: 30 },
  CMD_ICONS: { tank: "<path/>", healer: "<path/>", dps: "<path/>", other: "<path/>", dead: "<path/>" },
  UnitInfo: { activity: () => ({ icon: "move", text: "moving" }) },
};
vm.createContext(ctx);
vm.runInContext(names.map(grab).join("\n") + "\nthis.out = cmdPartyHtml({ bots: [{ guid: 1, map: 1 }, { guid: 2, map: 1 }, { guid: 3, map: 1 }, { guid: 4, map: 1 }] });", ctx);

// direct children of each .cmd-row
function cells(html) {
  const res = []; let depth = 0, cur = null, n = 0;
  for (const m of html.matchAll(/<(\/?)([a-zA-Z0-9]+)([^>]*?)(\/?)>/g)) {
    const [, close, tag, attrs, self] = m, voidTag = /^(br|img|input|path|circle)$/i.test(tag) || self;
    if (close) { depth--; if (cur !== null && depth === cur) { res.push(n); cur = null; } continue; }
    if (cur === null && /class="cmd-row[ "]/.test(attrs)) { cur = depth; n = 0; }
    else if (cur !== null && depth === cur + 1) n++;
    if (!voidTag) depth++;
  }
  return res;
}
const counts = cells(ctx.out);
assert.strictEqual(counts.length, rows.length + 1, "a header and one row per character");
const head = counts[0];
assert.ok(head >= 5);
counts.forEach((c, i) => assert.strictEqual(c, head, `row ${i} has ${c} cells, the header has ${head}`));
