"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's live events strip and the page's scroll position.
//  - app.css: in debug mode the strip has its own grid area under the map (not an auto-placed row of the rail column), with a
//    flexible closing row so a tall roles column cannot stretch the map's row; the phone column lists it after the map.
//  - the strip's rows: time column + wrapping text, a colour class by kind (k, o, r of the event), a bounded box that scrolls itself,
//    and a reader's scroll position kept across an update (newest first).
//  - the page must not scroll by itself: the repaint helpers that give the keyboard back to a control (fdSet, fdTabPaint) focus with
//    preventScroll, because a plain focus() scrolls the control into view and drags a scrolled page back up every 2 s repaint.
const assert = require("assert");
const fs = require("fs");
const vm = require("vm");
const css = fs.readFileSync(process.argv[2], "utf8");
const src = fs.readFileSync(process.argv[3], "utf8");

// ---- css
const body = css.match(/\.cmd-card\.fd-on \.cmd-body \{([^}]*)\}/);
assert.ok(body, "the debugger's card body rule exists");
assert.ok(/"rail centre roles"\s*"rail live roles"/.test(body[1]), "live sits under the centre column, between rail and roles");
assert.ok(/"frames frames frames"\s*"controls controls controls"/.test(body[1]), "controls sit under the raid frames, full width");
const rowsDecl = body[1].match(/grid-template-rows:\s*([^;]*);/);
assert.ok(rowsDecl, "the body states its rows");
const tracks = rowsDecl[1].trim().split(/\s+/);
assert.deepStrictEqual(tracks, ["auto", "auto", "auto", "auto", "auto", "auto", "1fr"],
  "seven tracks for the seven area rows, only the closing one flexible (a 1fr that is not last, or fewer tracks than rows, brings the stretch back)");
