"use strict";
// Run by commandersjs_test.go under node: the live fight view's page wiring in app.js (web/static/js/app.js): the live tile's
// markup (escaped, the link to the debugger's live view), the window labels, the filed-row match of the hand-over, the hash segment
// of the live entry, and the wiring pins of the stream handlers (the samples and the positions reach the live view before any gate).
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const ctx = { console };
vm.createContext(ctx);
vm.runInContext([grab("esc"), grab("fdLiveClock"), grab("fdLiveWinLabel"), grab("fdLiveTileHtml"), grab("fdLiveFiledRow"), grab("fdFightHash")].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

// ---- the tile escapes everything the game server names and links to the live view of its key
const EVIL = "<b>x</b>";
const tile = { name: "Gb mage#1", title: EVIL, group: EVIL, boss: EVIL, ms: 125000, bossHp: 42.4, alive: 9, total: 10, dps: 1200, hps: 300,
  events: [EVIL + " died"], href: "#/commanders/Gb%20mage%231/fight/live" };
const html = run("fdLiveTileHtml(t)", { t: tile });
assert.ok(!html.includes("<b>x</b>"), "no raw markup from a name: " + html);
assert.ok(html.includes("&lt;b&gt;x&lt;/b&gt;"), "the name is shown as text");
assert.ok(html.includes('href="#/commanders/Gb%20mage%231/fight/live"'), "the link opens the live view of the key");
assert.ok(html.includes("2:05") && html.includes("42%") && html.includes("9/10 alive") && html.includes("1200 dps") && html.includes("300 hps"));
assert.ok(html.includes('class="fd-tile-mech"'), "the mechanics scorecard slot is there");
assert.strictEqual((html.match(/"/g) || []).length % 2, 0, "balanced quotes");
const noBoss = run("fdLiveTileHtml(t)", { t: Object.assign({}, tile, { boss: "", bossHp: null, events: [] }) });
assert.ok(noBoss.includes("trash") && !noBoss.includes("fd-tile-hp"), "no boss: trash, no health bar");
assert.ok(run("fdLiveTileHtml(t)", { t: Object.assign({}, tile, { bossHp: 900 }) }).includes('width:100%'), "health is clamped");

// ---- the window picker's labels and the filed-row match
assert.deepStrictEqual([30, 60, 120, 300, 0].map((w) => run("fdLiveWinLabel(" + w + ")")), ["Last 30 s", "Last 60 s", "Last 2 min", "Last 5 min", "Whole fight"]);
const KEY = "Gbmage\u00007\u000055-6";
const rows = [{ key: "Gbmage\u00007\u000055-6", id: "55-6", session: 7, live: true }, { key: "Gbmage\u00007\u000055-5", id: "55-5", session: 7 },
  { key: "Gbmage\u00008\u000055-6", id: "55-6", session: 8 }, { key: "Gbmage\u00007\u000055-6", id: "55-6", session: 7 }];
assert.strictEqual(run("fdLiveFiledRow({ liveKey: k }, rows)", { k: KEY, rows }), rows[3], "the filed row of the live fight, not its live row, not another session");
assert.strictEqual(run("fdLiveFiledRow({ liveKey: k }, rows)", { k: "Gbmage\u00007\u000055-9", rows }), null);
assert.strictEqual(run("fdLiveFiledRow({ liveKey: null }, rows)", { rows }), null);

// ---- fdFightHash writes the live segment (and any picked fight's key)
const calls = [];
ctx.cmdPageMode = () => "debug";
ctx.fdPage = { open: "Gbmage", fight: null };
ctx.cmdPageHash = (...a) => calls.push(a);
run("fdFightHash({ sel: 'live' }, true)");
assert.deepStrictEqual(calls.pop(), ["Gbmage", "debug", "live", true], "the live entry is a hash segment");
run("fdFightHash({ sel: k }, false)", { k: KEY });
assert.deepStrictEqual(calls.pop(), ["Gbmage", "debug", KEY, false]);
run("fdFightHash({ sel: null }, true)");
assert.deepStrictEqual(calls.pop(), ["Gbmage", "debug", null, true]);

// ---- wiring pins: app.js as text
const handlers = src.slice(src.indexOf("function cmdLiveHandlers()"), src.indexOf("function cmdLiveKeyCommander("));
const commanderH = handlers.slice(handlers.indexOf("commander: d =>"), handlers.indexOf("pos: d =>"));
const sampleAt = commanderH.indexOf("FdLive.applySample(");
assert.ok(sampleAt > 0, "the commander handler feeds the live view");
assert.ok(sampleAt < commanderH.indexOf("wsConnected"), "the samples are taken outside the wsConnected test (every report, whatever the WebSocket does)");
const posH = handlers.slice(handlers.indexOf("pos: d =>"), handlers.indexOf("events: d =>"));
const posAt = posH.indexOf("FdLive.applyPos(");
assert.ok(posAt > 0, "the pos handler feeds the live view");
assert.ok(posAt < posH.indexOf("posAllowed("), "the positions are taken before the refresh rate gate");
assert.ok(handlers.includes("fdLiveEvents(d)") && handlers.includes("fdLiveHandoff(") && handlers.includes("fdLiveResync()"), "events, fight and resync reach the live views");
assert.ok(/Live fights|fd-livegrid/.test(src) && src.includes("getElementById('raid-live')") && src.includes("getElementById('dgn-live')"), "the Raids and Dungeons lists paint their tiles");
assert.ok(src.includes("'/api/v1/combat/live?commander='") && src.includes("&x=1'"), "the catch-up routes are the two bounded ones");

console.log("fdlive_ui_check.js ok");
