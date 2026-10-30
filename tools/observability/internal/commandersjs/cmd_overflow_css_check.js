"use strict";
// Run by commandersjs_test.go under node: the Commanders cards never widen the page on a phone. A live card carries a table of
// six nowrap columns (the Fights panel); the panel scrolls inside the card instead. The map toolbar fits the phone width by
// design (a second row), and no card-scoped rule sets a fixed width above the narrowest phone.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const cssPath = path.resolve(process.argv[2]);
const css = fs.readFileSync(cssPath, "utf8");

// ---- the Fights panel: one scroll container inside the card --------------------------------------------------------
const rule = (sel) => {
  const parts = [];
  const re = new RegExp("(^|\\n)([^{}\\n]*" + sel.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + "[^{}\\n]*)\\{([^}]*)\\}", "g");
  for (const m of css.matchAll(re)) parts.push(m[3]);
  return parts.join("\n");
};
const fights = rule(".cmd-fights");
assert.ok(fights, "the fights panel has a rule");
assert.ok(/overflow-x:\s*(auto|scroll)/.test(fights), "the fights panel scrolls sideways inside the card: " + fights);
assert.ok(/min-width:\s*0/.test(rule(".cmd-card .cmd-fights")), "the scroll container may shrink below its table");
assert.ok(/min-width:\s*0/.test(rule(".fight-panel")), "the panel may shrink below its table");
assert.ok(/width:\s*max-content/.test(rule(".cmd-fights .data-table")), "the table keeps its six columns inside the scroller");
// .data-table's headers are nowrap; a table of six of them is wider than a 390 px phone.
assert.ok(/\.data-table th\s*\{[^}]*white-space:\s*nowrap/.test(css), "the header cells really are nowrap (the reason for the scroller)");

// ---- no card-scoped fixed width above the narrowest phone ----------------------------------------------------------
const PHONE = 390;
const offenders = [];
const body = css.replace(/\/\*[\s\S]*?\*\//g, "");
// Rules outside @media blocks only: the phone overrides live inside a media query of their own.
let depthQuery = 0;
for (const m of body.matchAll(/(@media[^{]*\{)|(\})|([^{}]+)\{([^}]*)\}/g)) {
  if (m[1]) { depthQuery++; continue; }
  if (m[2]) { depthQuery = Math.max(0, depthQuery - 1); continue; }
  if (depthQuery > 0) continue;
  const sel = m[3].trim(), decl = m[4];
  if (!/\.cmd-card|\.cmd-body|\.cmd-panel|\.cmd-fights|\.fight-panel/.test(sel)) continue;
  for (const d of decl.matchAll(/(?:^|;)\s*(width|min-width)\s*:\s*(\d+)px/g)) {
    if (Number(d[2]) > PHONE) offenders.push(sel + " { " + d[1] + ": " + d[2] + "px }");
  }
}
assert.deepStrictEqual(offenders, [], "a card rule wider than a phone: " + offenders.join("; "));

// ---- the phone's top bar and map toolbar fit by design ---------------------------------------------------------------
// "Commanders" was ellipsised to "Com..." by a nowrap title rule.
const db = css.indexOf("@media (max-width: 600px)");
assert.ok(db > 0, "the phone media query exists");
const phone = css.slice(db, css.indexOf("\n}", db) + 2);
assert.ok(/\.page-head \{[^}]*order: 2/.test(phone) && /\.page-title \{[^}]*white-space: normal/.test(phone),
  "the page title gets its own row on a phone instead of being clipped: " + phone.slice(0, 400));
assert.ok(!/\.page-title \{[^}]*white-space: nowrap/.test(phone), "and is not clipped");
// The map toolbar (picture picker, Layers, zoom, Follow, Whole map) wraps inside the map box.
const mt = css.indexOf("@media (max-width: 640px)", css.indexOf(".cmd-maptools button { white-space: nowrap; }"));
assert.ok(mt > 0, "the toolbar has a phone rule");
const mtRule = css.slice(mt, css.indexOf("\n}", mt) + 2);
assert.ok(/\.cmd-maptools \{[^}]*flex-wrap: wrap/.test(mtRule), "the toolbar wraps: " + mtRule);
assert.ok(/\.cmd-maptools button \{[^}]*white-space: nowrap/.test(css), "a button label never breaks mid-word (\"Whole map\")");

console.log("cmd overflow css ok");
