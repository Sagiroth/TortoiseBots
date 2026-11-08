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
vm.runInContext(["var FD_LIVE_TILE_EVENTS = 3;", grab("esc"), grab("fdLiveClock"), grab("fdLiveWinLabel"), grab("fdLiveHpOf"), grab("fdLiveNumsText"), grab("fdLiveTileHtml"), grab("fdLiveTilePatch"), grab("fdLiveGridSync"),
  grab("fdLiveFiledRow"), grab("fdFightHash"), grab("fdLiveTimelineHtml"), grab("fdLiveHandoff"), grab("fdLiveReconcile"), grab("fdLiveResync"), grab("fdLiveDrop"),
  grab("raidLiveGroups"), grab("raidLiveNames"), grab("fdLiveNoteBoss"), grab("cmdLiveNames")].join("\n"), ctx);
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
assert.ok(noBoss.includes("trash") && /class="fd-tile-hp"[^>]* hidden/.test(noBoss), "no boss: trash, the health bar hidden");
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

// ---- (review M3) a fight too short to keep says so, even though the view still holds its events
ctx.fdTimelineHtml = (tl) => "<timeline " + tl.durMs + ">";
ctx.FightDebug = { timeline: (f) => ({ durMs: f.dur_ms }), deathRecaps: () => [] };
assert.ok(run("fdLiveTimelineHtml(st, lf)", { st: { unkept: true }, lf: { dur_ms: 100 } }).includes("too short to keep"), "unkept wins over the synthetic fight");
assert.strictEqual(run("fdLiveTimelineHtml(st, lf)", { st: { unkept: false }, lf: { dur_ms: 100 } }), "<timeline 100>");
assert.ok(run("fdLiveTimelineHtml(st, null)", { st: { noLive: true } }).includes("No live fight"));

// ---- (review L6) a tile that stays is patched in place: its nodes (and a focused link) survive, only changed texts are written
function fakeNode(cls, tag) {
  const n = { tag: tag || "div", classes: new Set(String(cls || "").split(/\s+/).filter(Boolean)), kids: [], children: [], dataset: {}, style: {}, attrs: {}, hidden: false, writes: 0, _t: "", parent: null };
  Object.defineProperty(n, "textContent", { get() { return n._t; }, set(v) { n._t = String(v); n.writes++; } });
  n.classList = { toggle: (c, on) => { if (on) n.classes.add(c); else n.classes.delete(c); }, contains: (c) => n.classes.has(c) };
  Object.defineProperty(n, "className", { get() { return Array.from(n.classes).join(" "); }, set(v) { n.classes = new Set(String(v).split(/\s+/).filter(Boolean)); } });
  n.setAttribute = (k, v) => { n.attrs[k] = v; n.writes++; };
  n.getAttribute = (k) => n.attrs[k];
  const desc = (x) => x.kids.reduce((a, k) => a.concat([k], desc(k)), []);
  const hit = (k, sel) => (sel[0] === "." ? k.classes.has(sel.slice(1)) : k.tag === sel);
  n.querySelector = (sel) => desc(n).find((k) => hit(k, sel)) || null;
  n.querySelectorAll = (sel) => desc(n).filter((k) => hit(k, sel));
  n.add = (k) => { n.kids.push(k); return k; };
  return n;
}
function fakeTile(name) {
  const el = fakeNode("fd-tile");
  el.dataset.fdTile = name;
  ["fd-tile-title", "fd-tile-group", "fd-tile-bossname", "fd-tile-time", "fd-tile-nums"].forEach((c) => el.add(fakeNode(c)));
  const hp = el.add(fakeNode("fd-tile-hp")); hp.add(fakeNode("", "i")); hp.add(fakeNode("", "span"));
  for (let k = 0; k < 3; k++) el.add(fakeNode("fd-tile-ev"));
  el.add(fakeNode("fd-tile-open"));
  return el;
}
ctx.document = { createElement: (tag) => fakeNode("", tag) };
const T = (over) => Object.assign({ name: "Gbmage", title: "Molten Core", group: "Gbmage", boss: "Magmadar", ms: 5000, bossHp: 80, alive: 9, total: 10, dps: 100, hps: 20, events: ["cast a", "cast b"], href: "#/commanders/Gbmage/fight/live" }, over || {});
const tileEl = fakeTile("Gbmage");
run("fdLiveTilePatch(el, t)", { el: tileEl, t: T() });
const link = tileEl.querySelector(".fd-tile-open");
assert.strictEqual(tileEl.querySelector(".fd-tile-time").textContent, "0:05");
assert.strictEqual(tileEl.querySelector(".fd-tile-nums").textContent, "9/10 alive · 100 dps · 20 hps");
assert.deepStrictEqual(tileEl.querySelectorAll(".fd-tile-ev").map((n) => [n.textContent, n.hidden]), [["cast a", false], ["cast b", false], ["", true]]);
assert.strictEqual(tileEl.querySelector(".fd-tile-hp").hidden, false);
assert.strictEqual(link.attrs.href, "#/commanders/Gbmage/fight/live");
const writes = () => tileEl.kids.reduce((a, k) => a + k.writes, 0);
const before = writes();
run("fdLiveTilePatch(el, t)", { el: tileEl, t: T() });
assert.strictEqual(writes(), before, "an unchanged tick writes nothing");
run("fdLiveTilePatch(el, t)", { el: tileEl, t: T({ ms: 6000, bossHp: null, boss: "" }) });
assert.strictEqual(tileEl.querySelector(".fd-tile-time").textContent, "0:06");
assert.strictEqual(tileEl.querySelector(".fd-tile-bossname").textContent, "trash");
assert.ok(tileEl.querySelector(".fd-tile-hp").hidden, "no boss health: the bar is hidden");
assert.strictEqual(tileEl.querySelector(".fd-tile-open"), link, "the link node is the same one");
assert.strictEqual(link.writes, 1, "and its href was written once, not every tick");

