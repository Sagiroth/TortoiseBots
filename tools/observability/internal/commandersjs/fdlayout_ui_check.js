"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's panel layouts in app.js (web/static/js/app.js) over the fake DOM. Every panel of
// the card template carries data-fd-panel once; a layout puts an order, a span and a hide mark on the panels of a Fight debugger card (and
// nothing on a card with no layout: no fd-custom class); the live bar is never a panel and never inside a hidden one; the arrange dialog
// escapes every label and layout name; the picker, Save as, the 20-layout cap, the two-click Delete and a link's Shared layout change the
// state the way the brief says and never touch storage for a link; the card is never rebuilt (only styles move, a map is redrawn); the
// stylesheet rules and the page wiring are pinned.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const { createDocument } = require("./fakedom.js");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");
const jsDir = path.dirname(appPath);
const webDir = path.join(jsDir, "..", "..");
const css = fs.readFileSync(path.join(webDir, "static", "css", "app.css"), "utf8");
const indexHtml = fs.readFileSync(path.join(webDir, "index.html"), "utf8");
const FdLayout = require(path.join(jsDir, "fdlayout.js"));
const FdView = require(path.join(jsDir, "fdview.js"));

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}
const J = (v) => JSON.parse(JSON.stringify(v));

// ---- the template: every panel of the layout table is tagged once, and nothing else is
{
  const ids = Array.from(src.matchAll(/data-fd-panel="([a-z]+)"/g), (m) => m[1]);
  assert.deepStrictEqual(ids.slice().sort(), FdLayout.PANELS.map((p) => p.id).sort(), "the template tags each of the 15 panels exactly once: " + ids.join(","));
  assert.ok(/<div class="fd-mech" data-fd-panel="mech"/.test(src), "the boss mechanics section is its own panel");
  assert.ok(/<details class="fd-underbox" data-fd-panel="under">/.test(src), "and so is the underperforming bots box");
  assert.ok(/<div class="fd-railbosses" data-fd-panel="bosses"/.test(src) && /<div class="fd-railcol" data-fd-panel="rail" hidden>/.test(src), "the boss abilities and the fight rail are two panels");
  assert.ok(/class="cmd-panel cmd-controls" data-fd-panel="controls"/.test(src), "the run controls are tagged where the debug paint moves them from");
  assert.ok(!/class="fd-livebar"[^>]*data-fd-panel/.test(src), "the live bar is not a panel");
  assert.ok(/<div class="fd-railwrap" hidden>/.test(src), "the rail and the boss abilities share a wrapper the custom grid dissolves; it is hidden until the debugger paint shows it");
  assert.ok(/\.fd-runtl, \.fd-raid, \.fd-railwrap, \.fd-railcol,/.test(src) && src.includes("el.hidden = !debug"), "the debug paint shows and hides it with the rail");
  // The Raids and Dungeons live cards are built from this template and never take the debugger paint: every element a CSS rule outside the
  // debugger (no .fd-on) puts in a grid area must be hidden in the template, or it becomes a visible grid item and adds implicit tracks.
  const cardTpl = src.slice(src.indexOf('  function newCmdCard(key)'), src.indexOf('bindCmdMap(card, key);', src.indexOf('  function newCmdCard(key)')));
  assert.ok(cardTpl.includes('class="fd-railwrap"'), 'the template slice is the card');
  const areaRules = Array.from(fs.readFileSync(path.join(path.dirname(appPath), "..", "css", "app.css"), "utf8").matchAll(/^((?:\.[\w-]+(?:, )?)+) \{[^}]*grid-area:/gm), (m) => m[1]).join(", ").split(", ");
  assert.ok(areaRules.includes(".fd-railwrap") === false, "the wrapper's grid-area rule is scoped to .fd-on");
  assert.ok(areaRules.length >= 3, "the scan sees the unscoped grid-area rules: " + areaRules.join(","));
  areaRules.forEach((sel) => {
    const cls = sel.slice(1), tag = new RegExp(`<[a-z]+ class="${cls}[ "][^>]*>`).exec(cardTpl);
    if (!tag) return; // not part of a card (the overview's own markup)
    assert.ok(/\shidden[\s>]/.test(tag[0]), cls + " has a grid-area outside .fd-on, so the card template must hide it: " + tag[0]);
  });
}

// ---- the page's bits the layout code touches
const dom = createDocument();
const memStore = () => { const m = {}; const writes = []; return { m, writes, getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { writes.push(k); m[k] = String(v); } }; };
let store = memStore();
const timers = [];
const ctx = { cmdPageMode: () => "debug", console, Object, Math, Date, JSON, String, Number, Array, FdLayout, document: dom.document,
  setTimeout: (f, ms) => { timers.push({ f, ms }); return timers.length; }, clearTimeout: (h) => { if (h && timers[h - 1]) timers[h - 1].dead = true; },
  fdPageStore: store, cmdCards: new Map(), fdPage: { open: null }, fdViewHash: (k) => { hashes.push(k); vm.runInContext("fdLay.written = ((c) => (c ? FdLayout.encode(c) : ''))(fdLayCurrent())", ctx); }, // what the real one records
  cmdSched: { shown: (k) => k !== "off", mark: (k) => { marked.push(k); } }, drawCmdMap: (k) => { drawn.push(k); } };
