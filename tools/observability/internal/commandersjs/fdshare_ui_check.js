"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's share links in app.js (web/static/js/app.js). The route reads the view out
// of the hash (range, filter, aura pick, night, replay place and speed, the live window) and writes it back after the hash's own
// params (bot, p); a new fight starts clean; the card writes its view with replaceState only; a link is applied once, when its fight is
// on screen; the live hand-over rewrites the hash to the filed fight; Copy link; the link survives login; a hostile link never breaks
// the page or puts markup in it.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");
const jsDir = path.dirname(appPath);

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const FdView = require(path.join(jsDir, "fdview.js"));
const FdFilter = require(path.join(jsDir, "fdfilter.js"));
const FdLive = require(path.join(jsDir, "fdlive.js"));
const FightAuras = require(path.join(jsDir, "fightauras.js"));
const FightReplay = require(path.join(jsDir, "fightreplay.js"));
const FightDebug = require(path.join(jsDir, "fightdebug.js"));

const ctx = { console, URLSearchParams, encodeURIComponent, decodeURIComponent, Object, Math, Date, JSON, String, Number, Array,
  FdView, FdFilter, FdLive, FightAuras, FightReplay, FightDebug, setTimeout: (f, ms) => { timers.push({ f, ms }); return timers.length; }, clearTimeout: () => {} };
const timers = [];
vm.createContext(ctx);
const routeSrc = src.slice(src.indexOf("  const TABS = {"), src.indexOf("  function openNavDrawer()"));
const names = ["keepHash", "parseRoute", "routeHash", "go", "cmdPageHash", "cmdRouteTab", "fdRoutePick", "fdFightHash", "fdViewHash", "fdViewHashSoon",
  "fdApplyPending", "fdSetDay", "fdLinkTail", "fdCopyLink", "fdCopyPaint", "fdLiveHandoff", "fdViewHandover", "fdViewSync", "fdAuCarry", "fdLiveFiledRow",
  "fdLiveDrop", "fdLiveRangeOf", "fdAuLiveKey", "fdAuIv", "fdSyncSel", "esc"];
