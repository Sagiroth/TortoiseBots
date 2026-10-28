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
  grab("cmdHeadView"), grab("cmdDebugOnly")].join("\n"), ctx);

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

// ---- the page chrome goes only when every shown card is in the debugger --------------------------------------------
assert.strictEqual(run("cmdDebugOnly([{m:'debug'},{m:'debug'}], b => b.m)"), true);
assert.strictEqual(run("cmdDebugOnly([{m:'debug'},{m:'live'}], b => b.m)"), false);
assert.strictEqual(run("cmdDebugOnly([], b => b.m)"), false, "an empty page keeps its tiles and its demo box");

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
assert.ok(src.includes("cmdDebugOnly(") && src.includes("cmd-debug-only"), "the page turns its chrome off for the debugger-only page");
assert.ok(src.includes('class="fd-crumbs"') && src.includes('class="cmd-legend fd-legend"'), "the card carries the chips and the short legend");
assert.ok(src.includes("fdReadyHtml(") && src.includes("fdFrameStats("), "the raid frames header counts its dead and low mana");
assert.ok(src.includes("UnitInfo.clockText("), "the run chip gets the run clock");
assert.ok(src.includes('class="refresh-picker fd-refresh"'), "the refresh picker sits in the card head");
assert.ok(src.includes("cmdPlaceLayers("), "the one layer panel moves into the map for the debugger's Layers button");

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
