"use strict";
// Run by commandersjs_test.go under node: the lean fight rail (owner decisions 2026-11-01). Details load only for the opened
// fight; bosses, wipes and fights with deaths are cards while clean trash folds into one line per leg and run; one "Show all
// fights" box (kept per viewer, storage errors survived) restores every card; Tonight is the current run plus two hours with a
// "Load older" row; the rail repaints by key, touching only the cards whose summary changed; the run strip marks a summary's deaths.
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
function grabConst(name) {
  const re = new RegExp(`^  const ${name} = `, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  return src.slice(m.index, src.indexOf("\n", m.index) + 1);
}

const store = {};
const ctx = { console, window: { localStorage: { getItem: (k) => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } } } };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(dir, "fightdebug.js"), "utf8"), ctx, { filename: "fightdebug.js" });
vm.runInContext([
  grab("esc"), grab("numOr0"), grab("fdRailHtml"), grab("fdRailParts"), grab("fdRailCardHtml"), grab("fdRailFoldHtml"), grab("fdRailPatch"),
  grab("fdShowAllGet"), grab("fdShowAllSet"), grab("fdMergeRows"), grab("fdTodayDay"), grab("fdTonightRows"), grab("fdWinList"), grab("fdRunModel"),
  grab("fdRunHtml"), grab("fdRunPct"), grab("fdRunT"), grab("fdRunClock"), grab("fdOlderState"),
  grabConst("FD_SHOWALL_KEY"), grabConst("FD_WINDOW_S"),
  "var cmdFightsCache = {};",
  "var cmdClock = (s) => String(s);",
  "var FightHistory = undefined;",
].join("\n"), ctx, { filename: "app.js lean" });
// Values built inside the vm carry its own Array prototype: compare them as plain JSON.
const plain = (v) => JSON.parse(JSON.stringify(v));
const deq = (a, b, m) => assert.deepStrictEqual(plain(a), plain(b), m);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const F = ctx.FightDebug;

// ---- a night: trash, a death, more trash, a boss, trash on the next leg (newest first, as the daemon lists them) -------------
const T = 100000;
const row = (key, i, extra) => Object.assign({ key, start: T + i * 60, dur: 30000, step: i, session: 7, leg: "1", deaths: 0, outcome: "kill" }, extra || {});
const ROWS = [
  row("t9", 9, { leg: "2" }), row("t8", 8, { leg: "2" }),
  row("boss", 7, { boss_name: "Lucifron" }),
  row("t6", 6), row("t5", 5), row("t4", 4, { warnings: 1 }),
  row("d3", 3, { deaths: 2 }),
  row("t2", 2), row("t1", 1), row("t0", 0),
];
const cards = F.railCards(ROWS, {}, null, { asked: 0 });

// ---- folding ---------------------------------------------------------------------------------------------------------------
const items = F.railItems(cards, { showAll: false, open: {}, sel: null });
const shape = items.map((it) => (it.kind === "fold" ? "fold" + it.n + ":" + it.leg : it.card.key));
deq(shape, ["fold3:1", "d3", "fold3:1", "boss", "fold2:2"],
  "clean trash folds per leg between the cards that matter (a fired rule alone does not make a card): " + JSON.stringify(shape));
assert.strictEqual(items[0].id, "fold:t0", "a fold's id is its oldest fight's key, so it stays put while trash joins it");
assert.ok(items.every((it) => it.kind === "card" || !it.open), "folds start closed");
assert.ok(F.railFull({ wipe: true }) && F.railFull({ deaths: 1 }) && F.railFull({ boss: true }) && F.railFull({ live: true }) && !F.railFull({ deaths: 0 }),
  "bosses, wipes, deaths and the running fight are always cards");
// A single clean fight between two cards stays a card (a fold of one would hide nothing).
deq(F.railItems(F.railCards([row("a", 2, { deaths: 1 }), row("b", 1), row("c", 0, { deaths: 1 })], {}, null, { asked: 0 }), {}).map((it) => it.kind),
  ["card", "card", "card"]);
