"use strict";
// Run by commandersjs_test.go under node: the run controls also show in the Fight debugger.
//  - a raid card in debug mode with controls enabled shows .cmd-controls not hidden, whose parent is .cmd-body;
//    the notes/threat/deaths panels stay hidden in debug mode, as before;
//  - on the Live paint it is back inside .cmd-panels;
//  - a non-raid bind keeps it hidden in both views;
//  - a late control answer only applies to the newest paint (debug then Live while the fetch is in flight: one fetch,
//    the Live parent, the Live bind);
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

// ---- the race: a late control answer only touches the newest paint
// The debugger paints, then the Live paint, while the one control fetch is still in flight; the answer lands after.
// Exactly one fetch happens, the controls end in the Live parent (.cmd-panels), and the buttons carry the Live
// paint's bind, bound once.
assert.ok(/const ctlGen = card\._ctlGen = \(card\._ctlGen \|\| 0\) \+ 1;/.test(src),
  "the paint stamps the card before asking, so a late answer knows its paint");
assert.ok(/cmdControlAsk\(on => \{\n\s*if \(card\._ctlGen !== ctlGen\) return;/.test(src),
  "a control answer older than the newest paint yields");
const askSrc = grab("cmdControlAsk");
assert.ok(askSrc.includes("cmdControlAsking"), "paints while unanswered share the one pending control fetch");
const cbSrc = src.match(/cmdControlAsk\(on => \{\n([\s\S]*?)\n    \}\);/);
assert.ok(cbSrc, "the paint's control callback is found");
const paintCtl = new Function("card", "bind", "debug", "raid", "on", "ctlGen",
  "setHtml", "cmdControlsHtml", "cmdControlsBind", "cmdPlaceControls", cbSrc[1]);
(async () => {
  let fetchCalls = 0, resolveControl = null;
  const box = {
    fetch: (url) => {
      assert.strictEqual(url, "/api/v1/control", "the shared fetch asks the control flag");
      fetchCalls++;
      return new Promise((res) => { resolveControl = res; });
    },
    jsonOrThrow: (r) => r,
  };
  vm.createContext(box);
  vm.runInContext("let cmdControlOn = null;\nlet cmdControlAsking = null;\n" + askSrc, box);
  vm.runInContext("function __ctlFlush() { return cmdControlAsking; }", box);
  const acard = node("cmd-card"), abody = node("cmd-body", acard),
    apanels = node("cmd-panels", abody), actl = node("cmd-controls", apanels);
  node("cmd-controls-row", actl);
  node("fd-bosses", abody);
  actl.hidden = true;
  const boundBinds = [], places = [];
  const stubs = [
    (el, html) => { el._html = html; },
    (bind) => "row:" + bind.commander,
    (card, bind) => { boundBinds.push(bind); },
    (card, inDebug) => { places.push(inDebug); mctx.cmdPlaceControls(card, inDebug); },
  ];
  const bindDebug = { commander: "debug-raid" }, bindLive = { commander: "live-raid" };
  const genDebug = acard._ctlGen = (acard._ctlGen || 0) + 1; // the debugger paint
  box.cmdControlAsk((on) => paintCtl(acard, bindDebug, true, true, on, genDebug, ...stubs));
  const genLive = acard._ctlGen = (acard._ctlGen || 0) + 1; // the Live paint, before the answer lands
  box.cmdControlAsk((on) => paintCtl(acard, bindLive, false, true, on, genLive, ...stubs));
  assert.strictEqual(fetchCalls, 1, "the debug and Live paints share the one control fetch");
  resolveControl({ enabled: true });
  await box.__ctlFlush();
  assert.strictEqual(actl.parentElement, apanels, "the late answer leaves the controls in the Live parent");
  assert.deepStrictEqual(places, [false], "only the Live paint places the panel");
  assert.deepStrictEqual(boundBinds, [bindLive], "the buttons carry the Live paint's bind, bound once");
  console.log("controls late-answer race ok");
})().catch((e) => { console.error(e); process.exit(1); });

console.log("controls in debugger ok");