// the grid: tiles kept by name and reordered by moves, new ones built, gone ones removed, the note last
const host = fakeNode("fd-livegrid");
host.children = []; host.removeChild = (c) => { host.children.splice(host.children.indexOf(c), 1); host.kids = host.children; };
host.insertBefore = (el, ref) => { const i = host.children.indexOf(el); if (i >= 0) host.children.splice(i, 1); const j = ref ? host.children.indexOf(ref) : host.children.length; host.children.splice(j, 0, el); host.kids = host.children; };
host.appendChild = (el) => host.insertBefore(el, null);
Object.defineProperty(host, "lastElementChild", { get() { return host.children[host.children.length - 1] || null; } });
host.querySelector = (sel) => host.children.find((k) => k.classes.has(sel.slice(1))) || null;
const built = [];
const make = (t) => { built.push(t.name); return fakeTile(t.name); };
run("fdLiveGridSync(host, tiles, more, make)", { host, tiles: [T({ name: "A" }), T({ name: "B" })], more: 3, make });
assert.deepStrictEqual(host.children.map((c) => c.dataset.fdTile || "note"), ["A", "B", "note"]);
assert.strictEqual(host.children[2].textContent, "+3 more fighting");
const a1 = host.children[0], b1 = host.children[1];
run("fdLiveGridSync(host, tiles, more, make)", { host, tiles: [T({ name: "B" }), T({ name: "C" }), T({ name: "A" })], more: 0, make });
assert.deepStrictEqual(host.children.map((c) => c.dataset.fdTile), ["B", "C", "A"], "reordered, the note gone");
assert.strictEqual(host.children[0], b1);
assert.strictEqual(host.children[2], a1, "the tiles that stayed are the same nodes");
assert.deepStrictEqual(built, ["A", "B", "C"], "only the new tile was built");
run("fdLiveGridSync(host, tiles, more, make)", { host, tiles: [T({ name: "C" })], more: 0, make });
assert.deepStrictEqual(host.children.map((c) => c.dataset.fdTile), ["C"], "tiles that left are removed");