// Another run (session) starts a new fold.
const twoRuns = F.railCards([row("s2b", 3, { session: 8 }), row("s2a", 2, { session: 8 }), row("s1b", 1), row("s1a", 0)], {}, null, { asked: 0 });
deq(F.railItems(twoRuns, {}).map((it) => it.id), ["fold:s1a", "fold:s2a"], "one fold per run");

// Opened in place: the fold line stays, its cards follow it; the selected fight's fold opens by itself, an explicit close wins.
const opened = F.railItems(cards, { open: { "fold:t4": true } });
assert.strictEqual(opened[2].open, true);
const selItems = F.railItems(cards, { open: {}, sel: "t1" });
assert.strictEqual(selItems[0].open, true, "the fold holding the picked fight opens");
assert.strictEqual(F.railItems(cards, { open: { "fold:t0": false }, sel: "t1" })[0].open, false, "the reader can close it again");

const parts = run("fdRailParts(cards, null, null, '', opts)", { cards, opts: { showAll: false, open: { "fold:t4": true }, older: null } });
deq(parts.map((p) => p.k), ["fold%3At0", "f:d3", "fold%3At4", "f:t4", "f:t5", "f:t6", "f:boss", "fold%3At8"],
  "parts in reading order, an open fold followed by its cards: " + JSON.stringify(parts.map((p) => p.k)));
const foldHtml = parts[0].html;
assert.ok(foldHtml.includes(">Leg 1 · 3 trash pulls, 0 deaths<"), "the fold line says how many clean pulls: " + foldHtml);
assert.ok(foldHtml.includes('aria-expanded="false"') && foldHtml.includes('data-fd-fold="fold%3At0"'), "a closed fold is a button that opens");
assert.ok(parts[2].html.includes('aria-expanded="true"') && parts[3].html.includes("fd-infold"), "an open fold's cards sit under it");
parts.forEach((p) => assert.ok(p.html.includes(`data-fd-k="${p.k}"`), "every part carries its key on its root: " + p.k));

