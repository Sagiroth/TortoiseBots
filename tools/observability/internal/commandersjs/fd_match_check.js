"use strict";
// Run by commandersjs_test.go under node: the Commanders card's Fight debugger header matches the approved prototype, while Live
// mode keeps the elements the prototype drops. The debugger mode shows its own title and chips (bots/groups, plan, run, tanks, the
// fight verdict) and the refresh picker; Live keeps the title, the ONLINE/FEED badges, the lead/run line and the warnings.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const ctx = { console };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(dir, "fightdebug.js"), "utf8"), ctx, { filename: "fightdebug.js" });
vm.runInContext([grab("esc"), grab("numOr0"), grab("fdHeaderHtml"), grab("fdFrameStats"), grab("fdReadyHtml"), grab("fdLegendHtml"),
  grab("cmdHeadView")].join("\n"), ctx);
function grabConst(name) {
  const re = new RegExp(`^  const ${name} = `, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf(";\n", m.index);
  return src.slice(m.index, end + 2);
}
vm.runInContext(grabConst("FD_EMPTY") + grab("fdEmptyNote"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const plain = (v) => JSON.parse(JSON.stringify(v));
const fights = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-fights.json"), "utf8"));
const byId = (id) => fights.find((f) => f.id === id);
const F54 = byId("5-4");
const LAYOUT = { mt: "Ragemeister", ot: ["Brun", "Irelanthe"] };
// newest first, like GET /api/v1/combat/fights; the keys are the ids the fixture knows.
const ROWS = fights.map((f) => ({ key: f.id, dur: f.dur_ms, step: f.step, deaths: (f.deaths || []).length, boss_name: f.boss_name }));
const DETAILS = {};
fights.forEach((f) => { DETAILS[f.id] = { fight: f, warnings: [] }; });
const BIND = { commander: "Glacienne", bots: F54.bots, layout: LAYOUT, run: { plan: "onyxias_lair", step: 20, steps: 29 } };

// ---- which header pieces each mode shows ---------------------------------------------------------------------------
// The removed elements: the old title, the ONLINE/FEED badges, "Led by ..." and the run line, the down/far warnings. The
// debugger's own title and chips, and the refresh picker, replace them.
assert.deepStrictEqual(plain(run("cmdHeadView(true)")), { title: false, badges: false, sub: false, run: false, warn: false, crumbs: true, refresh: true },
  "the debugger hides the Live header and shows its own chips");
assert.deepStrictEqual(plain(run("cmdHeadView(false)")), { title: true, badges: true, sub: true, run: true, warn: true, crumbs: false, refresh: false },
  "Live mode keeps every element the debugger drops");

// ---- the page chrome goes in the page's Fight debugger mode ---------------------------------------------------------
assert.ok(src.includes("cmdPageMode(") && src.includes("cmd-debug-only"), "the page's debugger mode turns its chrome off");
assert.ok(src.includes("cmd-debug-grid"), "the All raids tab keeps a chrome of its own");

// ---- the empty states: one line, no blank fight boxes ------------------------------------------------------------------
assert.strictEqual(run("FD_EMPTY.noRaid"), "No raid bound right now.");
assert.strictEqual(run("fdEmptyNote([], null, false)"), "No fight yet: the first pull shows here.", "no fight: the card says where it will show");
assert.strictEqual(run("fdEmptyNote([], null, true)"), "", "a live fight is a fight");
assert.strictEqual(run("fdEmptyNote([{ key: 'k' }], null, false)"), "", "a fight waiting for its detail is not empty");
assert.strictEqual(run("fdEmptyNote([], { bots: [] }, false)"), "", "a loaded fight is not empty");
assert.ok(src.includes('<p class="fd-nofight" hidden></p>'), "the card carries the line");
assert.ok(src.includes("FD_EMPTY.noRaid"), "the All raids grid says no raid is bound");

// ---- the header chips: the prototype's five, plus the run chip carrying the step -----------------------------------
const head = run("fdHeaderHtml(bind, rows, details, layout, '19:29')", { bind: BIND, rows: ROWS, details: DETAILS, layout: LAYOUT });
const chips = [...head.matchAll(/<span class="fd-crumb[^"]*">([^<]*)<\/span>/g)].map((m) => m[1]);
assert.deepStrictEqual(chips, [
  "39 bots · 8 groups",
  "onyxias_lair",
  "Run 19:29 · step 20/29",
  "Main tank Ragemeister",
  "Off-tanks Brun, Irelanthe",
  "1 of 5 fights need a look",
], "the header chips: " + head);
assert.ok(head.includes('<span class="fd-crumb fail">1 of 5 fights need a look</span>'), "a fight needs a look, so the chip is red");
const cleanHead = run("fdHeaderHtml(bind, rows, details, layout, '')", { bind: Object.assign({}, BIND, { run: null }), rows: [], details: {}, layout: null });
assert.ok(cleanHead.includes('<span class="fd-crumb">39 bots · 8 groups</span>'), cleanHead);
assert.ok(!cleanHead.includes("Main tank") && !cleanHead.includes("fights need a look") && !cleanHead.includes("Run "), "no layout and no fights leave those chips out");

// ---- raid frames: dead and low-mana counts, the ready chips ---------------------------------------------------------
const frames = run("FightDebug.raidFrames(bind)", { bind: BIND });
assert.deepStrictEqual(plain(run("fdFrameStats(frames)", { frames })), { dead: 0, lowMana: 0 });
const ready = run("fdReadyHtml(frames)", { frames });
assert.ok(ready.includes('<span class="fd-chip ok">0 dead</span>'), ready);
assert.ok(ready.includes('<span class="fd-chip ok">0 under 30% mana</span>'), ready);
const badFrames = [{ tiles: [
  { alive: false, role: "dps", hasPower: true, powerKind: "mana", powerPct: 80 },
  { alive: true, role: "healer", hasPower: true, powerKind: "mana", powerPct: 12 },
  { alive: true, role: "tank", hasPower: true, powerKind: "rage", powerPct: 90 },
] }];
assert.deepStrictEqual(plain(run("fdFrameStats(bad)", { bad: badFrames })), { dead: 1, lowMana: 1 });
const badReady = run("fdReadyHtml(bad)", { bad: badFrames });
assert.ok(badReady.includes('<span class="fd-chip fail">1 dead</span>'), badReady);
assert.ok(badReady.includes('<span class="fd-chip warn">1 under 30% mana</span>'), badReady);

// ---- the map legend: four entries, not the Live page's long list ----------------------------------------------------
const legend = run("fdLegendHtml()");
for (const word of ["Tank", "Healer", "Damage", "Mob / death"]) assert.ok(legend.includes(word), "the legend names " + word + ": " + legend);
for (const word of ["casting", "crowd controlled", "drinking", "fleeing", "Lead", "Dead"]) assert.ok(!legend.includes(word), "the Live legend's " + word + " is gone");
assert.ok(legend.includes('<i class="fdmob"></i>'), "Mob / death keeps its dot: " + legend);

// ---- escaping: a plan, a tank and an off-tank that look like markup -------------------------------------------------
const EVIL = '<img src=x onerror=alert(1)>"&';
const evilHead = run("fdHeaderHtml(bind, rows, details, layout, '19:29')", {
  bind: Object.assign({}, BIND, { run: { plan: EVIL, step: 1, steps: 2 } }),
  layout: { mt: EVIL, ot: [EVIL] },
});
assert.ok(!/<img/i.test(evilHead), "raw markup survived: " + evilHead.slice(0, 200));
assert.ok(evilHead.includes("&lt;img"), "the names are escaped as text");
for (const m of evilHead.matchAll(/="([^"]*)"/g)) assert.ok(!/[<>]/.test(m[1]), "an attribute value broke out: " + m[1]);
assert.strictEqual((evilHead.match(/"/g) || []).length % 2, 0, "unbalanced quotes: " + evilHead.slice(0, 200));

// ---- the page wires it up -------------------------------------------------------------------------------------------
assert.ok(src.includes("cmdPageMode(") && src.includes("cmd-debug-only"), "the page turns its chrome off for its debugger mode");
assert.ok(src.includes('class="fd-crumbs"') && src.includes('class="cmd-legend fd-legend"'), "the card carries the chips and the short legend");
assert.ok(src.includes("fdReadyHtml(") && src.includes("fdFrameStats("), "the raid frames header counts its dead and low mana");
assert.ok(src.includes("UnitInfo.clockText("), "the run chip gets the run clock");
assert.ok(src.includes('class="refresh-picker fd-refresh"'), "the refresh picker sits in the card head");
// ---- the one layer panel survives the tab switches ---------------------------------------------------------------
// The debugger moves #cmd-layers into the open card's map; that card is removed on the next tab switch and on All raids, so
// the panel must be taken back first (cmdRescueLayers) or it leaves the page inside the removed card. Run through the real
// cmdPlaceLayers / renderFdPage / fdLeaveCard / cmdForgetCard under a fake DOM.
{
  const { createDocument } = require("./fakedom.js");
  const dom = createDocument(), doc = dom.document;
  const page = dom.element("div", "tab-commanders"), list = dom.element("div", "cmd-list"), grid = dom.element("div", "cmd-overview");
  const layers = dom.element("details", "cmd-layers"), sum = dom.element("summary");
  sum.textContent = "Layers and trails";
  layers.appendChild(sum);
  dom.attach(doc.body, page);
  dom.attach(page, layers);
  dom.attach(page, list);
  dom.attach(doc.body, grid);

  const h1 = { console, Array, Map, Set, clearTimeout, document: doc };
  vm.createContext(h1);
  vm.runInContext([grab("cmdRescueLayers"), grab("cmdPlaceLayers"), grab("renderFdPage"), grab("fdLeaveCard"),
    grab("cmdForgetCard")].join("\n"), h1);
  const binds = { A: { commander: "A", bots: [] }, B: { commander: "B", bots: [] } };
  const cards = new Map();
  Object.assign(h1, {
    fdPage: { open: null }, fdTabGone: () => false, cmdFindBind: (k) => binds[k] || null, cmdCards: cards,
    fdStopCard: () => {}, cmdFd: {}, cmdMaps: {}, cmdAvail: {}, cmdFightsCache: {}, cmdFightsPending: {},
    setHtml: (el, html) => { el.innerHTML = html; }, esc: (v) => String(v), fdGridHtml: () => "", fdGridPoll: () => {},
    FdTabs: { sortBinds: (l) => l },
    newCmdCard: (key) => {
      const card = doc.createElement("div");
      card.className = "cmd-card";
      card.dataset.commander = key;
      card.appendChild(dom.element("div", "", "cmd-stage"));
      return card;
    },
    cmdRenderCard: (card, bind, debug) => { card.classList.toggle("fd-on", !!debug); },
  });
  const open = (name) => { h1.fdPage.open = name; h1.renderFdPage([binds.A, binds.B], [], list); };
  const stageOf = (key) => cards.get(key).querySelector(".cmd-stage");

  open("A");
  h1.cmdPlaceLayers(true);
  assert.strictEqual(doc.getElementById("cmd-layers").parentElement, stageOf("A"), "the panel moves into the open card's map");

  open("B"); // the card that holds the panel leaves
  assert.ok(doc.getElementById("cmd-layers"), "the panel is still on the page after the previous card left");
  h1.cmdPlaceLayers(true);
  assert.strictEqual(doc.getElementById("cmd-layers").parentElement, stageOf("B"), "and sits in the newly open card's map");
  assert.strictEqual(cards.has("A"), false, "the previous card is gone");

  open(null); // All raids: every card leaves
  h1.cmdPlaceLayers(false);
  const back = doc.getElementById("cmd-layers");
  assert.ok(back, "the panel is still on the page after All raids");
  assert.strictEqual(back.parentElement, page, "and is back on the page, not inside a card");
  assert.ok(!back.classList.contains("cmd-layers-map"), "with its page styling");
  assert.strictEqual(back.querySelector("summary").textContent, "Layers and trails", "and its own label");
  assert.ok(page.children.indexOf(back) < page.children.indexOf(list), "the panel still sits above the commander list");

  open("B");
  h1.cmdPlaceLayers(true);
  h1.cmdForgetCard("B"); // a commander that unbound while its tab was open
  assert.strictEqual(doc.getElementById("cmd-layers").parentElement, page, "a forgotten commander's card leaves the panel behind");
}

// ---- the stylesheet drops the page chrome and the Live legend, and keeps the map's Layers panel usable ---------------
const css = fs.readFileSync(path.join(dir, "..", "css", "app.css"), "utf8");
const hide = /\.cmd-debug-only\s*>\s*\.kpi-row[\s\S]*?\{[^}]*display:\s*none/.exec(css);
assert.ok(hide, "the debugger page hides the tiles and the page boxes");
for (const sel of [".cmd-refresh-bar", ".cmd-demo-bar", ".cmd-layers", ".cmd-external"]) {
  assert.ok(css.includes(sel), "the hide rule covers " + sel);
}
assert.ok(/\.cmd-card\.fd-on \.cmd-legend\s*\{[^}]*display:\s*none/.test(css), "the Live legend is hidden in the debugger");
assert.ok(/\.cmd-card\.fd-on \.cmd-legend\.fd-legend\s*\{[^}]*display:\s*flex/.test(css), "the short legend shows in its place");
assert.ok(/\.cmd-layers\.cmd-layers-map\s*\{/.test(css), "the moved layers panel has its map styling");
console.log("fight debugger artifact match ok");
