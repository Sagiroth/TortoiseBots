"use strict";
// Run by commandersjs_test.go under node: app.css carries the Fight debugger block (tokens on the card, every class the builders emit,
// the phone column, colours from the theme tokens only, and the dashboard's focus ring kept).
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const cssPath = path.resolve(process.argv[2]);
const css = fs.readFileSync(cssPath, "utf8");

const HEAD = "/* ---------- Fight debugger (Commanders card) ---------- */";
const at = css.indexOf(HEAD);
assert.ok(at >= 0, "the Fight debugger block starts with its header comment");
// Its own block only: later blocks in app.css have their own checks and may carry their own colours.
const nextHead = css.indexOf("/* ---------- ", at + HEAD.length);
const block = css.slice(at, nextHead < 0 ? css.length : nextHead);
assert.ok(!css.slice(at + HEAD.length).includes(HEAD), "the block is appended once");

// the level tokens live on the card and only borrow the theme's accent tokens
assert.ok(/\.cmd-card\s*\{[^}]*--fd-fail:\s*var\(--accent-red\)/.test(block), "--fd-fail is the theme red");
assert.ok(/\.cmd-card\s*\{[^}]*--fd-warn:\s*var\(--accent-yellow\)/.test(block), "--fd-warn is the theme yellow");
assert.ok(/\.cmd-card\s*\{[^}]*--fd-ok:\s*var\(--accent-green\)/.test(block), "--fd-ok is the theme green");

// no raw colour in the block: a later light theme overrides the tokens, nothing else
const hex = block.match(/#[0-9a-fA-F]{3,8}\b/g);
assert.strictEqual(hex, null, "no hex colour in the block: " + (hex || []).join(" "));
assert.ok(!/outline\s*:\s*(none|0)\b/.test(block), "the focus ring is never removed");

// every class the card's builders emit has a rule in the block
const jsPath = path.resolve(process.argv[3]);
const js = fs.readFileSync(jsPath, "utf8");
const emitted = new Set(["fdmark", "fdring"]); // SVG marks built by the map, not class="" strings
for (const m of js.matchAll(/class="([^"]*)"/g)) {
  for (const t of m[1].replace(/\$\{[^}]*\}/g, " ").split(/\s+/)) if (/^fd-[a-z0-9-]+$/.test(t)) emitted.add(t);
}
assert.ok(emitted.has("fd-fight") && emitted.has("fd-fv") && emitted.has("fd-both") && emitted.has("fd-open"),
  "the builders' fight rail and bot panel classes are seen");
// The replay row and the aggro lanes are styled in the later Dashboard phase A block, which phase_a_css_check.js keeps complete;
// the ready check has its own block after it. A class the app emits with its rule there is covered too.
const blockOf = (head) => {
  const at = css.indexOf(head);
  if (at < 0) return "";
  const next = css.indexOf("/* ---------- ", at + head.length);
  return css.slice(at, next < 0 ? css.length : next);
};
const later = blockOf("/* ---------- Dashboard phase A: plan layer, fight replay, aggro lanes ---------- */")
  + blockOf("/* ---------- Ready check ---------- */")
  + blockOf("/* ---------- Fight history ---------- */");
const missing = [...emitted].filter((c) => !new RegExp("\\." + c + "(?![\\w-])").test(block)
  && !new RegExp("\\." + c + "(?![\\w-])").test(later));
assert.deepStrictEqual(missing, [], "classes without a rule: " + missing.join(", "));

// the verdict word takes its level's colour, like the prototype's chips
assert.ok(/\.fd-fight\.v-fail \.fd-fv \{[^}]*color:\s*var\(--fd-fail\)/.test(block), "a failing fight's word is red");
assert.ok(/\.fd-fight\.v-warn \.fd-fv \{[^}]*color:\s*var\(--fd-warn\)/.test(block), "a warning fight's word is yellow");
assert.ok(/\.fd-fight\.v-ok \.fd-fv \{[^}]*color:\s*var\(--fd-ok\)/.test(block), "a clean fight's word is green");

// the phone width is one column, in the spec's order
function blockAt(start) {
  let depth = 0;
  for (let i = start; i < block.length; i++) {
    if (block[i] === "{") depth++;
    else if (block[i] === "}" && --depth === 0) return block.slice(start + 1, i);
  }
  return "";
}
const phone = block.indexOf("@media (max-width: 760px)");
assert.ok(phone >= 0, "the phone breakpoint exists");
const phoneBody = blockAt(block.indexOf("{", phone));
assert.ok(phoneBody.length > 0, "the phone breakpoint has a body");
const bodyRule = phoneBody.match(/\.cmd-card\.fd-on \.cmd-body\s*\{([^}]*)\}/);
assert.ok(bodyRule, "the phone breakpoint restates the card body");
assert.ok(/grid-template-columns:\s*1fr\b|display:\s*block\b/.test(bodyRule[1]), "the phone body is one column");
assert.ok(/\.fd-rail\s*\{[^}]*display:\s*flex/.test(phoneBody) && /overflow-x:\s*auto/.test(phoneBody), "the rail scrolls sideways on a phone");
assert.ok(/\.fd-frames\s*\{[^}]*repeat\(2, minmax\(0, 1fr\)\)/.test(phoneBody), "the frames wrap two groups per row on a phone");
assert.ok(/\.fd-rail\s*\{[^}]*min-width:\s*0/.test(phoneBody), "the rail scrolls inside its column instead of widening the page");
assert.ok(/\.cmd-card\.fd-on \.cmd-maptools\s*\{[^}]*flex-wrap:\s*wrap/.test(phoneBody), "the map toolbar wraps inside the map on a phone");

// the debugger's map is at least as tall as the prototype's
const stage = block.match(/\.cmd-card\.fd-on \.cmd-stage\s*\{\s*height:\s*(\d+)px/);
assert.ok(stage && Number(stage[1]) >= 440, "the desktop map is at least 440 px tall");

console.log("fight debugger css ok");
