"use strict";
// Run by commandersjs_test.go under node: the run controls also show in the Fight debugger.
//  - a raid card in debug mode with controls enabled shows .cmd-controls not hidden, whose parent is .cmd-body;
//    the notes/threat/deaths panels stay hidden in debug mode, as before;
//  - on the Live paint it is back inside .cmd-panels;
//  - a non-raid bind keeps it hidden in both views;
//  - the debug grid carries the controls row under the raid frames, with one track per area row.
const assert = require("assert");
const fs = require("fs");
const vm = require("vm");
const css = fs.readFileSync(process.argv[2], "utf8");
const src = fs.readFileSync(process.argv[3], "utf8");

// ---- the debug grid: the controls row sits under the raid frames, full width
const body = css.match(/\.cmd-card\.fd-on \.cmd-body \{([^}]*)\}/);
assert.ok(body, "the debugger's card body rule exists");
const areas = body[1].match(/"[^"]*"/g);
assert.deepStrictEqual(areas, [
  '"run run run"',
  '"frames frames frames"',
  '"controls controls controls"',
  '"bosses bosses bosses"',
  '"rail centre roles"',
  '"rail live roles"',
  '"rail . roles"',
], "the controls row sits under the raid frames, above the fight sections");
const rowsDecl = body[1].match(/grid-template-rows:\s*([^;]*);/);
assert.ok(rowsDecl, "the body states its rows");
const tracks = rowsDecl[1].trim().split(/\s+/);
assert.deepStrictEqual(tracks, ["auto", "auto", "auto", "auto", "auto", "auto", "1fr"],
  "seven tracks for the seven area rows, only the closing one flexible");
assert.strictEqual(areas.length, tracks.length, "one track per area row");
assert.ok(/\.cmd-card\.fd-on \.cmd-controls \{[^}]*grid-area:\s*controls/.test(css), "the controls panel has the controls grid area in debug mode");
const phone = css.slice(css.indexOf("@media (max-width: 760px) {\n  .cmd-card.fd-on .cmd-body"));
assert.ok(/"frames" "controls" "bosses"/.test(phone.slice(0, 400)), "on a phone the controls follow the raid frames");

// ---- the move: like cmdPlaceLiveStrip, the controls panel moves into the body on the debug paint and home on Live
function grab(name) {
  const m = new RegExp(`^  function ${name}\\(`, "m").exec(src);
  assert.ok(m, name + " not found");
  return src.slice(m.index, src.indexOf("\n  }\n", m.index) + 4);
}
function node(cls, parent) {
  const n = { cls, parent: null, kids: [], get parentElement() { return this.parent; },
    get previousElementSibling() { const i = this.parent ? this.parent.kids.indexOf(this) : -1; return i > 0 ? this.parent.kids[i - 1] : null; },
    appendChild(c) { if (c.parent) c.parent.kids.splice(c.parent.kids.indexOf(c), 1); c.parent = this; this.kids.push(c); return c; },
    insertBefore(c, ref) { if (c.parent) c.parent.kids.splice(c.parent.kids.indexOf(c), 1); c.parent = this;
      const i = ref ? this.kids.indexOf(ref) : -1; if (i < 0) this.kids.push(c); else this.kids.splice(i, 0, c); return c; },
    querySelector(sel) { const c = sel.slice(1); const walk = (x) => { for (const k of x.kids) { if (k.cls === c || k.className === c) return k;
      const r = walk(k); if (r) return r; } return null; }; return walk(this); } };
  if (parent) parent.appendChild(n);
  return n;
}
const mcard = node("card"), mbody = node("cmd-body", mcard), mpanels = node("cmd-panels", mbody), mctl = node("cmd-controls", mpanels);
mctl.hidden = false;
const bosses = node("fd-bosses", mbody);
const mctx = { document: { createElement: () => node("") } };
vm.createContext(mctx);
vm.runInContext(grab("cmdPlaceControls"), mctx);
mctx.cmdPlaceControls(mcard, true); // the debugger paint: the panel joins the grid under the raid frames
assert.strictEqual(mctl.parentElement, mbody, "in the debugger the controls panel is a grid item of .cmd-body");
assert.strictEqual(mbody.kids.indexOf(mctl) + 1, mbody.kids.indexOf(bosses), "the controls sit ahead of the fight sections");
mctx.cmdPlaceControls(mcard, true);
assert.strictEqual(mctl.parentElement, mbody, "and stays there");
mctx.cmdPlaceControls(mcard, false); // back to Live: with the notes, threat and deaths
assert.strictEqual(mctl.parentElement, mpanels, "back on the Live page the controls return to .cmd-panels");

// ---- the paint: a raid card in debug mode shows the same panel; Live keeps the rest hidden only in debug
assert.ok(/cmdPlaceControls\(card, debug && raid && cmdControlOn\)/.test(src),
  "the card paint places the controls from the debug flag, the raid bind and the one controls answer");
assert.ok(/cmdPlaceControls\(card, debug && raid && on\)/.test(src),
  "the controls answer re-places the panel once it lands, for a card painted before the fetch replied");
assert.ok(/const ctl = card\.querySelector\('\.cmd-controls'\);\s*\n\s*if \(ctl\) ctl\.hidden = !on \|\| !raid;/.test(src),
  "the panel stays hidden while controls are off, and for a non-raid bind, in both views");
assert.ok(/if \(panels\) panels\.hidden = !raid \|\| debug;/.test(src),
  "the rest of .cmd-panels (notes, threat, deaths) stays hidden in debug mode");
assert.ok(/if \(on && raid\) \{ setHtml\(card\.querySelector\('\.cmd-controls-row'\), cmdControlsHtml\(bind\)\); cmdControlsBind\(card, bind\); \}/
  .test(src), "the debug paint fills the same buttons, binding and answer note as Live");

console.log("controls in debugger ok");