const hashes = [], drawn = [], marked = [];
vm.createContext(ctx);
vm.runInContext([grab("esc"), src.slice(src.indexOf("  const fdLay = {"), src.indexOf("\n", src.indexOf("  fdLay.sel = fdLay.state.active")))].concat(
  ["fdLayCurrent", "fdLayApply", "fdLayApplyAll", "fdLayOptionsHtml", "fdLayRowsHtml", "fdLayPaint", "fdLayChanged", "fdLayDisarm", "fdLayPersist", "fdLayPick",
    "fdLayAct", "fdLaySave", "fdLayDelete", "fdLaySetShared", "fdLayChip"].map(grab)).join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const reset = (st) => {
  store = memStore();
  run("fdLay.state = FdLayout.load(fdPageStore); fdLay.shared = null; fdLay.sel = fdLay.state.active ? 'n:' + fdLay.state.active : ''; fdLay.edit = null; fdLay.note = ''; fdLay.armed = 0; fdLay.focus = ''; fdLay.written = '';", { fdPageStore: store });
  if (st) { store.m[FdLayout.STORAGE_KEY] = JSON.stringify(st); run("fdLay.state = FdLayout.load(fdPageStore); fdLay.sel = fdLay.state.active ? 'n:' + fdLay.state.active : '';"); }
  hashes.length = 0; drawn.length = 0; marked.length = 0; timers.length = 0; store.writes.length = 0;
};

// A debugger card as the fake DOM can hold it: every panel under the body (as the grid sees them), the live bar and the range bar inside the
// timeline panel, exactly where the template puts them.
function makeCard(on) {
  const el = (cls) => dom.element("div", null, cls);
  const card = el("card cmd-card" + (on === false ? "" : " fd-on"));
  const body = el("cmd-body");
  card.appendChild(body);
  const p = {};
  FdLayout.PANELS.forEach((d) => { const x = el("p-" + d.id); x.setAttribute("data-fd-panel", d.id); body.appendChild(x); p[d.id] = x; });
  const tl = p.timeline;
  tl.classList.add("fd-timeline");
  const bar = el("fd-livebar"), rb = el("fd-rangebar");
  tl.appendChild(bar); tl.appendChild(rb);
  const stage = el("cmd-stage"); p.map.appendChild(stage);
  return { card, body, p, bar, rb };
}
const attrs = (x) => Object.keys(x.attributes);
const LAY = FdLayout.normalize({ panels: [{ id: "map", span: 12 }, { id: "timeline", span: 12 }] });

// ---- a card with no layout is untouched: no fd-custom class, no style, no mark (the default grid is the stylesheet's alone)
{
  reset();
  const c = makeCard();
  assert.strictEqual(run("fdLayCurrent()"), null, "no layout in force");
  assert.strictEqual(run("fdLayApply(card)", { card: c.card }), false, "nothing to do");
  assert.ok(!c.card.classList.contains("fd-custom"), "no fd-custom class: today's CSS runs untouched");
  FdLayout.PANELS.forEach((d) => assert.deepStrictEqual([Object.keys(c.p[d.id].style), attrs(c.p[d.id]).filter((k) => k !== "data-fd-panel")], [[], []], d.id + " has no order, span or hide mark"));
}

// ---- applying ma.12~tl.12: --fd-span 12 and order 0 on the map, no hide mark anywhere; the card is not rebuilt
{
  reset();
  const c = makeCard();
  run("fdLay.edit = LAY", { LAY });
  assert.strictEqual(run("fdLayApply(card)", { card: c.card }), true);
  assert.ok(c.card.classList.contains("fd-custom"), "the card is custom");
  assert.strictEqual(c.p.map.style["--fd-span"], "12");
  assert.strictEqual(c.p.map.style.order, "0", "the map is first");
  assert.strictEqual(c.p.timeline.style.order, "1");
  assert.strictEqual(c.p.run.style.order, "2", "the rest follow in default order");
  assert.strictEqual(c.p.run.style["--fd-span"], "12");
  assert.strictEqual(c.p.rail.style["--fd-span"], "3", "a panel the layout does not name keeps its default width");
  FdLayout.PANELS.forEach((d) => assert.ok(!("data-fd-hide" in c.p[d.id].attributes), d.id + " is not hidden"));
  assert.strictEqual(c.body.children.length, 15, "no panel was added or removed");
  assert.strictEqual(run("fdLayApply(card)", { card: c.card }), false, "the same layout again changes nothing (no redraw)");
  // a hide mark, then a layout without it
  run("fdLay.edit = FdLayout.show(fdLay.edit, 'auras', false)");
  assert.strictEqual(run("fdLayApply(card)", { card: c.card }), true);
  assert.ok("data-fd-hide" in c.p.auras.attributes, "a hidden panel carries data-fd-hide");
  assert.ok(!("data-fd-hide" in c.p.map.attributes));
  run("fdLay.edit = FdLayout.show(fdLay.edit, 'auras', true)");
  run("fdLayApply(card)", { card: c.card });
  assert.ok(!("data-fd-hide" in c.p.auras.attributes), "shown again: the mark is gone");
  // the layout never un-hides a panel the render hid
  c.p.meters.hidden = true;
  run("fdLay.edit = FdLayout.show(fdLay.edit, 'meters', true); fdLay.edit = FdLayout.move(fdLay.edit, 'meters', -1)");
  run("fdLayApply(card)", { card: c.card });
  assert.strictEqual(c.p.meters.hidden, true, "a panel the render hides stays hidden");
  // back to the default: every trace is removed
  run("fdLay.edit = null");
  assert.strictEqual(run("fdLayApply(card)", { card: c.card }), true);
  assert.ok(!c.card.classList.contains("fd-custom"));
  FdLayout.PANELS.forEach((d) => assert.deepStrictEqual([Object.keys(c.p[d.id].style), attrs(c.p[d.id]).filter((k) => k !== "data-fd-panel")], [[], []], d.id + " is clean again"));
}

// ---- only a Fight debugger card takes a layout: the Live page's card is cleared (an order would leak into its own grid)
{
  reset();
  const c = makeCard();
  run("fdLay.edit = LAY", { LAY });
  run("fdLayApply(card)", { card: c.card });
  assert.ok(c.p.map.style.order === "0");
  c.card.classList.remove("fd-on"); // the card goes back to Live
  assert.strictEqual(run("fdLayApply(card)", { card: c.card }), true);
  assert.ok(!c.card.classList.contains("fd-custom") && !("order" in c.p.map.style) && !("--fd-span" in c.p.map.style), "no layout outlives the debugger");
  const live = makeCard(false);
  assert.strictEqual(run("fdLayApply(card)", { card: live.card }), false, "a Live card is never given a layout");
  assert.ok(!live.card.classList.contains("fd-custom"));
}

// ---- LIVE: the live bar is not a panel; a layout keeps it outside every panel (so hiding the Timeline hides nothing of it), the default puts it back
{
  reset();
  const c = makeCard();
  c.bar.hidden = false; // the live paint has shown it
  const lay = FdLayout.show(FdLayout.normalize({ panels: [{ id: "map", span: 12 }] }), "timeline", false);
  run("fdLay.edit = lay", { lay });
  run("fdLayApply(card)", { card: c.card });
  assert.ok(!("data-fd-panel" in c.bar.attributes) && !("data-fd-hide" in c.bar.attributes), "the live bar has no panel id and no hide mark");
  assert.strictEqual(c.bar.parentElement, c.card, "the bar sits at the top of the card");
  assert.ok(c.card.children.indexOf(c.bar) < c.card.children.indexOf(c.body), "above the panels");
  assert.ok("data-fd-hide" in c.p.timeline.attributes, "the Timeline panel is hidden ...");
  for (let n = c.bar; n; n = n.parentElement) assert.ok(!("data-fd-hide" in n.attributes), "... but nothing around the live bar is");
  assert.strictEqual(c.bar.hidden, false, "the bar's own shown state is not touched");
  assert.ok(c.card.querySelector(".fd-livebar") === c.bar, "the live paint still finds it");
  run("fdLay.edit = null");
  run("fdLayApply(card)", { card: c.card });
  assert.strictEqual(c.bar.parentElement, c.p.timeline, "the default grid has it back in the Timeline panel");
  assert.deepStrictEqual(c.p.timeline.children.map((x) => x.className), ["fd-livebar", "fd-rangebar"], "before the range bar, as the template has it");
}

// ---- every card of the page changes at once; only a card that changed gets its map drawn again (a card off screen is marked)
{
  reset();
  const a = makeCard(), b = makeCard(), l = makeCard(false);
  run("cmdCards.clear(); cmdCards.set('A', a); cmdCards.set('off', b); cmdCards.set('L', l);", { a: a.card, b: b.card, l: l.card });
  run("cmdCards.set('A', a); cmdCards.set('off', b); cmdCards.set('L', l);");
  run("fdLay.edit = LAY; fdLayApplyAll();", { LAY });
  assert.deepStrictEqual(drawn, ["A"], "the visible debugger card's map is drawn again");
  assert.deepStrictEqual(marked, ["off"], "an off-screen one is drawn when it is back");
  drawn.length = 0; marked.length = 0;
  run("fdLayApplyAll()");
  assert.deepStrictEqual([drawn, marked], [[], []], "nothing changed: no redraw, no work");
  assert.ok(a.card.classList.contains("fd-custom") && b.card.classList.contains("fd-custom") && !l.card.classList.contains("fd-custom"));
  run("cmdCards.clear()");
}

// ---- the dialog: labels and names are escaped
{
  reset();
  const evil = "<img src=x onerror=1>";
  const st = FdLayout.put(FdLayout.emptyState(), evil, null).state;
  const opts = run("fdLayOptionsHtml(st, shared, sel, false)", { st, shared: LAY, sel: "n:" + evil });
  assert.ok(!/<img/i.test(opts), "the layout name is escaped in the picker: " + opts);
  assert.ok(opts.includes("&lt;img src=x onerror=1&gt;") && opts.includes('value="n:&lt;img'), "as text and in the value");
  assert.ok(opts.includes('<option value="">Default</option>') && opts.includes('<option value="~">Shared layout</option>'), "Default and Shared layout are in it");
  assert.ok(/value="n:[^"]*" selected/.test(opts), "the picked one is selected");
  assert.ok(!run("fdLayOptionsHtml(st, null, '', false)", { st }).includes("Shared layout"), "no Shared layout without a link");
  assert.ok(run("fdLayOptionsHtml(st, null, '', true)", { st }).includes("Unsaved changes"), "an edit shows as Unsaved changes");
  const rows = run("fdLayRowsHtml(l)", { l: FdLayout.defaults() });
  assert.strictEqual((rows.match(/<li /g) || []).length, 15, "one row per panel");
  assert.ok(rows.includes('data-fd-up="map"') && rows.includes('data-fd-down="map"') && rows.includes('data-fd-span="map"') && rows.includes('data-fd-show="map"'), "↑ ↓, width and Show per panel");
  assert.ok(rows.includes("Boss mechanics") && rows.includes("Run controls"), "labels from the table");
  assert.ok(/data-fd-up="run" aria-label="Move Run timeline up" disabled/.test(rows) && /data-fd-down="mech" aria-label="Move Boss mechanics down" disabled/.test(rows), "the ends cannot move further");
  FdLayout.SPANS.forEach((n) => assert.ok(rows.includes(">" + FdLayout.SPAN_LABELS[n] + "</option>")));
  assert.ok(/data-fd-show="map" checked/.test(rows) && !/data-fd-show="auras" checked/.test(run("fdLayRowsHtml(l)", { l: FdLayout.show(null, "auras", false) })), "Show is checked unless hidden");
  // the painted dialog (fake DOM keeps what was set)
  const d = dom.element("dialog", "fd-arrange", "fd-arrange");
  const mk = (cls, attr) => { const x = dom.element("div", null, cls); if (attr) x.setAttribute(attr, ""); d.appendChild(x); return x; };
  const sel = mk("", "data-fd-lay"), list = mk("fd-arr-list"), del = mk("", "data-fd-del"), note = mk("fd-arr-note");
  dom.document.body.appendChild(d);
  run("fdLay.state = st; fdLay.sel = 'n:' + evil;", { st, evil });
  run("fdLayPaint()");
  assert.ok(!/<img/i.test(sel._html) && !/<img/i.test(list._html + note.textContent), "the painted dialog carries no markup of the name");
  assert.strictEqual(del.disabled, false, "a saved layout can be deleted");
  assert.strictEqual(del.textContent, "Delete");
  d.remove();
}

// ---- state: pick, edit, Save as, the cap, Delete (two clicks), a link's Shared layout, a blocked storage
{
  reset();
  const named = (n, l) => FdLayout.put(FdLayout.emptyState(), n, l).state;
  // a pick of a saved name makes it active and stores the choice; it applies to the cards
  const a = makeCard();
  run("cmdCards.clear(); cmdCards.set('A', a);", { a: a.card });
  reset({ active: "", layouts: { wide: LAY, other: FdLayout.defaults() } });
  run("cmdCards.set('A', a);");
  run("fdLayPick('n:wide')");
  assert.strictEqual(run("fdLay.sel"), "n:wide");
  assert.strictEqual(JSON.parse(store.m[FdLayout.STORAGE_KEY]).active, "wide", "the choice is stored");
  assert.ok(a.card.classList.contains("fd-custom") && a.p.map.style.order === "0", "and applied");
  assert.deepStrictEqual(hashes, [], "no open tab: no hash to write");
  run("fdPage.open = 'T'; fdLayPick('n:other'); fdPage.open = null;");
  assert.deepStrictEqual(hashes, ["T"], "a change rewrites the open card's link (fdViewHash)");
  // an edit becomes the unsaved overlay; the saved layout and storage are not changed
  store.writes.length = 0;
  run("fdLayAct('up', 'rail')");
  assert.ok(run("fdLay.edit") && run("fdLayCurrent()").panels[3].id === "rail", "↑ moves the rail up past the boss abilities");
  assert.strictEqual(store.writes.length, 0, "an edit alone writes nothing to storage");
  run("fdLayAct('span', 'map', '12')");
  assert.strictEqual(run("fdLayCurrent()").panels.find((e) => e.id === "map").span, 12);
  run("fdLayAct('show', 'auras', false)");
  assert.strictEqual(run("fdLayCurrent()").panels.find((e) => e.id === "auras").hide, true);
  run("fdLayAct('down', 'zz')");
  run("fdLayAct('nonsense', 'map')");
  assert.strictEqual(run("fdLayCurrent()").panels.length, 15, "an unknown id or op changes nothing");
  // Save as: a trimmed name, the edit becomes it, storage written
  run("fdLaySave('  mine  ')");
  assert.strictEqual(run("fdLay.sel"), "n:mine");
  assert.strictEqual(run("fdLay.edit"), null);
  const saved = JSON.parse(store.m[FdLayout.STORAGE_KEY]);
  assert.deepStrictEqual([saved.active, Object.keys(saved.layouts).sort()], ["mine", ["mine", "other", "wide"]]);
  assert.strictEqual(saved.layouts.mine.panels.find((e) => e.id === "auras").hide, true, "the edit was saved");
  // a refused name says why and changes nothing
  const before = JSON.stringify(run("fdLay.state"));
  run("fdLaySave('" + "x".repeat(41) + "')");
  assert.ok(/1 to 40/.test(run("fdLay.note")), "the note says why: " + run("fdLay.note"));
  assert.strictEqual(JSON.stringify(run("fdLay.state")), before);
  // the cap: the 21st layout is refused
  let big = FdLayout.emptyState();
  for (let i = 0; i < 20; i++) big = FdLayout.put(big, "L" + i, null).state;
  reset(big);
  run("fdLaySave('one more')");
  assert.strictEqual(run("fdLay.note"), "At most 20 layouts");
  assert.strictEqual(Object.keys(run("fdLay.state.layouts")).length, 20);
  // Delete asks twice; the timer un-arms it after 3 s
  reset({ active: "k", layouts: { k: LAY, j: LAY } });
  run("fdLayDelete()");
  assert.ok(run("fdLay.armed") > 0 && Object.keys(run("fdLay.state.layouts")).length === 2, "the first click only asks");
  assert.strictEqual(timers[timers.length - 1].ms, 3000, "for 3 s");
  timers[timers.length - 1].f();
  assert.strictEqual(run("fdLay.armed"), 0, "after the 3 s the ask is off");
  run("fdLayDelete()");
  run("fdLayDelete()");
  assert.deepStrictEqual(Object.keys(run("fdLay.state.layouts")), ["j"], "the second click within the time deletes it");
  assert.strictEqual(run("fdLay.sel"), "", "the default grid is back");
  assert.strictEqual(JSON.parse(store.m[FdLayout.STORAGE_KEY]).active, "");
  run("fdLayDelete()");
  assert.strictEqual(run("fdLay.armed"), 0, "with no saved layout picked Delete does nothing");
  // a link's layout: the Shared layout, unsaved; the saved layouts and the stored active are not touched; no hash write
  reset({ active: "k", layouts: { k: LAY } });
  run("fdPage.open = 'T'");
  run("fdLaySetShared(l)", { l: FdLayout.normalize({ panels: [{ id: "events", span: 12 }] }) });
  assert.strictEqual(run("fdLay.sel"), "~");
  assert.strictEqual(run("fdLayCurrent().panels[0].id"), "events");
  assert.deepStrictEqual([store.writes.length, hashes.length], [0, 0], "a link writes nothing to storage and no hash");
  assert.strictEqual(JSON.parse(store.m[FdLayout.STORAGE_KEY]).active, "k", "the viewer's own active layout is kept");
  run("fdLayAct('down', 'events')");
  assert.strictEqual(run("fdLayCurrent().panels[0].id"), "run", "an edit of the shared layout is an unsaved edit of it");
  // the router reads back what this page wrote (written = the layout of the last hash write): that is not a link, nothing switches
  run("fdLaySetShared(l)", { l: run("fdLay.edit") });
  assert.ok(run("fdLay.edit"), "the page's own hash (the edit's code) read again changes nothing");
  assert.strictEqual(run("fdLay.written"), FdLayout.encode(run("fdLay.edit")), "the hash write recorded the layout it carried");
  run("fdLaySetShared(l)", { l: FdLayout.normalize({ panels: [{ id: "events", span: 12 }] }) });
  assert.strictEqual(run("fdLay.edit"), null, "a link opened again is that layout again");
  run("fdLaySetShared(null)");
  assert.strictEqual(run("fdLay.sel"), "~", "no layout in the link: nothing changes");
  run("fdLayPick('n:k')");
  assert.strictEqual(run("fdLay.sel"), "n:k", "the viewer can pick their own again");
  run("fdLayPick('~')");
  assert.strictEqual(run("fdLay.sel"), "~", "and the link's layout is still there to pick");
  // a link whose layout equals the saved one in force (or the unsaved edit) is still the Shared layout, and stays selectable
  run("fdLayPick('n:k'); fdLay.written = ''"); // a page just opened from the link has written nothing yet
  run("fdLaySetShared(l)", { l: LAY });
  assert.strictEqual(run("fdLay.sel"), "~", "a link equal to the saved layout in force is installed as Shared");
  assert.strictEqual(FdLayout.encode(run("fdLay.shared")), FdLayout.encode(LAY));
  run("fdLayPick('')");
  assert.strictEqual(run("fdLay.sel"), "", "switching away ...");
  run("fdLayPick('~')");
  assert.strictEqual(run("fdLay.sel"), "~", "... the Shared layout can be picked again");
  assert.deepStrictEqual(Object.keys(run("fdLay.state.layouts")), ["k"], "and no saved layout was added");
  run("fdLayPick('n:k'); fdLayAct('down', 'run')");
  const editCode = FdLayout.encode(run("fdLay.edit"));
  run("fdLay.written = ''; fdLaySetShared(l)", { l: run("fdLay.edit") });
  assert.strictEqual(run("fdLay.sel"), "~", "a link equal to the unsaved edit is installed as Shared too (the edit is dropped)");
  assert.strictEqual(run("fdLay.edit"), null);
  assert.strictEqual(FdLayout.encode(run("fdLay.shared")), editCode);
  // the page's own hash of a saved layout read back does not flip it to Shared
  run("fdLayPick('n:k')");
  assert.strictEqual(run("fdLay.written"), FdLayout.encode(LAY), "the page wrote the saved layout's code");
  run("fdLaySetShared(l)", { l: LAY });
  assert.strictEqual(run("fdLay.sel"), "n:k", "the page's own link (the saved layout's code) does not turn it into a Shared layout");
  run("fdPage.open = null");
  // a blocked storage: the layout works for the session and says so
  const blocked = { getItem() { throw new Error("no"); }, setItem() { throw new Error("no"); } };
  reset();
  run("fdPageStore = blocked; fdLay.state = FdLayout.emptyState();", { blocked });
  run("fdLayAct('up', 'rail'); fdLaySave('s')");
  assert.strictEqual(run("fdLay.sel"), "n:s", "it still works");
  assert.ok(/blocks storage/.test(run("fdLay.note")), "and says it is not kept: " + run("fdLay.note"));
  run("fdPageStore = realStore", { realStore: store });
}

// ---- the page wiring: the route, the hash, the button, the order of the scripts, the versions
{
  assert.ok(/lw: live \? fd\.liveWin : null, lay: curLay,/.test(grab("fdViewHash")) && /const curLay = fdLayCurrent\(\);/.test(grab("fdViewHash")), "the hash writes the layout in force");
  assert.ok(/fdLay\.written = curLay \? FdLayout\.encode\(curLay\) : '';/.test(grab("fdViewHash")), "and records which layout it carried (the router skips only that one)");
  assert.ok(/if \(fight && r\.fdq && r\.fdq\.lay\) fdLaySetShared\(r\.fdq\.lay\);/.test(grab("cmdRouteTab")), "a link's layout is the page's Shared layout");
  assert.ok(/fdLayApply\(card\);[^\n]*\n\s*cmdPlaceLiveStrip\(card, debug\);/.test(grab("cmdRenderCard")), "every card render applies it before the map is drawn");
  assert.ok(grab("cmdRenderCard").indexOf("fdLayApply(card)") < grab("cmdRenderCard").indexOf("drawCmdMap(key)"), "and the map draw follows (it reads its new size)");
  assert.ok(grab("fdStopCard").includes("fdLayApply(card)"), "a card that leaves the debugger drops its layout");
  assert.ok(/panels\.hidden = m !== 'debug'/.test(grab("cmdPaintPageMode")), "the Panels button is shown only in the debugger");
  assert.ok(/<button type="button" class="btn" id="cmd-fd-panels" hidden /.test(indexHtml), "the button is in the view bar, hidden to begin with");
  assert.ok(indexHtml.indexOf('id="cmd-page-mode"') < indexHtml.indexOf('id="cmd-fd-panels"') && indexHtml.indexOf('id="cmd-fd-panels"') < indexHtml.indexOf("cmd-refresh-bar"), "inside .cmd-view-bar");
  assert.ok(/getElementById\('cmd-fd-panels'\);\s*\n\s*if \(arrange\) arrange\.addEventListener\('click', fdLayOpen\);/.test(src), "it opens the dialog");
  assert.ok(src.includes("d.showModal()") && src.includes("document.createElement('dialog')"), "a native dialog, shown modal (Escape closes it)");
  assert.ok(!/window\.prompt|\bprompt\(/.test(src.slice(src.indexOf("  const fdLay = {"), src.indexOf("  function fdLaySetShared"))), "no window.prompt: Save as has its own input");
  assert.ok(!/fetch\(|setInterval\(|EventSource|WebSocket/.test(src.slice(src.indexOf("  const fdLay = {"), src.indexOf("  function fdLaySetShared") + 900)), "layouts add no request and no poll");
  const tags = Array.from(indexHtml.matchAll(/<script src="\/static\/js\/([a-z]+\.js)\?v=(\d+)">/g), (m) => [m[1], m[2]]);
  const names = tags.map((t) => t[0]);
  assert.ok(names.indexOf("fdlayout.js") > names.indexOf("fdview.js") && names.indexOf("fdlayout.js") < names.indexOf("app.js"), "fdlayout.js loads before app.js: " + names.join(","));
  const ver = (n) => (tags.find((t) => t[0] === n) || [])[1];
  assert.deepStrictEqual([ver("fdlayout.js"), ver("fdview.js"), ver("app.js")], ["2", "3", "60"], "the changed scripts carry new versions");
  assert.ok(/app\.css\?v=34/.test(indexHtml), "and so does the stylesheet");
}

// ---- the stylesheet
{
  assert.ok(/\.cmd-card\.fd-on\.fd-custom \.cmd-body\s*\{[^}]*grid-template-columns:\s*repeat\(12, minmax\(0, 1fr\)\);[^}]*grid-template-areas:\s*none;[^}]*grid-template-rows:\s*none;[^}]*grid-auto-flow:\s*row dense/.test(css), "the custom card is a 12-column dense grid");
  assert.ok(/\.cmd-card\.fd-on\.fd-custom \.fd-centre,[^{]*\{\s*display:\s*contents/.test(css), "the centre dissolves");
  assert.ok(/\.cmd-card\.fd-on\.fd-custom \.fd-railwrap[^{]*\{\s*display:\s*contents/.test(css) && /\.cmd-card\.fd-on\.fd-custom [^{]*\.fd-under\s*\{\s*display:\s*contents/.test(css) || /\.fd-custom \.fd-railwrap, [^{]*\.fd-under \{ display: contents/.test(css), "so do the two wrappers that hold two panels");
  assert.ok(/\.cmd-card\.fd-on\.fd-custom \[data-fd-panel\]\s*\{[^}]*grid-area:\s*auto;[^}]*grid-column:\s*span var\(--fd-span, 12\)/.test(css), "a panel's width is its --fd-span");
  assert.ok(/\.cmd-card\.fd-on\.fd-custom \[data-fd-hide\]\s*\{\s*display:\s*none !important/.test(css), "a hide mark hides");
  assert.ok(/@media \(max-width: 760px\)\s*\{\s*\.cmd-card\.fd-on\.fd-custom \[data-fd-panel\]\s*\{\s*grid-column:\s*1 \/ -1/.test(css), "on a phone every panel is full width");
  assert.ok(!/\.fd-custom[^{]*\{[^}]*\bhidden\b/.test(css.replace(/\[data-fd-hide\]/g, "")), "no custom rule un-hides anything");
  assert.ok(/\.cmd-card\.fd-on \.fd-railwrap \{ grid-area: rail;/.test(css) && !/\.fd-railcol \{ grid-area/.test(css), "the default grid's rail area is the wrapper's now (same place, same look)");
  assert.ok(/\.fd-railwrap\[hidden\]/.test(css), "the wrapper hides with the rail");
  assert.ok(/\.fd-arrange \{/.test(css) && /\.fd-arrange::backdrop/.test(css), "the dialog has its look");
}

// ---- a hostile link and hostile storage reach the page only as bounded layouts
{
  const bad = FdView.fromQuery(new URLSearchParams("lay=" + encodeURIComponent("<script>alert(1)</script>")));
  assert.deepStrictEqual([bad.lay, bad.bad], [null, ["lay"]], "markup in lay is a bad key");
  const huge = FdView.fromQuery(new URLSearchParams("lay=" + "ma.8~".repeat(5000)));
  assert.deepStrictEqual([huge.lay, huge.bad], [null, ["lay"]], "a huge lay is not read");
  reset();
  store.m[FdLayout.STORAGE_KEY] = JSON.stringify({ active: "__proto__", layouts: JSON.parse('{"__proto__": {"panels": [{"id": "map"}]}, "a": {"panels": [{"id": "<img>", "span": 99}]}}') });
  run("fdLay.state = FdLayout.load(fdPageStore); fdLay.sel = '';");
  assert.deepStrictEqual(Object.keys(run("fdLay.state.layouts")), ["a"], "hostile names and layouts are cleaned when read");
  assert.strictEqual(run("fdLay.state.layouts.a.panels.length"), 15);
}


// ---- the last shown panel cannot be hidden: the edit changes nothing and the dialog disables its Show box
{
  reset();
  let only = FdLayout.defaults();
  FdLayout.PANELS.slice(1).forEach((p) => { only = FdLayout.show(only, p.id, false); });
  const rows = run("fdLayRowsHtml(l)", { l: only });
  assert.ok(/data-fd-show="run" checked disabled/.test(rows), "the one shown panel's Show box is disabled: " + rows.match(/data-fd-show="run"[^>]*>/));
  assert.strictEqual((rows.match(/data-fd-show="[a-z]+" checked/g) || []).length, 1);
  assert.ok(!/data-fd-show="map"[^>]*disabled/.test(rows), "the hidden ones can be shown");
  assert.ok(!/data-fd-show="[a-z]+"[^>]*disabled/.test(run("fdLayRowsHtml(l)", { l: FdLayout.defaults() })), "with several shown none is disabled");
  run("fdLay.edit = only", { only });
  run("fdLayAct('show', 'run', false)");
  assert.strictEqual(run("fdLayCurrent().panels.find((e) => e.id === 'run').hide"), false, "hiding the last shown panel changes nothing");
}

// ---- Up or Down that reaches an end disables its button: the focus goes to the other arrow of the row
{
  reset();
  run("fdLay.edit = FdLayout.move(FdLayout.defaults(), 'frames', -1)"); // frames is now second ... run first? move once puts frames at 0
  assert.strictEqual(run("fdLayCurrent().panels[0].id"), "frames");
  run("fdLayAct('down', 'frames')");
  assert.strictEqual(run("fdLay.focus"), "", "painted at once with no dialog open: the focus is spent");
  reset();
  run("fdLay.edit = FdLayout.move(FdLayout.defaults(), 'frames', -1)");
  const dlg = dom.element("dialog", "fd-arrange", "fd-arrange");
  dom.document.body.appendChild(dlg);
  const focus = []; // the fake dialog has no rows to find: record the selector fdLayPaint is asked to focus
  const stub = (sel) => (sel === "[data-fd-lay]" || sel === ".fd-arr-list" || sel === "[data-fd-del]" || sel === ".fd-arr-note") ? dlg.children[["[data-fd-lay]", ".fd-arr-list", "[data-fd-del]", ".fd-arr-note"].indexOf(sel)] : { disabled: false, focus() { focus.push(sel); } };
  ["a", "b", "c", "d"].forEach(() => dlg.appendChild(dom.element("div")));
  dlg.querySelector = stub;
  run("fdLayAct('up', 'frames')");
  assert.deepStrictEqual(focus, ['[data-fd-down="frames"]'], "frames reached the top: the Down arrow takes the focus");
  focus.length = 0;
  run("fdLayAct('down', 'frames')");
  assert.deepStrictEqual(focus, ['[data-fd-down="frames"]'], "a move that stays inside keeps the focus on its own arrow");
  run("fdLay.edit = FdLayout.move(FdLayout.defaults(), 'mech', 1)"); focus.length = 0;
  run("fdLayAct('down', 'mech')");
  assert.deepStrictEqual(focus, ['[data-fd-up="mech"]'], "the last row's Down arrow is disabled: the Up arrow takes the focus");
  focus.length = 0;
  run("fdLayAct('span', 'map', '12')");
  assert.deepStrictEqual(focus, ['[data-fd-span="map"]'], "a width change keeps its own select");
  dlg.remove();
}

// ---- the Shared layout chip: shown while a link's layout is in force (and the page is the debugger), gone for a saved or default one
{
  reset();
  const chip = dom.element("button", "cmd-fd-laychip", "fd-laychip"); chip.hidden = true;
  dom.document.body.appendChild(chip);
  run("fdLayChip()");
  assert.strictEqual(chip.hidden, true, "no link layout: no chip");
  run("fdLaySetShared(l)", { l: LAY });
  assert.strictEqual(chip.hidden, false, "a link's layout shows the chip");
  assert.strictEqual(chip.textContent, "Shared layout");
  run("fdLayAct('down', 'map')");
  assert.strictEqual(chip.textContent, "Shared layout (edited)");
  run("fdLayPick('')");
  assert.strictEqual(chip.hidden, true, "back to the default grid: the chip goes");
  run("fdLayPick('~')");
  assert.strictEqual(chip.hidden, false);
  run("cmdPageMode = () => 'live'; fdLayChip();");
  assert.strictEqual(chip.hidden, true, "on the Live page there is no chip");
  run("cmdPageMode = () => 'debug'");
  chip.remove();
  assert.ok(/id="cmd-fd-laychip"[^>]*hidden/.test(indexHtml) || /hidden[^>]*id="cmd-fd-laychip"/.test(indexHtml), "the chip is in the view bar, hidden to begin with");
  assert.ok(/fdLayChip\(\);\s*\n\s*\}/.test(grab("cmdPaintPageMode")) || grab("cmdPaintPageMode").includes("fdLayChip()"), "the page mode paint updates it");
  assert.ok(/getElementById\('cmd-fd-laychip'\);\s*\n\s*if \(chip\) chip\.addEventListener\('click', fdLayOpen\);/.test(src), "it opens the dialog");
}

// ---- the fake DOM's own selectors (L6): an own-property check, and dataset writes are attributes
{
  const d = dom.element("div");
  assert.strictEqual(d.querySelector("[constructor]"), null);
  const host = dom.element("div"); const kid = dom.element("div");
  host.appendChild(kid);
  assert.strictEqual(host.querySelectorAll("[constructor]").length + host.querySelectorAll("[toString]").length + host.querySelectorAll("[hasOwnProperty]").length, 0, "inherited names are not attributes");
  kid.dataset.fdPanel = "map";
  assert.strictEqual(host.querySelectorAll("[data-fd-panel]").length, 1, "a dataset write is visible to [data-x]");
  assert.strictEqual(host.querySelectorAll('[data-fd-panel="map"]').length, 1);
  assert.strictEqual(host.querySelectorAll('[data-fd-panel="x"]').length, 0);
  assert.strictEqual(kid.getAttribute("data-fd-panel"), "map");
  assert.strictEqual(kid.hasAttribute("data-fd-panel"), true);
  assert.strictEqual(kid.hasAttribute("constructor"), false);
}

console.log("fdlayout_ui_check.js ok");