// ---- show all fights: every fight a card, the choice kept per viewer, a broken storage survived ---------------------------
const all = run("fdRailParts(cards, null, null, '', opts)", { cards, opts: { showAll: true, open: {}, older: null } });
assert.strictEqual(all.length, ROWS.length, "show all: one card per fight, nothing folded");
assert.ok(all.every((p) => p.k.startsWith("f:")));
assert.strictEqual(run("fdShowAllGet()"), false, "off by default");
run("fdShowAllSet(true)");
assert.strictEqual(store["tbdash.fdShowAll"], "1", "kept in this browser");
assert.strictEqual(run("fdShowAllGet()"), true);
run("fdShowAllSet(false)");
assert.strictEqual(run("fdShowAllGet()"), false);
ctx.window.localStorage = { getItem: () => { throw new Error("blocked"); }, setItem: () => { throw new Error("blocked"); } };
assert.strictEqual(run("fdShowAllGet()"), false, "a blocked storage reads as off");
assert.doesNotThrow(() => run("fdShowAllSet(true)"), "a blocked storage does not break the toggle");
assert.ok(src.includes('<input type="checkbox" data-fd-showall> Show all fights'), "the rail offers the box");
assert.ok(/matches\('\[data-fd-showall\]'\)\) \{\s*\n[^\n]*\n\s*fdShowAllSet\(!!e\.target\.checked\);/.test(src), "the box saves the choice");
assert.ok(/if \(fd\.showAll\) fdDayFights\(fd, bind, today, /.test(src) && /else fdWinFights\(fd, bind, fd\.winSince, /.test(src),
  "show all lists the whole of today, otherwise the two-hour window");

// ---- the summary verdicts ----------------------------------------------------------------------------------------------
const railAll = run("fdRailHtml(cards, null, null)", { cards });
assert.ok(railAll.includes(">deaths<") && railAll.includes(">check<") && railAll.includes(">no deaths<"), "unopened cards read their summaries");
assert.strictEqual(F.worstSummaryIndex(ROWS), ROWS.findIndex((r) => r.key === "d3"), "the pick is the fight with deaths");
assert.strictEqual(F.worstSummaryIndex([row("w", 1, { outcome: "wipe" }), row("d", 0, { deaths: 9 })]), 0, "a wipe beats deaths");
assert.strictEqual(F.worstSummaryIndex([row("a", 1), row("b", 0)]), -1, "all clean: no worst (the newest is picked)");

// ---- load older ------------------------------------------------------------------------------------------------------
const older = run("fdRailParts(cards, null, null, '', opts)", { cards, opts: { showAll: false, open: {}, older: { more: true, loading: false, n: 0 } } });
assert.strictEqual(older[0].k, "older", "the Load older row sits at the top (the rail reads oldest first)");
assert.ok(older[0].html.includes(">Load older fights<") && !older[0].html.includes("disabled"));
const done = run("fdRailParts([], null, null, '', opts)", { opts: { older: { more: false, loading: false, n: 50 } } });
assert.ok(done[0].html.includes("No older fights stored") && done[0].html.includes("disabled") && done[0].html.includes("50 loaded"));
assert.ok(/'&before=' \+ encodeURIComponent\(before\)\s*\n\s*\+ '&limit=' \+ FD_OLDER_PAGE/.test(src), "older pages ask the cursor and the page size");
assert.ok(src.includes("r.headers.get('X-More') === '1'"), "the daemon's X-More decides whether another page exists");

// ---- Tonight's window -------------------------------------------------------------------------------------------------
const live = [row("L", 20, { live: true }), row("m2", 15), row("m1", 1)];
run("cmdFightsCache = { G: { list: live } }", { live });
const fdW = { day: "", winSince: T + 10 * 60, showAll: false, hist: { ["win:" + (T + 600)]: { list: [row("h1", 12, { hist: true })] } } };
deq(run("fdTonightRows(fd, 'G').map(r => r.key)", { fd: fdW }), ["L", "m2", "h1"],
  "the window: live rows inside it and the window's stored list; the memory list's older rows wait for Load older");
fdW.older = { day: "", rows: [row("o1", 0)] };
deq(run("fdTonightRows(fd, 'G').map(r => r.key)", { fd: fdW }), ["L", "m2", "h1", "m1", "o1"],
  "a loaded older page widens the window back to its oldest row");
fdW.older = { day: "2026-10-01", rows: [row("x", 0)] };
assert.ok(!run("fdTonightRows(fd, 'G').map(r => r.key)", { fd: fdW }).includes("x"), "older pages of another night are not mixed in");

// The window moved a minute on: until its list arrives the rail keeps the previous window's rows instead of dropping them.
const fdMove = { day: "", winSince: T + 660, showAll: false, hist: { ["win:" + (T + 600)]: { list: [row("h1", 12, { hist: true })] } } };
deq(run("fdTonightRows(fd, 'G').map(r => r.key)", { fd: fdMove }), ["L", "m2", "h1"], "no flicker when the window moves");
assert.ok(/if \(list\) Object\.keys\(fd\.hist\)\.forEach\(k => \{ if \(k\.indexOf\('win:'\) === 0 && k !== 'win:' \+ since/.test(src),
  "an older window's list is dropped once the new one is in (fd.hist does not grow by a list a minute)");

// ---- keyed redraws ------------------------------------------------------------------------------------------------------
function el(k, html) {
  return { dataset: { fdK: k }, _fdHtml: undefined, html, parent: null, contains(n) { return n === this; }, focus(o) { focused.push([k, o]); },
    remove() { if (this.parent) { this.parent.children.splice(this.parent.children.indexOf(this), 1); this.parent = null; } } };
}
const focused = [];
const nav = { children: [], firstChild: null,
  insertBefore(n, ref) { if (n.parent) n.remove(); n.parent = this; const i = ref ? this.children.indexOf(ref) : -1; if (i < 0) this.children.push(n); else this.children.splice(i, 0, n); return n; } };
let built = 0;
const make = (html) => { built++; const k = /data-fd-k="([^"]*)"/.exec(html)[1]; return el(k, html); };
ctx.document = { activeElement: null };
const paint = (p) => run("fdRailPatch(nav, parts, make)", { nav, parts: p, make });
const p1 = run("fdRailParts(cards, null, null, '', opts)", { cards, opts: { showAll: false, open: {}, older: null } });
assert.strictEqual(paint(p1), p1.length, "the first paint builds every part");
const nodes1 = nav.children.slice();
built = 0;
assert.strictEqual(paint(p1), 0, "the same rail again touches nothing");
assert.strictEqual(built, 0);
assert.ok(nav.children.every((n, i) => n === nodes1[i]), "every node is kept");
// One fight's summary changes (a death recorded): only its card is rebuilt; a new fight adds one node.
const rows2 = ROWS.slice();
rows2[1] = row("t8", 8, { leg: "2", deaths: 1 });
rows2.unshift(row("t10", 10, { leg: "2" }));
const p2 = run("fdRailParts(cards, null, null, '', opts)", { cards: F.railCards(rows2, {}, null, { asked: 0 }), opts: { showAll: false, open: {}, older: null } });
built = 0;
const changed = paint(p2);
deq(nav.children.map((n) => n.dataset.fdK), p2.map((p) => p.k), "the order follows the parts");
assert.ok(nav.children.slice(0, 4).every((n, i) => n === nodes1[i]), "the cards above the change keep their nodes");
assert.ok(changed <= 4 && built <= 3, "only the changed parts are rebuilt: " + changed + " changes, " + built + " built");
// Focus: a rebuilt card that had the keyboard hands it to its new node without scrolling.
const target = nav.children.find((n) => n.dataset.fdK === "f:t8");
ctx.document.activeElement = target;
const p3 = p2.map((p) => (p.k === "f:t8" ? { k: p.k, html: p.html.replace("1 death", "2 deaths") } : p));
paint(p3);
deq(focused.map((f) => f[0]), ["f:t8"], "focus follows the rebuilt card");
assert.strictEqual(focused[0][1].preventScroll, true);
ctx.document.activeElement = null;
// Gone parts are removed.
paint(p3.slice(0, 2));
deq(nav.children.map((n) => n.dataset.fdK), p3.slice(0, 2).map((p) => p.k));
assert.ok(/fdRailPatch\(card\.querySelector\('\.fd-rail'\), fdRailParts\(cards, live, fd\.sel, fd\.day, \{/.test(src), "the card paints the rail by key");
assert.ok(!/fdSet\(card\.querySelector\('\.fd-rail'\)/.test(src), "the rail is no longer swapped whole");

// ---- detail on demand (the loader itself is driven in fightdebug_state_check.js) ------------------------------------------
assert.ok(!/FD_DETAIL_MAX/.test(src), "no newest-50 detail walk is left");
assert.ok(/asked: 0, retry: FD_DETAIL_TRIES/.test(src), "the rail asks no row's detail up front");

// ---- the run strip from summaries ------------------------------------------------------------------------------------------
const model = run("fdRunModel({ bound_at: 0 }, rows, {}, now)", { rows: ROWS, now: T + 700 });
const marks = model.deaths.filter((d) => d.n);
deq(marks.map((d) => [d.key, d.n]), [["d3", 2]], "a fight with deaths and no detail gets one counted mark");
assert.strictEqual(marks[0].tS, T + 180 + 15, "mid-fight");
const strip = run("fdRunHtml(model, null, { run: { t: null } })", { model });
assert.ok(strip.includes("2 deaths in this fight (open it for who and when)") && strip.includes("<sub>2</sub>"), strip);
const withDet = run("fdRunModel({ bound_at: 0 }, rows, det, now)", { rows: ROWS, now: T + 700, det: { d3: { fight: { deaths: [{ name: "A", t_ms: 1000 }, { name: "B", t_ms: 2000 }] } } } });
deq(withDet.deaths.map((d) => d.name), ["A", "B"], "an opened fight puts each death at its own time");

console.log("fd_lean_check: ok");