assert.strictEqual(body[1].match(/"[^"]*"/g).length, tracks.length, "one track per area row (run, frames, controls, bosses, centre, live, filler)");
assert.ok(/\.cmd-card\.fd-on \.cmd-live \{[^}]*grid-area:\s*live/.test(css), "the strip has the live grid area in debug mode");
assert.ok(/\.cmd-card\.fd-on \.cmd-controls \{[^}]*grid-area:\s*controls/.test(css), "the controls panel has the controls grid area in debug mode");
const phone = css.slice(css.indexOf("@media (max-width: 760px) {\n  .cmd-card.fd-on .cmd-body"));
assert.ok(/"centre" "live" "roles"/.test(phone.slice(0, 400)), "on a phone the strip follows the map column");
assert.ok(/"frames" "controls" "bosses"/.test(phone.slice(0, 400)), "on a phone the controls follow the raid frames");
const rows = css.match(/\.cmd-live-events \{([^}]*)\}/)[1];
assert.ok(/max-height:\s*260px/.test(rows) && /overflow-y:\s*auto/.test(rows), "the strip is bounded and scrolls on its own");
assert.ok(/font-size:\s*0\.82rem/.test(rows) && /line-height:\s*1\.35/.test(rows), "0.82rem text at line-height 1.35");
const row = css.match(/\.cmd-live-row \{([^}]*)\}/)[1];
assert.ok(/grid-template-columns:\s*4\.6em minmax\(0, 1fr\)/.test(row), "a fixed time column and a text column");
assert.ok(!/nowrap|ellipsis/.test(row), "a row wraps instead of truncating");
assert.ok(/\.cmd-live-row span \{[^}]*overflow-wrap:\s*anywhere/.test(css), "a long name breaks instead of widening the page");
assert.ok(/\.cmd-live-row\.lv-death span, \.cmd-live-row\.lv-wipe span \{ color: var\(--accent-red\)/.test(css), "death and wipe are red");
assert.ok(/\.cmd-live-row\.lv-boss span \{[^}]*#facc15/.test(css), "a boss cast is the feed's yellow");

// ---- the strip's code
function grab(name) {
  const m = new RegExp(`^  function ${name}\\(`, "m").exec(src);
  assert.ok(m, name + " not found");
  return src.slice(m.index, src.indexOf("\n  }\n", m.index) + 4);
}
const el = (cls) => ({ cls, scrollTop: 0, scrollHeight: 0, hidden: false, _h: undefined, html: "", children: [] });
const box = el("box"), wrap = el("wrap");
const rowH = 20; // every row is 20 px tall in this fake box; the box's own top is 0, a row's top is its index * 20 - scrollTop
box.getBoundingClientRect = () => ({ top: 0, bottom: 260 });
const card = { querySelector: (s) => (s === ".cmd-live-events" ? box : s === ".cmd-live" ? wrap : null) };
const ctx = {
  cmdCards: new Map([["C", card]]), cmdLiveEvents: { C: [] },
  esc: (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;"), cmdClock: (s) => "T" + s,
  setHtml: (n, html) => { if (n._h === html) return; n._h = html; n.html = html; const n2 = (html.match(/cmd-live-row /g) || []).length; n.scrollHeight = n2 * rowH;
    n.children = Array.from({ length: n2 }, (_, i) => ({ getBoundingClientRect: () => ({ top: i * rowH - n.scrollTop, bottom: (i + 1) * rowH - n.scrollTop }) })); },
};
vm.createContext(ctx);
vm.runInContext([grab("cmdLiveEventText"), grab("cmdLiveEventClass"), grab("cmdLiveRowHtml"), grab("cmdLivePaintEvents")].join("\n"), ctx);

const cls = (ev) => ctx.cmdLiveEventClass({ ev });
assert.strictEqual(cls({ k: "death", n: "A" }), "lv-death");
assert.strictEqual(cls({ k: "end", o: "wipe" }), "lv-wipe");
assert.notStrictEqual(cls({ k: "end", o: "kill" }), "lv-wipe", "a kill is not red");
assert.strictEqual(cls({ k: "cast", r: "b" }), "lv-boss", "a boss cast (r = b) is yellow");
assert.strictEqual(cls({ k: "cast", r: "e" }), "lv-other", "an elite cast stays muted");
assert.strictEqual(cls({ k: "engage" }), "lv-pull");
assert.strictEqual(cls({ k: "taunt" }), "lv-taunt");
const html = ctx.cmdLiveRowHtml({ at: 5, ev: { k: "cast", n: "<Bat>", s: "Unstable", r: "e" } });
assert.strictEqual(html, '<div class="cmd-live-row lv-other"><b>T5</b><span>cast &lt;Bat>: Unstable</span></div>', "time and text in their own cells, escaped");
assert.ok(!/T5/.test(ctx.cmdLiveEventText({ at: 5, ev: { k: "start" } })), "the text carries no time");

// scroll position: a reader scrolled down keeps the row they were on when a newer event is added on top, also when the
// 12-event cap drops the oldest at the same time (the total height then stays the same while every row moves down)
const ev = (n) => ({ at: n, ev: { k: "engage", n: "m" + n } });
ctx.cmdLiveEvents.C = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12].map(ev);
ctx.cmdLivePaintEvents("C");
assert.strictEqual(wrap.hidden, false);
assert.ok(box.html.indexOf("m12") < box.html.indexOf("m1<"), "newest first");
const topRow = () => { const r = box.children.findIndex((c) => c.getBoundingClientRect().bottom > 1); return ctx.cmdLiveEvents.C.slice().reverse()[r].at; };
box.scrollTop = 65; // the reader is on the 4th row (m9), partly scrolled
assert.strictEqual(topRow(), 9);
ctx.cmdLiveEvents.C.push(ev(13)); ctx.cmdLiveEvents.C.shift(); // a new event on top, the oldest dropped: same total height
ctx.cmdLivePaintEvents("C");
assert.strictEqual(topRow(), 9, "the reader still sees the same event after an update with the cap");
assert.strictEqual(box.scrollTop, 85, "the scroll moved by one row, the reader keeps their place");
box.scrollTop = 0; // at the top the box keeps showing the newest
ctx.cmdLiveEvents.C.push(ev(14));
ctx.cmdLivePaintEvents("C");
assert.strictEqual(box.scrollTop, 0, "at the top it stays at the top");
ctx.cmdLivePaintEvents("C"); // nothing new: no change
assert.strictEqual(box.scrollTop, 0);

// ---- a card first drawn on the Live page: ensureFightMount moves the strip out of .cmd-body to the card's end; the debugger must
// take it back into the body, or grid-area: live has no grid item to place
function node(cls, parent) {
  const n = { cls, parent: null, kids: [], get parentElement() { return this.parent; },
    get previousElementSibling() { const i = this.parent ? this.parent.kids.indexOf(this) : -1; return i > 0 ? this.parent.kids[i - 1] : null; },
    appendChild(c) { if (c.parent) c.parent.kids.splice(c.parent.kids.indexOf(c), 1); c.parent = this; this.kids.push(c); return c; },
    querySelector(sel) { const c = sel.slice(1); const walk = (x) => { for (const k of x.kids) { if (k.cls === c || k.className === c) return k; const r = walk(k); if (r) return r; } return null; }; return walk(this); } };
  if (parent) parent.appendChild(n);
  return n;
}
const mcard = node("card"), mbody = node("cmd-body", mcard), mlive = node("cmd-live", mbody);
const mctx = { document: { createElement: () => node("") } };
vm.createContext(mctx);
vm.runInContext([grab("ensureFightMount"), grab("cmdPlaceLiveStrip")].join("\n"), mctx);
// the Live render of a card: mountFightPanel's ensureFightMount puts the fights panel at the card's end and the strip after it
mctx.ensureFightMount(mcard);
assert.strictEqual(mlive.parentElement, mcard, "on the Live page the strip sits under the fights panel, outside .cmd-body (as before)");
mctx.cmdPlaceLiveStrip(mcard, true); // the same card, switched to the debugger
assert.strictEqual(mlive.parentElement, mbody, "in the debugger the strip is a grid item of .cmd-body again");
mctx.cmdPlaceLiveStrip(mcard, true);
assert.strictEqual(mlive.parentElement, mbody, "and stays there");
mctx.cmdPlaceLiveStrip(mcard, false); // back to Live
assert.strictEqual(mlive.parentElement, mcard, "back on the Live page the strip returns under the fights panel");
assert.strictEqual(mlive.previousElementSibling.className, "cmd-fights");
assert.ok(/cmdPlaceLiveStrip\(card, debug\);\s*\n\s*cmdLivePaintEvents\(key\)/.test(src), "the card paint places the strip before it paints the events");

// ---- the page must not scroll by itself
const calls = [];
const fdSetCtx = {
  document: { activeElement: null },
  setHtml: (n, h) => { n.html = h; },
  Array,
};
const button = { getAttribute: () => "7", hasAttribute: (a) => a === "data-fd-bot" };
const container = { contains: (n) => n === button, html: "", querySelectorAll: () => [{ getAttribute: () => "7", focus: (o) => calls.push(o) }] };
fdSetCtx.document.activeElement = button;
vm.createContext(fdSetCtx);
vm.runInContext(grab("fdSet"), fdSetCtx);
fdSetCtx.fdSet(container, "<new>");
assert.strictEqual(calls.length, 1, "fdSet gives the keyboard back to the rebuilt control");
assert.strictEqual(calls[0] && calls[0].preventScroll, true, "fdSet focuses without scrolling the page");

// every repaint helper that restores focus names preventScroll (the tab strip's restore is the other one)
const restores = [...src.matchAll(/\bback\.focus\(([^)]*)\)/g)];
assert.ok(restores.length >= 2, "both focus restores (fdSet, fdTabPaint) are found");
restores.forEach((m) => assert.ok(/preventScroll:\s*true/.test(m[1]), "a repaint's focus restore must pass preventScroll: " + m[0]));

console.log("live layout and scroll ok");
