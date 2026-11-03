"use strict";
// Run by commandersjs_test.go under node: app.css carries the Dashboard phase A block (the plan layer and the raid layers on
// the map, the replay row and the aggro lanes in the Fight debugger; theme tokens only, the eight raid group colours the one
// exception, every class the builders emit, the phone row and the focus ring kept).
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const cssPath = path.resolve(process.argv[2]);
const css = fs.readFileSync(cssPath, "utf8");

const HEAD = "/* ---------- Dashboard phase A: plan layer, fight replay, aggro lanes ---------- */";
const at = css.indexOf(HEAD);
assert.ok(at >= 0, "the Dashboard phase A block starts with its header comment");
const nextHead = css.indexOf("/* ---------- ", at + HEAD.length);
const block = css.slice(at, nextHead < 0 ? css.length : nextHead);
assert.ok(!css.slice(at + HEAD.length).includes(HEAD), "the block is appended once");

// The eight raid group colours are the block's only raw colours, and they sit on the card's --grp-* properties.
const grpRule = /\.cmd-card\s*\{([^}]*)\}/.exec(block);
assert.ok(grpRule, "the card declares the group colours");
const grpStart = grpRule.index;
const grpDecls = grpRule[1].split(";").map((d) => d.trim()).filter(Boolean);
const grpColours = {};
for (const decl of grpDecls) {
  const m = /^(--grp-[0-7])\s*:\s*(#[0-9a-fA-F]{3,8})$/.exec(decl);
  if (m) grpColours[m[1]] = m[2].toLowerCase();
}
assert.deepStrictEqual(Object.keys(grpColours).sort(), ["--grp-0", "--grp-1", "--grp-2", "--grp-3", "--grp-4", "--grp-5", "--grp-6", "--grp-7"],
  "the card declares eight group colours: " + Object.keys(grpColours).join(", "));
const outside = block.slice(0, grpStart) + block.slice(grpStart + grpRule[0].length);
const hex = outside.match(/#[0-9a-fA-F]{3,8}\b/g);
assert.strictEqual(hex, null, "no raw colour outside the group colours: " + (hex || []).join(" "));
assert.ok(!/outline\s*:\s*(none|0)\b/.test(block), "the focus ring is never removed");

// Every class the plan layer, the preset row, the raid layers and the replay/lane builders emit has a rule in the block.
const classes = ["plan-route", "plan-ring", "plan-mob", "plan-stand", "plan-step", "plan-num", "plan-fight", "plan-stray",
  "plan-move", "fd-play", "fd-scrub", "fd-clock", "fd-cov", "fd-cursor", "fd-lanes", "fd-lane", "fd-lanel", "fd-lanebar",
  "fd-seg", "fd-lanesum", "fd-lanecap", "cmd-presets", "healtank", "nontank", "sg-0", "sg-7"];
const missing = classes.filter((c) => !new RegExp("\\." + c + "(?![\\w-])").test(block));
assert.deepStrictEqual(missing, [], "classes without a rule: " + missing.join(", "));

// The dive bar keeps the role readable on top of the red a bad segment turns.
for (const kind of ["healer", "dps"]) {
  const rule = new RegExp("\\.fd-seg\\.k-" + kind + "\\s*\\{([^}]*)\\}|\\.k-" + kind + "\\s*\\{([^}]*)\\}").exec(block);
  assert.ok(rule, "." + kind + " has a rule");
  assert.ok(/var\(--fd-fail\)/.test(rule[0]), "." + kind + " is drawn in the fail colour");
}

// The wake ring says it is scripted by its dash, distinct from the aggro ring.
assert.ok(/\.plan-ring\.wake\s*\{[^}]*stroke-dasharray/.test(block), "the wake ring is dashed");
assert.ok(/\.plan-ring\.aggro\s*\{[^}]*var\(--accent-red\)/.test(block), "the aggro ring uses the theme red");
assert.ok(/\.cmd-map \.plan-mob\b[^{]*\{[^}]*var\(--accent-red\)/.test(block), "the mob spawn dot uses the theme red");

// The raid layers: subgroup colours on the bot dots, the off-tank threat line red, the layout's healer line green.
for (let n = 0; n <= 7; n++) {
  assert.ok(new RegExp("\\.cmd-map \\.bot\\.sg-" + n + " \\.dot\\s*\\{[^}]*var\\(--grp-" + n + "\\)").test(block),
    "subgroup " + n + " fills its bot dot from --grp-" + n);
}
assert.ok(/line\.threat\.nontank[^{]*\{[^}]*var\(--fd-fail\)/.test(block), "a mob on a non-tank is drawn in the fail colour");
assert.ok(/line\.healtank[^{]*\{[^}]*var\(--role-healer\)/.test(block), "the healer-to-tank line is the healer's colour");
assert.ok(/line\.healtank[^{]*\{[^}]*stroke-dasharray/.test(block), "the healer-to-tank line is dashed");
assert.ok(/line\.healtank\.far[^{]*\{[^}]*var\(--fd-warn\)/.test(block), "an out-of-range healer line warns");
assert.ok(/line\.healtank\.dead[^{]*\{[^}]*opacity\s*:\s*\.?35/.test(block), "a dead healer line fades");

// Presets are chips; the chosen one is marked by the yellow border.
assert.ok(/\.cmd-presets\s*\{[^}]*flex-wrap:\s*wrap/.test(block), "the preset row wraps");
assert.ok(/\.cmd-presets button\[aria-pressed="true"\]\s*\{[^}]*var\(--accent-yellow\)/.test(block), "the chosen preset is marked");

// The legend names the new marks.
assert.ok(/\.cmd-legend i\.plan\s*\{/.test(block), "the plan has a legend swatch");
assert.ok(/\.cmd-legend i\.ring\.wake\s*\{/.test(block), "the aggro and wake rings have a red legend swatch");
assert.ok(/\.cmd-legend i\.nontank\s*\{/.test(block), "the non-tank threat line has a legend swatch");
assert.ok(/\.cmd-legend i\.healtank\s*\{/.test(block), "the healer-to-tank line has a legend swatch");

// A phone stacks each lane and gives the scrub the full row.
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
assert.ok(/\.fd-lane\s*\{[^}]*grid-template-columns:\s*1fr/.test(phoneBody), "a phone stacks the lane");
assert.ok(/\.fd-scrub\s*\{[^}]*flex-basis:\s*100%/.test(phoneBody), "a phone gives the scrub the full row");

console.log("dashboard phase a css ok");