// ---- (review L5) the tiles are ranked by the live rows' boss names; the list view subscribes every fighting group, so the stream's URL does not move when the top six reorder
const grp = (key, fightMs) => ({ key, commander: key, combat: { fight_ms: fightMs } });
const raids = { activeTab: "raids", commanders: { list: [] }, dungeons: { openKey: null }, raids: { openKey: null, openGroup: null, runs: [{ key: "409:1", groups: [] }], binds: [] } };
raids.raids.runs[0].groups = Array.from({ length: 9 }, (_, i) => grp("G" + i, 1000 * (i + 1)));
Object.assign(ctx, { state: raids, fdLive: {}, fdLiveBoss: {}, LiveView: { MAX_COMMANDERS: 20 }, cmdPageMode: () => "live", fdPage: { open: null }, cmdFd: {} });
assert.deepStrictEqual(Array.from(run("raidLiveGroups()"), (g) => g.name).slice(0, 3), ["G8", "G7", "G6"], "no boss named: the longest fight first");
run("fdLiveNoteBoss('G1', 'Magmadar'); fdLiveNoteBoss('G0', '')");
assert.deepStrictEqual(Array.from(run("raidLiveGroups()"), (g) => g.name).slice(0, 3), ["G1", "G8", "G7"], "a group whose live row names a boss ranks first");
const names1 = Array.from(run("cmdLiveNames()"));
assert.deepStrictEqual(names1.slice().sort(), Array.from({ length: 9 }, (_, i) => "G" + i), "every fighting group is subscribed, not just the top six");
raids.raids.runs[0].groups[8].combat.fight_ms = 1; // the top six reorder
assert.deepStrictEqual(Array.from(run("cmdLiveNames()")).sort(), names1.slice().sort(), "the subscription (and so the URL) does not change");
Object.assign(ctx, { state: Object.assign({}, raids, { raids: Object.assign({}, raids.raids, { runs: [{ key: "k", groups: Array.from({ length: 25 }, (_, i) => grp("H" + i, i)) }] }) }) });
assert.strictEqual(run("cmdLiveNames()").length, 20, "capped at the stream's 20 names");

// ---- (review Codex H2, L3) a dropped `fight` frame: the resync looks the live key up in the filed fights and hands over
{
  const FdLive = require(path.join(path.dirname(appPath), "fdlive.js"));
  const KEY = "G\u00007\u000055-6";
  const st = FdLive.create("G");
  FdLive.applyEventsFrame(st, { key: KEY, from: 0, cur: 1, events: [{ k: "cast", t: 1 }], fight: { start_ms: 1000, fight_ms: 10 } }, 5);
  const fd = { sel: "live", userSel: false, liveKey: KEY, death: null };
  const calls = [];
  Object.assign(ctx, {
    FdLive, fdLive: { G: st }, cmdFd: { G: fd }, fdLiveReconciling: {}, cmdCards: { get: () => ({}) }, cmdFindBind: () => ({}),
    fdLoad: () => calls.push("load"), fdRender: () => calls.push("render"), fdFightHash: () => calls.push("hash"), fdLiveCatchUp: () => {},
    jsonOrThrow: (r) => r,
    fetch: (url) => { calls.push(url); return Promise.resolve([{ key: "G\u00007\u000055-5", live: false }, { key: KEY, live: false, id: "55-6" }]); },
  });
  run("fdLiveResync()");
  setImmediate(() => setImmediate(() => {
    assert.ok(calls.some((c) => c.startsWith("/api/v1/combat/fights?commander=G")), "the filed fights of that name are asked");
    assert.strictEqual(fd.sel, KEY, "the view handed over to the filed fight");
    assert.ok(fd.userSel && fd.liveKey === null, "and holds it");
    assert.ok(!ctx.fdLive.G, "the live state is dropped");
    assert.ok(calls.includes("load") && calls.includes("render") && calls.includes("hash"));
    // L3: the pick left `live` when the window closed (the state is gone with it); the late `fight` frame still hands over by the card's liveKey
    const fd2 = { sel: "5-5", userSel: false, liveKey: KEY, death: null };
    ctx.cmdFd = { G: fd2 }; ctx.fdLive = {};
    run("fdLiveHandoff('G', { key: k })", { k: KEY });
    assert.strictEqual(fd2.sel, KEY, "a cleared pick is handed over by the liveKey");
    const fd3 = { sel: "5-5", userSel: true, liveKey: KEY, death: null };
    ctx.cmdFd = { G: fd3 };
    run("fdLiveHandoff('G', { key: k })", { k: KEY });
    assert.strictEqual(fd3.sel, "5-5", "a fight the reader picked stands");
    console.log("fdlive_ui_check.js ok");
  }));
}


