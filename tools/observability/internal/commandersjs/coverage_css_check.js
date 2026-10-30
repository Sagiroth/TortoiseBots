"use strict";
// Run by commandersjs_test.go under node: the Raid coverage block in app.css. The block styles every class the coverage view
// emits, borrows the theme's colours only, and on a phone the line is one column with no fixed width above 358 px.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const cssPath = path.resolve(process.argv[2]);
const css = fs.readFileSync(cssPath, "utf8");

const HEAD = "/* ---------- Raid coverage (tactics coverage) ---------- */";
const at = css.indexOf(HEAD);
assert.ok(at >= 0, "the coverage block starts with its header comment");
assert.strictEqual(css.indexOf(HEAD, at + HEAD.length), -1, "the block is appended once");
const nextHead = css.indexOf("/* ---------- ", at + HEAD.length);
const block = css.slice(at, nextHead < 0 ? css.length : nextHead);

// ---- no fixed colour: every colour is a theme token ------------------------------------------------
const rawColour = block.match(/#[0-9a-fA-F]{3,8}\b|\brgba?\(|\bhsla?\(/g);
assert.strictEqual(rawColour, null, "no fixed colour in the block: " + (rawColour || []).join(" "));
assert.ok(!/outline\s*:\s*(none|0)\b/.test(block), "the focus ring is never removed");

// ---- every class the view emits has a rule in the block --------------------------------------------
const js = fs.readFileSync(path.resolve(process.argv[3]), "utf8");
const emitted = new Set();
for (const m of js.matchAll(/class="([^"]*)"/g)) {
  for (const token of m[1].replace(/\$\{[^}]*\}/g, " ").split(/\s+/)) {
    if (/^cov-[a-z0-9-]+$/.test(token) && !token.endsWith("-")) emitted.add(token);
  }
}
// Built in template strings, not class="" literals: the status and level suffixes.
const suffixes = [
  "cov-st-answered", "cov-st-partly", "cov-st-missing", "cov-st-notneeded", "cov-st-unknown",
  "cov-need-full", "cov-need-partly", "cov-need-none", "cov-need-unknown", "cov-need-ok",
  "cov-seg-answered", "cov-seg-partly", "cov-seg-missing", "cov-seg-notneeded", "cov-seg-unknown",
];
suffixes.forEach((name) => emitted.add(name));
emitCheck("cov-chips", "cov-chip", "cov-line", "cov-needs", "cov-answers", "cov-summary", "cov-boss", "cov-extra-row");
const missing = [...emitted].filter((name) => !new RegExp("\\." + name + "(?![\\w-])").test(block));
assert.deepStrictEqual(missing, [], "classes without a rule: " + missing.join(", "));

// ---- the phone: one column, lines wrap, no fixed width above 358 px ---------------------------------
function blockAt(start) {
  let depth = 0;
  for (let i = start; i < block.length; i++) {
    if (block[i] === "{") depth++;
    else if (block[i] === "}" && --depth === 0) return block.slice(start + 1, i);
  }
  return "";
}
const phone = block.indexOf("@media (max-width: 500px)");
assert.ok(phone >= 0, "the 500 px breakpoint exists");
const phoneBody = blockAt(block.indexOf("{", phone));
assert.ok(/\.cov-line\s*\{[^}]*grid-template-columns:\s*1fr/.test(phoneBody), "the phone line is one column");
assert.ok(/\.cov-line\s*\{[^}]*gap:\s*/ .test(phoneBody), "the phone line tightens its gap");
assert.ok(/\.cov-head\s*\{[^}]*flex-wrap:\s*wrap/.test(block), "the card head wraps its chips");
assert.ok(/\.cov-answers\s*\{[^}]*overflow-wrap:\s*anywhere/.test(block), "a comment breaks inside the column");
assert.ok(/\.cov-chips\s*\{[^}]*flex-wrap:\s*wrap/.test(block), "the raid chips wrap");
const wide = [...block.matchAll(/(?<!max-)width:\s*(\d+)px/g)].map((m) => Number(m[1])).filter((n) => n > 358);
assert.deepStrictEqual(wide, [], "no .cov-* rule fixes a width above 358 px");

// The boss panel's chip has its five states, coloured by the theme.
["answered", "partly", "missing", "notneeded", "unknown"].forEach((status) => {
  assert.ok(new RegExp("\\." + "fd-cov-" + status + "\\s*\\{[^}]*color:\\s*var\\(--").test(block), "the " + status + " chip colour");
});

function emitCheck(...names) {
  names.forEach((name) => emitted.add(name));
}

console.log("coverage css ok");