vm.runInContext(["var FD_EV_PAGE = 500; const fdAuMemo = new WeakMap();", routeSrc].concat(names.map(grab)).join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const J = (v) => JSON.parse(JSON.stringify(v));
const realSetDay = ctx.fdSetDay; // cmdRouteTab's test swaps in a recorder; fdSetDay's own test needs the real one

// ---- the page's bits the route code touches
const loc = { _hash: "#/commanders", get hash() { return this._hash; }, set hash(v) { this._hash = v; }, href: "https://dash.example/dashboard#/commanders" };
const writes = [];
Object.assign(ctx, {
  window: { location: loc },
  history: { replaceState: (a, b, h) => { writes.push(["replace", h]); loc._hash = h; }, pushState: (a, b, h) => { writes.push(["push", h]); loc._hash = h; } },
  state: { activeTab: "commanders", profileGuid: null, profileTab: "overview", dungeons: { openKey: null }, raids: { openKey: null, openGroup: null } },
  applyRoute: () => { writes.push(["applyRoute"]); },
  fdPage: { mode: "debug", explicit: true, loaded: true, open: null, fight: null, pending: null, hashQ: "" },
  cmdPageMode: () => "debug",
  cmdFd: {}, cmdCards: new Map(), fdLive: {},
});
const route = (hash) => { loc._hash = hash; return run("parseRoute()"); };
const KEY = "Tank\u0000" + 7 + "\u0000f3";
const KENC = encodeURIComponent(KEY);
const FILTER = FdFilter.addAt(FdFilter.empty(), "", { f: "ability", op: "is", v: "Blood Siphon", not: false });
const CODE = FdFilter.encode(FILTER);
const POISON = 24321, SIPHON = 24324;

// ---- parseRoute: the debugger with a fight carries its view; other routes carry none
let r = route("#/commanders/Tank/fight/" + KENC + "?bot=5&r=1000-9000");
assert.deepStrictEqual([r.cmd, r.cmdMode, r.cmdFight, r.bot], ["Tank", "debug", KEY, 5], "the route and the bot param");
assert.deepStrictEqual(J(r.fdq.range), { from: 1000, to: 9000 }, "the view's range");
assert.deepStrictEqual(J(r.fdq.bad), []);
assert.strictEqual(route("#/commanders/Tank").fdq, null, "a commander's Live card has no view");
assert.strictEqual(route("#/commanders/Tank/fight").fdq, null, "the debugger with no fight picked has none");
assert.strictEqual(route("#/commanders?r=1-9").fdq, null);
r = route("#/commanders/Tank/fight/" + KENC);
assert.deepStrictEqual(J(r.fdq.bad), [], "a fight with no view params: nothing to read, nothing bad");
assert.strictEqual(FdView.toQuery(r.fdq), "");

// ---- routeHash: the view goes after bot and p; a route with no view is exactly as before
const withQ = { tab: "commanders", cmd: "Tank", cmdMode: "debug", cmdFight: KEY, bot: 5, ptab: "gear", fdq: { range: { from: 1000, to: 9000 }, filter: FILTER, au: POISON } };
let h = run("routeHash(r)", { r: withQ });
assert.strictEqual(h, "#/commanders/Tank/fight/" + KENC + "?bot=5&p=gear&r=1000-9000&f=" + CODE + "&au=" + POISON, "bot and p first, then r, f, au: " + h);
assert.ok(h.indexOf("bot=5") < h.indexOf("r=1000-9000"), "bot before r");
assert.strictEqual(run("routeHash(r)", { r: { tab: "commanders", cmd: "Tank", cmdMode: "debug", cmdFight: KEY, bot: null, fdq: null } }), "#/commanders/Tank/fight/" + KENC, "no view: the hash of a route without view params is unchanged");
assert.strictEqual(run("routeHash(r)", { r: { tab: "commanders", cmd: "Tank", cmdMode: null, cmdFight: null, bot: 5, fdq: { range: { from: 1, to: 9999 } } } }), "#/commanders/Tank?bot=5", "a route with no fight writes no view");

// ---- go(): the view stays inside its fight
loc._hash = "#/commanders/Tank/fight/" + KENC + "?r=1000-9000&f=" + CODE;
run("go({ bot: 7 })");
assert.strictEqual(loc._hash, "#/commanders/Tank/fight/" + KENC + "?bot=7&r=1000-9000&f=" + CODE, "a profile over the debugger keeps the view: " + loc._hash);
run("go({ bot: null })");
assert.strictEqual(loc._hash, "#/commanders/Tank/fight/" + KENC + "?r=1000-9000&f=" + CODE, "closing it keeps it too");
run("go({ cmdFight: 'other' })");
assert.strictEqual(loc._hash, "#/commanders/Tank/fight/other", "another fight starts clean: " + loc._hash);
loc._hash = "#/commanders/Tank/fight/" + KENC + "?r=1000-9000";
run("go({ cmd: 'Mage' })");
assert.ok(loc._hash.indexOf("r=") < 0, "another commander starts clean: " + loc._hash);

// ---- cmdPageHash: a new fight drops the view params, keeps bot; the same fight keeps all
loc._hash = "#/commanders/Tank/fight/" + KENC + "?bot=5&r=1000-9000&f=" + CODE + "&au=" + POISON + "&mk=deaths&n=2026-11-06&rp=5&sp=2";
writes.length = 0;
run("cmdPageHash('Tank', 'debug', 'F2', false)");
assert.strictEqual(loc._hash, "#/commanders/Tank/fight/F2?bot=5", "another fight: r, f, au, mk, n, rp, sp go; bot stays: " + loc._hash);
loc._hash = "#/commanders/Tank/fight/" + KENC + "?bot=5&r=1000-9000&f=" + CODE;
run("cmdPageHash('Tank', 'debug', k, true)", { k: KEY });
assert.strictEqual(loc._hash, "#/commanders/Tank/fight/" + KENC + "?bot=5&r=1000-9000&f=" + CODE, "the same fight keeps its view");
loc._hash = "#/commanders/Tank/fight/" + KENC + "?bot=5&r=1000-9000&p=gear";
run("cmdPageHash('Tank', 'live', null, true)");
assert.strictEqual(loc._hash, "#/commanders/Tank?bot=5&p=gear", "back to Live: no view, bot and p stay");

// ---- keepHash and the login pins
assert.strictEqual(run("keepHash()", { window: { location: { hash: "#/commanders/Tank/fight/live?lw=120" } } }), "#/commanders/Tank/fight/live?lw=120", "a route is kept");
assert.strictEqual(run("keepHash()", { window: { location: { hash: "" } } }), "", "no hash");
assert.strictEqual(run("keepHash()", { window: { location: { hash: "#frag" } } }), "", "a fragment that is not a route");
assert.strictEqual(run("keepHash()", { window: { location: { hash: "#/" + "a".repeat(4095) } } }), "", "past 4096 characters");
assert.strictEqual(run("keepHash()", { window: { location: { hash: "#/" + "a".repeat(4094) } } }).length, 4096, "exactly 4096 is kept");
assert.strictEqual(run("keepHash()", { window: { location: { hash: undefined } } }), "", "no string at all");
ctx.window = { location: loc };
assert.strictEqual((src.match(/'\/login' \+ keepHash\(\)/g) || []).length, 2, "the logout and the 401 redirect carry the route");
assert.ok(!/'\/login'(?! \+ keepHash)/.test(src.replace(/'\/login' \+ keepHash\(\)/g, "")), "no other redirect to /login drops it");
const login = fs.readFileSync(path.join(jsDir, "..", "..", "login.html"), "utf8");
assert.ok(login.includes("window.location.hash") && login.includes("h.startsWith('#/')") && login.includes("h.length <= 4096") && login.includes("'/dashboard' + keep"), "login.html keeps the route");

// ---- a hostile link never breaks the page or carries markup into it
{
  const EVIL = "<img src=x onerror=alert(1)>";
  const hostile = route("#/commanders/Tank/fight/" + KENC + "?r=" + encodeURIComponent(EVIL) + "&f=" + encodeURIComponent(EVIL) + "&au=" + encodeURIComponent(EVIL) + "&sp=" + encodeURIComponent(EVIL)
    + "&mk=n" + encodeURIComponent(EVIL) + "&n=" + encodeURIComponent(EVIL) + "&rp=1e9&lw=" + encodeURIComponent(EVIL));
  assert.deepStrictEqual(J(hostile.fdq.bad), ["r", "f", "au", "n", "rp", "sp", "lw"], "every bad part is dropped and named");
  assert.deepStrictEqual(J(hostile.fdq.marker), { name: EVIL }, "a marker name is data");
  const back = run("routeHash(r)", { r: hostile });
  assert.ok(back.indexOf("<") < 0 && back.indexOf(">") < 0, "and what is written back carries no markup: " + back);
  const tail = run("fdLinkTail(fd)", { fd: { sel: KEY, linkBad: { key: KEY, bad: ["r", EVIL] }, copy: { url: "https://x/\"><script>alert(1)</script>" } } });
  assert.ok(!tail.includes("<script>") && !tail.includes("<img"), "the unread-parts note and the link box are escaped: " + tail);
  assert.ok(tail.includes("Some parts of the link could not be read: r, &lt;img src=x onerror=alert(1)&gt;"), "the note: " + tail);
  assert.ok(tail.includes('readonly value="https://x/&quot;&gt;&lt;script&gt;'), "the box is read-only");
  assert.ok(route("#/commanders/Tank/fight/" + KENC + "?%E0%A4%A&=&&r").fdq.bad.indexOf("r") >= 0, "a mangled query is read, not thrown on");
  assert.ok(Array.isArray(route("#/commanders/Tank/fight/%E0%A4%A?r=1").fdq.bad), "a mangled fight segment too");
}

// ---- cmdRouteTab: a link's view is kept until its fight is on screen; a night goes through fdSetDay first
{
  const states = {}, calls = [];
  const fs_ = (k) => states[k] || (states[k] = { sel: null, userSel: false, day: "", liveLink: false, death: null, linkBad: null });
  Object.assign(ctx, {
    fdState: fs_, fdStopCard: () => {}, fdSeenWhenOpen: () => {}, cmdPageSetMode: () => {}, fdRunStopAll: () => {},
    fdSetDay: (card, key, day, o) => { calls.push(["fdSetDay", key, day, !!(o && o.quiet)]); const fd = fs_(key); fd.day = day; fd.sel = null; fd.userSel = false; },
  });
  ctx.fdPage = { mode: "debug", explicit: true, loaded: true, open: null, fight: null, pending: null, hashQ: "" };
  run("cmdRouteTab(r)", { r: route("#/commanders/Tank/fight/" + KENC + "?n=2026-11-06&r=1000-9000&au=3&zz=1&sp=3") });
  assert.deepStrictEqual(calls, [["fdSetDay", "Tank", "2026-11-06", true]], "a night in the link goes through fdSetDay (before the pick)");
  assert.strictEqual(ctx.fdPage.pending.key, KEY, "the link's view waits for its fight");
  assert.strictEqual(ctx.fdPage.pending.cmd, "Tank");
  assert.deepStrictEqual(J(ctx.fdPage.pending.q.range), { from: 1000, to: 9000 });
  assert.strictEqual(states.Tank.sel, KEY, "and the fight is picked as if clicked");
  assert.strictEqual(states.Tank.userSel, true);
  assert.deepStrictEqual(J(states.Tank.linkBad), { key: KEY, bad: ["sp"] }, "the unread part is remembered for the note");
  // the same hash again (a hashchange from a profile opening): not a new link
  const first = ctx.fdPage.pending;
  ctx.fdPage.pending = null;
  run("cmdRouteTab(r)", { r: route("#/commanders/Tank/fight/" + KENC + "?n=2026-11-06&r=1000-9000&au=3&sp=3") });
  assert.strictEqual(ctx.fdPage.pending, null, "the view the page wrote itself is not applied again");
  // another fight's route with no view: no pending, and an older one for another fight is dropped
  ctx.fdPage.pending = first;
  run("cmdRouteTab(r)", { r: route("#/commanders/Tank/fight/F9") });
  assert.strictEqual(ctx.fdPage.pending, null, "a plain route to another fight clears an old link's view");
  // a link with no view params: no pending at all
  calls.length = 0;
  run("cmdRouteTab(r)", { r: route("#/commanders/Mage/fight/" + KENC) });
  assert.strictEqual(ctx.fdPage.pending, null);
  assert.strictEqual(calls.length, 0, "no night, no fdSetDay");
  // a manual edit of the link inside the same fight is read again
  ctx.fdPage.hashQ = "r=1000-9000";
  run("cmdRouteTab(r)", { r: route("#/commanders/Mage/fight/" + KENC + "?r=2000-8000") });
  assert.deepStrictEqual(J(ctx.fdPage.pending.q.range), { from: 2000, to: 8000 }, "a changed link for the open fight is applied");
  // a live link
  ctx.fdPage = { mode: "debug", explicit: true, loaded: true, open: null, fight: null, pending: null, hashQ: "" };
  run("cmdRouteTab(r)", { r: route("#/commanders/Tank/fight/live?lw=120&f=" + CODE) });
  assert.strictEqual(ctx.fdPage.fight, "live");
  assert.strictEqual(ctx.fdPage.pending.q.lw, 120, "the live window waits");
  assert.strictEqual(states.Tank.liveLink, true, "a live link stays on its note when no fight runs");
  assert.strictEqual(states.Tank.sel, "live");
}

// ---- the live link: it parses as the debugger's live fight, with its window and filter
r = route("#/commanders/Tank/fight/live?lw=120&f=" + CODE);
assert.deepStrictEqual([r.cmd, r.cmdMode, r.cmdFight, r.fdq.lw], ["Tank", "debug", "live", 120], "lw=120 on the live route");
assert.deepStrictEqual(J(r.fdq.filter), J(FILTER), "and its filter");
assert.strictEqual(run("routeHash(r)", { r }), "#/commanders/Tank/fight/live?f=" + CODE + "&lw=120", "the live route writes it back");
r = route("#/commanders/Tank/fight/live?lw=45&n=2026-11-06&rp=5&sp=2&r=1000-9000");
assert.deepStrictEqual(J(r.fdq.bad), ["n", "rp", "sp", "lw"], "a live link has no night, replay or speed, and lw=45 is not a window");
assert.strictEqual(r.fdq.lw, null);
assert.strictEqual(r.fdq.follow, false, "a range in a live link is a fixed range");

// ---- fdViewHash: the card's view into the hash, replaceState only
{
  const mk = () => ({
    sel: KEY, day: "2026-11-06", filter: FILTER, liveWin: 60,
    view: { key: KEY, range: { from: 72000, to: 125000 }, follow: true, reclamp: false },
    au: { key: KEY, sid: POISON, marker: { sid: SIPHON }, userSid: true, userMark: true },
    rp: { on: true, t: 95000.4, playing: false, speed: 2, timer: 0, key: KEY },
  });
  const fd = mk();
  ctx.cmdFd = { Tank: fd };
  ctx.fdPage = { mode: "debug", explicit: true, loaded: true, open: "Tank", fight: KEY, pending: null, hashQ: "" };
  loc._hash = "#/commanders/Tank/fight/" + KENC + "?bot=5";
  writes.length = 0;
  run("fdViewHash('Tank')");
  assert.strictEqual(loc._hash, "#/commanders/Tank/fight/" + KENC + "?bot=5&r=72000-125000&f=" + CODE + "&au=" + POISON + "&mk=s" + SIPHON + "&n=2026-11-06&rp=95000&sp=2", "the whole view after bot: " + loc._hash);
  assert.deepStrictEqual(writes.map((w) => w[0]), ["replace"], "one replaceState, no pushState, no applyRoute");
  run("fdViewHash('Tank')");
  assert.strictEqual(writes.length, 1, "unchanged: no write");
  assert.strictEqual(ctx.fdPage.hashQ, FdView.toQuery({ range: fd.view.range, filter: FILTER, au: POISON, marker: { sid: SIPHON }, night: "2026-11-06", rp: 95000, sp: 2 }), "the page knows what it wrote");
  // defaults are not written: an aura the reader never picked, a marker left on its default, replay off, speed 1, Tonight
  const d = mk(); d.au.userSid = false; d.au.userMark = false; d.rp.on = false; d.rp.speed = 1; d.day = ""; d.view.range = null; d.filter = FdFilter.empty();
  ctx.cmdFd = { Tank: d };
  run("fdViewHash('Tank')");
  assert.strictEqual(loc._hash, "#/commanders/Tank/fight/" + KENC + "?bot=5", "a view at its defaults leaves only bot");
  // a range that belongs to another fight is not this fight's
  const o = mk(); o.view.key = "other"; o.au.key = "other"; o.day = ""; o.rp.on = false; o.rp.speed = 1; o.filter = FdFilter.empty();
  ctx.cmdFd = { Tank: o };
  run("fdViewHash('Tank')");
  assert.strictEqual(loc._hash, "#/commanders/Tank/fight/" + KENC + "?bot=5", "a range or pick of another fight is not written");
  // not the open tab / no fight / a link not applied yet / the hash on another route: nothing
  ctx.cmdFd = { Tank: mk() };
  writes.length = 0;
  ctx.fdPage.open = "Mage"; run("fdViewHash('Tank')");
  ctx.fdPage.open = "Tank"; ctx.cmdFd.Tank.sel = null; run("fdViewHash('Tank')");
  ctx.cmdFd.Tank.sel = KEY; ctx.fdPage.pending = { cmd: "Tank", key: KEY, q: {} }; run("fdViewHash('Tank')");
  assert.strictEqual(writes.length, 0, "not the open tab, no fight, or a link still waiting: nothing is written (the link is not overwritten with defaults)");
  ctx.fdPage.pending = null;
  loc._hash = "#/commanders/Tank/fight/F9?bot=5"; ctx.cmdFd.Tank.sel = KEY; run("fdViewHash('Tank')");
  assert.strictEqual(writes.length, 0, "the hash names another fight: nothing");
  // a hostile name in the marker is encoded, not written raw
  const ev = mk(); ev.au.marker = { name: "<b>\"x\"&y" }; ev.day = ""; ev.rp.on = false;
  ctx.cmdFd = { Tank: ev };
  loc._hash = "#/commanders/Tank/fight/" + KENC;
  run("fdViewHash('Tank')");
  assert.ok(loc._hash.indexOf("<") < 0 && loc._hash.indexOf('"') < 0 && loc._hash.indexOf("mk=n%3Cb%3E%22x%22%26y") > 0, "the marker name is URL-encoded: " + loc._hash);
  assert.deepStrictEqual(J(route(loc._hash).fdq.marker), { name: "<b>\"x\"&y" }, "and reads back as the same name");
}

// ---- fdViewHash in a live view: the window and a fixed range, no replay, no night
{
  const live = { sel: "live", day: "2026-11-06", filter: FILTER, liveWin: 120, view: { key: "live:K1", range: null, follow: true, reclamp: false },
    au: { key: "live:K1", sid: POISON, marker: "deaths", userSid: true, userMark: true }, rp: { on: true, t: 5000, playing: false, speed: 2, timer: 0, key: "live" } };
  ctx.cmdFd = { Tank: live };
  ctx.fdPage = { mode: "debug", explicit: true, loaded: true, open: "Tank", fight: "live", pending: null, hashQ: "" };
  loc._hash = "#/commanders/Tank/fight/live";
  run("fdViewHash('Tank')");
  assert.strictEqual(loc._hash, "#/commanders/Tank/fight/live?f=" + CODE + "&au=" + POISON + "&mk=deaths&lw=120", "a following live view: window, filter and picks; no r, n, rp or sp: " + loc._hash);
  live.view = { key: "live:K1", range: { from: 20000, to: 50000 }, follow: false, reclamp: false };
  run("fdViewHash('Tank')");
  assert.ok(loc._hash.indexOf("r=20000-50000") > 0, "a range the reader fixed is written: " + loc._hash);
  live.liveWin = 60;
  run("fdViewHash('Tank')");
  assert.ok(loc._hash.indexOf("lw=") < 0, "the default window is not written");
}

// ---- the hand-over: the hash becomes the filed fight's, with f / au / mk and a fixed r, and no lw
{
  const live = { sel: "live", userSel: false, liveKey: KEY, death: null, day: "", filter: FILTER, liveWin: 120,
    view: { key: "live:" + KEY, range: { from: 20000, to: 50000 }, follow: false, reclamp: false },
    au: { key: "live:" + KEY, sid: POISON, marker: "deaths", userSid: true, userMark: true }, rp: { on: false, t: 0, playing: false, speed: 1, timer: 0, key: "live" } };
  ctx.cmdFd = { Tank: live };
  ctx.fdLive = { Tank: { key: KEY, cmd: "Tank" } };
  ctx.fdPage = { mode: "debug", explicit: true, loaded: true, open: "Tank", fight: "live", pending: null, hashQ: "" };
  ctx.cmdFindBind = () => null;
  ctx.fdRender = () => {}; ctx.fdLoad = () => {};
  loc._hash = "#/commanders/Tank/fight/live?bot=5&f=" + CODE + "&au=" + POISON + "&mk=deaths&r=20000-50000&lw=120";
  run("fdLiveHandoff('Tank', { key: k })", { k: KEY });
  assert.strictEqual(live.sel, KEY, "the selection is the filed fight");
  assert.strictEqual(loc._hash, "#/commanders/Tank/fight/" + KENC + "?bot=5&r=20000-50000&f=" + CODE + "&au=" + POISON + "&mk=deaths", "the link is the filed fight's: key, f, au, mk and the fixed r; no lw: " + loc._hash);
  assert.ok(!(("Tank") in ctx.fdLive), "the live view is dropped");
  // a following live view: no r after the hand-over
  const live2 = { sel: "live", userSel: false, liveKey: KEY, death: null, day: "", filter: FdFilter.empty(), liveWin: 30,
    view: { key: "live:" + KEY, range: null, follow: true, reclamp: false }, au: { key: "live:" + KEY, sid: null, marker: null, userSid: false, userMark: false },
    rp: { on: false, t: 0, playing: false, speed: 1, timer: 0, key: "live" } };
  ctx.cmdFd = { Tank: live2 };
  ctx.fdLive = { Tank: { key: KEY, cmd: "Tank" } };
  loc._hash = "#/commanders/Tank/fight/live?lw=30";
  run("fdLiveHandoff('Tank', { key: k })", { k: KEY });
  assert.strictEqual(loc._hash, "#/commanders/Tank/fight/" + KENC, "a following view becomes the whole fight: just the key");
  assert.ok(grab("fdLiveHandoff").indexOf("fdFightHash(fd, true);\n      fdViewHash(key);") > 0, "the hand-over writes the view right after the new key");
}

// ---- fdApplyPending: once, when the fight is on screen; the range held inside it; an aura the fight lacks is ignored
{
  const fight = { dur_ms: 200000, auras: [[10000, 1, POISON, 1], [40000, 1, POISON, -1]], bots: [] };
  const fdNew = () => ({ sel: KEY, filter: FdFilter.empty(), liveWin: 60, evShown: 1000, evWhy: "x", view: FdView.create(KEY), au: { key: KEY, sid: null, marker: null, userSid: false, userMark: false },
    rp: { on: false, t: 0, playing: false, speed: 1, timer: 0, key: KEY } });
  const q = (s, o) => FdView.fromQuery(new URLSearchParams(s), o);
  ctx.fdPage = { pending: { cmd: "Tank", key: KEY, q: q("r=1000-9000&f=" + CODE + "&au=" + POISON + "&mk=s" + SIPHON + "&rp=95000&sp=2") } };
  let fd = fdNew();
  assert.strictEqual(run("fdApplyPending(fd, { commander: 'Tank' }, fight, null, null)", { fd, fight }), true, "the replay was set: the caller syncs it");
  assert.deepStrictEqual(J(fd.view), { key: KEY, range: { from: 1000, to: 9000 }, follow: true, reclamp: false }, "the range");
  assert.deepStrictEqual(J(fd.filter), J(FILTER), "the filter");
  assert.strictEqual(fd.evShown, 500, "the events list starts at its first page");
  assert.deepStrictEqual(J(fd.au), { key: KEY, sid: POISON, marker: { sid: SIPHON }, userSid: true, userMark: true }, "the aura pick and marker");
  assert.deepStrictEqual(J(fd.rp), { on: true, t: 95000, playing: false, speed: 2, timer: 0, key: KEY }, "the replay place and speed");
  assert.strictEqual(ctx.fdPage.pending, null, "applied once");
  assert.strictEqual(run("fdApplyPending(fd, { commander: 'Tank' }, fight, null, null)", { fd }), false, "and not again");
  // the range is held inside the fight; a replay place past the end too; an unknown aura is ignored; a marker needs no aura
  ctx.fdPage.pending = { cmd: "Tank", key: KEY, q: q("r=150000-900000&au=999&mk=deaths&rp=86000000") };
  fd = fdNew();
  run("fdApplyPending(fd, { commander: 'Tank' }, fight, null, null)", { fd });
  assert.deepStrictEqual(J(fd.view.range), { from: 150000, to: 200000 }, "held inside the fight");
  assert.strictEqual(fd.au.sid, null, "an aura the fight has no rows for is ignored");
  assert.strictEqual(fd.au.userSid, false);
  assert.strictEqual(fd.au.marker, "deaths");
  assert.strictEqual(fd.rp.t, 200000, "the replay place is held inside the fight");
  ctx.fdPage.pending = { cmd: "Tank", key: KEY, q: q("r=1000-1500") };
  fd = fdNew();
  run("fdApplyPending(fd, { commander: 'Tank' }, fight, null, null)", { fd });
  assert.strictEqual(fd.view.range, null, "a range under a second is no range");
  // it waits for the detail; another card's render or another fight does not take it
  ctx.fdPage.pending = { cmd: "Tank", key: KEY, q: q("r=1000-9000") };
  fd = fdNew();
  assert.strictEqual(run("fdApplyPending(fd, { commander: 'Tank' }, null, null, null)", { fd }), false);
  assert.ok(ctx.fdPage.pending, "no fight detail yet: the link waits");
  run("fdApplyPending(fd, { commander: 'Mage' }, fight, null, null)", { fd });
  assert.ok(ctx.fdPage.pending, "another commander's card does not take it");
  // a live link: the window, a fixed range held inside the running fight, the filter, the picks; it waits while no fight runs
  const st = FdLive.create("Tank"); st.key = "K1"; st.fightMs = 100000; st.fightAtMs = Date.now(); st.startMs = Date.now() - 100000;
  const lf = { key: "K1", auras: [] };
  ctx.fdPage.pending = { cmd: "Tank", key: "live", q: q("lw=300&r=20000-9000000&f=" + CODE + "&au=" + POISON + "&mk=deaths") };
  fd = fdNew(); fd.sel = "live"; fd.au.key = null;
  assert.strictEqual(run("fdApplyPending(fd, { commander: 'Tank' }, null, null, null)", { fd }), false);
  assert.ok(ctx.fdPage.pending, "no live fight yet: the link waits");
  run("fdApplyPending(fd, { commander: 'Tank' }, null, st, lf)", { fd, st, lf });
  assert.strictEqual(fd.liveWin, 300, "the live window");
  assert.strictEqual(fd.view.follow, false, "a range in a live link fixes the range");
  assert.strictEqual(fd.view.key, "live:K1");
  assert.ok(fd.view.range.from === 20000 && fd.view.range.to <= 101000 && fd.view.range.to > 99000, "held inside the running fight: " + JSON.stringify(fd.view.range));
  assert.deepStrictEqual(J(fd.filter), J(FILTER));
  assert.deepStrictEqual(J(fd.au), { key: "live:K1", sid: POISON, marker: "deaths", userSid: true, userMark: true }, "the picks are keyed to this live fight");
  assert.strictEqual(ctx.fdPage.pending, null);
  // a live link with no range stays following
  ctx.fdPage.pending = { cmd: "Tank", key: "live", q: q("lw=0") };
  fd = fdNew(); fd.sel = "live";
  run("fdApplyPending(fd, { commander: 'Tank' }, null, st, lf)", { fd });
  assert.deepStrictEqual([fd.liveWin, fd.view.follow, fd.view.range], [0, true, null], "lw=0 is the whole fight, following");
}

// ---- "No live fight right now.": a live link keeps its pick while no fight runs; the rest of the debugger picks as ever
{
  const fd = { sel: "live", userSel: true, liveKey: null, liveLink: true, list: [], day: "" };
  ctx.fdLive = {};
  ctx.fdFightHash = () => {}; ctx.fdViewHash = () => {};
  const bind = { commander: "Tank", bots: [], mobs: [] };
  run("fdSyncSel(fd, bind, [], [], null)", { fd, bind });
  assert.strictEqual(fd.sel, "live", "a live link with no live fight stays on live (no other fight is picked)");
  const fd2 = { sel: "live", userSel: true, liveKey: null, liveLink: false, list: [], day: "" };
  run("fdSyncSel(fd, bind, [], [], null)", { fd: fd2, bind });
  assert.strictEqual(fd2.sel, null, "without the link flag the ended live fight hands the pick back, as before");
  assert.ok(src.includes("noLive: 'No live fight right now.'") && grab("renderFightDebug").includes("const emptyNote = waitLive ? FD_EMPTY.noLive : fdEmptyNote("), "the one line");
  assert.ok(grab("renderFightDebug").includes("const waitLive = selLive && !live && !!fd.liveLink;") && grab("renderFightDebug").includes("selLive && !waitLive && fdPage.open === key"), "and no live subscription while it waits");
}

// ---- fdSetDay: the night picker's body, shared by the picker and a link
{
  const calls = [];
  ctx.fdSetDay = realSetDay;
  const fd = { day: "", hist: { "night:2026-11-06": 1 }, sel: "k", userSel: true, death: 2, list: [{ key: "k" }] };
  ctx.cmdFd = { Tank: fd };
  Object.assign(ctx, { fdState: (k) => ctx.cmdFd[k], fdRunStopAll: () => calls.push("stop"), cmdFindBind: () => ({ commander: "Tank" }), fdLoad: () => calls.push("load"), fdRender: () => calls.push("render") });
  run("fdSetDay({}, 'Tank', '2026-11-06')");
  assert.deepStrictEqual(J([fd.day, fd.sel, fd.userSel, fd.death, fd.list, "night:2026-11-06" in fd.hist]), ["2026-11-06", null, false, null, [], false], "the night is set, the pick and list cleared, the night's cache dropped");
  assert.deepStrictEqual(calls, ["stop", "load", "render"]);
  calls.length = 0;
  run("fdSetDay({}, 'Tank', '', { quiet: true })");
  assert.deepStrictEqual([fd.day, calls], ["", ["stop"]], "quiet (a link being restored): no load, no render");
  const handler = src.slice(src.indexOf("matches('[data-fd-day]')"), src.indexOf("matches('[data-fd-day]')") + 200);
  assert.ok(handler.includes("fdSetDay(card, key, e.target.value || '')"), "the picker calls it");
}

// ---- where the hash is written, and where it must never be
for (const [fn, why] of [["fdReplayTick", "the 250 ms replay tick"], ["fdLivePaintDebug", "the 1 s live repaint"], ["fdLiveTickSync", "the live tick"], ["fdLivePaintBar", "the live bar paint"], ["fdReplaySync", "a replay sync"]]) {
  assert.ok(!grab(fn).includes("fdViewHash"), "never from " + why);
}
const vh = grab("fdViewHash");
assert.ok(vh.includes("history.replaceState(") && !vh.includes("pushState") && !vh.includes("applyRoute") && !/location\.hash = [^=]/.test(vh), "replaceState only: Back is not filled, nothing is re-applied");
for (const [what, needle] of [
  ["a range drag", "fdRender(key);\n    fdViewHash(key);\n  }"], ["the range buttons", "fdRender(key);\n          fdViewHash(key);"], ["the live window", "if (fdLive[key]) fdLivePaintDebug(key);\n        fdViewHash(key);"],
  ["the aura and marker picks", "fd.au.userMark = true;\n        }\n        fdRender(key);\n        fdViewHash(key);"], ["a filter edit", "fdViewHash(key); // an edit"],
  ["a replay control", "fdViewHash(key); // a pause"], ["a scrub (settled)", "fdViewHashSoon(key);"], ["a picked fight (and its night)", "fdViewHash(fdPage.open); // a night's own fight"],
]) assert.ok(src.includes(needle), "the view is written after " + what);


// ---- Copy link
{
  const fd = { sel: KEY, view: FdView.create(KEY), liveWin: 60, copy: null, linkBad: null };
  const painted = [];
  const card = { querySelector: (s) => (s === ".fd-rangebar" ? bar : null) };
  const bar = { querySelector: () => null };
  Object.assign(ctx, { cmdFd: { Tank: fd }, cmdCards: new Map([["Tank", card]]), fdSet: (n, html) => painted.push(html), fdRangeBarHtml: () => "<bar>", fdViewHash: () => { painted.push("hash"); } });
  const flush = () => new Promise((res) => setImmediate(res));
  (async () => {
    ctx.navigator = { clipboard: { writeText: (u) => { painted.push("copy " + u); return Promise.resolve(); } } };
    timers.length = 0;
    run("fdCopyLink('Tank', fd)", { fd });
    await flush();
    assert.deepStrictEqual(painted.slice(0, 2), ["hash", "copy " + loc.href], "the hash is brought up to date, then the address is copied");
    assert.ok(painted[2].includes(">Copied</button>") && !painted[2].includes("fd-linkbox"), "the button reads Copied: " + painted[2]);
    assert.ok(timers.some((t) => t.ms === 2000), "for 2 s");
    timers.find((t) => t.ms === 2000).f();
    assert.ok(painted[painted.length - 1].includes(">Copy link</button>"), "then it reads Copy link again");
    // a browser that refuses the clipboard: the link in a read-only box
    painted.length = 0;
    ctx.navigator = { clipboard: { writeText: () => Promise.reject(new Error("denied")) } };
    run("fdCopyLink('Tank', fd)", { fd });
    await flush();
    assert.ok(painted[painted.length - 1].includes('<input class="fd-linkbox" type="text" readonly value="' + loc.href + '"'), "the box: " + painted[painted.length - 1]);
    assert.ok(painted[painted.length - 1].includes(">Copy link</button>"), "and the button is not Copied");
    // no clipboard at all (no navigator, or a throwing one)
    painted.length = 0;
    delete ctx.navigator;
    run("fdCopyLink('Tank', fd)", { fd });
    await flush();
    assert.ok(painted[painted.length - 1].includes("fd-linkbox"), "no navigator: the box");
    painted.length = 0;
    ctx.navigator = { clipboard: { writeText: () => { throw new Error("sync"); } } };
    run("fdCopyLink('Tank', fd)", { fd });
    await flush();
    assert.ok(painted[painted.length - 1].includes("fd-linkbox"), "a throwing clipboard: the box");
    // the button and the box are part of the range bar (post-fight and live) and the click goes to the copy
    assert.ok(src.includes("fdRangeBarHtml(fd.view, true, fd.liveWin) + fdLinkTail(fd)") && src.includes("fdRangeBarHtml(fd.view, false, 0) + fdLinkTail(fd)"), "the range bar carries the button in both views");
    assert.ok(src.includes("e.target.closest('[data-fd-copylink]')) { fdCopyLink(key, fd); return; }"), "the click goes to the copy");
    assert.ok(src.includes("'data-fd-evnew', 'data-fd-copylink']"), "the keyboard stays on the button across a repaint");
    console.log("fdshare_ui_check.js ok");
  })().catch((e) => { console.error(e); process.exit(1); });
}

