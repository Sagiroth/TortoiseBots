"use strict";
// Run by commandersjs_test.go under node: the boss mechanics wiring in app.js (web/static/js/app.js): the night's table draws one row
// per boss (the key and second metric per boss, a null Hex or dose as the "not deployed" text, the worst-fight button carrying the fight's
// key and night), "This fight: ..." above it, every game name escaped; a live tile's mechanic slot carries the escaped row text and its title;
// the debugger's live bar shows the row; the section asks the daemon once per 30 s and on a window change, only while open; the page loads
// fdmech.js between fightauras.js and app.js with bumped versions.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");
const jsDir = path.dirname(appPath);
const indexHtml = fs.readFileSync(path.join(jsDir, "..", "..", "index.html"), "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const FightAuras = require(path.join(jsDir, "fightauras.js"));
const FdMech = require(path.join(jsDir, "fdmech.js"));
const FdLive = require(path.join(jsDir, "fdlive.js"));
const ctx = { console, document: { activeElement: null }, FightAuras, FdMech, FdLive, cmdCards: new Map(), Underperf: { query: () => "since=100&run=all" }, fdNightParams: () => "", fdUnderWin: () => "w1", jsonOrThrow: (r) => r.json() };
vm.createContext(ctx);
vm.runInContext(["var FD_LIVE_TILE_EVENTS = 3; const fdAuMemo = new WeakMap(); var fdLive = {}; var cmdFindBind = () => null;",
  ["esc", "setHtml", "fdSet", "fdAuIv", "fdLiveClock", "fdLiveHpOf", "fdLiveNumsText", "fdLiveTileHtml", "fdLiveTilePatch", "fdMechLine", "fdMechSamples", "fdLiveMechOf", "fdMechTitleAttr",
    "fdLiveMechBar", "fdMechRowHtml", "fdMechHtml", "fdMechSync"].map(grab).join("\n"), (src.match(/^  const FD_MECH_[A-Z_]+ = .*;$/gm) || []).join("\n")].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const count = (s, re) => (s.match(re) || []).length;
const EVIL = "<b>x</b>";

// ---- the night's table: one row per boss
const rows = [
  { boss: 14834, name: EVIL, kind: "hakkar", fights: 3, kills: 2, wipes: 1, deaths_per_fight: 2, worst: "Cmd\u00007\u0000h-1", casts: 6, cycles: 6, dosed_cycles: 2, dosed_pct: 63.6, son_deaths_per_fight: 1.5, aura_fights: 3, lower: false },
  { boss: 11380, name: "Jin'do", kind: "jindo", fights: 2, kills: 0, wipes: 2, deaths_per_fight: 4, worst: "Cmd\u00007\u0000j-2", hex_fights: 2, hex_mt_s_per_fight: 14.6, shade_deaths_per_fight: 2.5, shade_share_pct: 62.5, lower: false },
  { boss: 4000, name: "Some Boss", kind: "generic", fights: 1, kills: 1, wipes: 0, deaths_per_fight: 0.5, worst: "" },
];
{
  const html = run("fdMechHtml(line, st, day)", { line: { text: "Hakkar: dosed 1/2 cycles · Son pulled 1/2", title: "from <aura rows>" }, st: { res: { rows, fights: 6, window: {} } }, day: "2026-11-08" });
  assert.strictEqual(count(html, /class="fd-mechrow"/g), 3, "one row per boss: " + html);
  assert.ok(/<p class="fd-mechnow" title="from &lt;aura rows&gt;">This fight: Hakkar: dosed 1\/2 cycles · Son pulled 1\/2<\/p>/.test(html), "this fight's line comes first, escaped: " + html.slice(0, 250));
  assert.ok(html.indexOf("fd-mechnow") < html.indexOf("fd-mechtable"), "above the table");
  assert.ok(html.includes("dosed at siphon 64 % (2/6 cycles)") && html.includes("Son deaths/fight 1.5"), "Hakkar: the dosed share and the Son deaths");
  assert.ok(html.includes("Hex on MT 15 s/fight") && html.includes("Shade deaths/fight 2.5 (63 %)"), "Jin'do: Hex time on the main tank, Shade deaths with their share: " + html);
  assert.ok(html.includes("deaths/fight 0.5"), "any other boss: deaths per fight");
  assert.ok(!html.includes(EVIL) && html.includes("&lt;b&gt;x&lt;/b&gt;"), "a boss name is shown as text");
  for (const m of html.matchAll(/="([^"]*)"/g)) assert.ok(!/[<>]/.test(m[1]), "an attribute value broke out: " + m[1]);
  assert.strictEqual(count(html, /"/g) % 2, 0, "balanced quotes");
  // the worst-fight link: the fight's key and its night, the pack table's button the debugger's click handler opens
  const links = Array.from(html.matchAll(/<button type="button" class="fd-link fd-mechworst" data-fd-open="([^"]*)" data-fd-day="([^"]*)">/g), (m) => [decodeURIComponent(m[1]), m[2]]);
  assert.deepStrictEqual(links, [["Cmd\u00007\u0000h-1", "2026-11-08"], ["Cmd\u00007\u0000j-2", "2026-11-08"]], "a link for each boss that has a worst fight, none for the one without");
  // the night table counts Son DEATHS and never says "pulled" (pulls need position samples: the fight line has them)
  const table = html.slice(html.indexOf('<div class="fd-mechtable">'));
  assert.ok(!/pulled/i.test(table), "no 'pulled' in the night table: " + table);
  assert.ok(table.includes('<td title="Son pulls need position samples; the fight line shows them while the fight&#39;s track is held">Son deaths/fight 1.5</td>'), "the Son cell carries the title: " + table.slice(table.indexOf("Son deaths") - 150, table.indexOf("Son deaths") + 30));
  assert.ok(html.includes("Son pulled 1/2") && html.indexOf("Son pulled") < html.indexOf("fd-mechtable"), "the fight line keeps 'Son pulled'");
  assert.ok(html.includes("<th>Worst fight</th>") && html.includes("<th>Key metric</th>") && html.includes("<th>Second metric</th>"));
}
// a null Hex or dose: the watch row is not deployed
{
  const nulls = [{ boss: 14834, name: "Hakkar", kind: "hakkar", fights: 1, kills: 1, wipes: 0, dosed_pct: null, son_deaths_per_fight: 0 }, { boss: 11380, name: "Jin'do", kind: "jindo", fights: 1, kills: 1, wipes: 0, hex_mt_s_per_fight: null, shade_deaths_per_fight: 0, shade_share_pct: null }];
  const html = run("fdMechHtml(null, st, '')", { st: { res: { rows: nulls } } });
  assert.ok(html.includes("no Hex aura rows: the watch row is not deployed"), "Jin'do with no Hex rows: " + html);
  assert.ok(html.includes("no aura rows: the watch row is not deployed"), "Hakkar with no aura rows");
  assert.ok(!html.includes("This fight"), "no line without a fight");
  assert.ok(html.includes("Shade deaths/fight 0.0") && !html.includes("%)"), "no share without deaths");
  assert.ok(!html.includes("fd-mechworst") && html.includes("<td>-</td></tr>"), "no worst fight: a dash");
  const lower = run("fdMechRowHtml(r, '')", { r: { boss: 14834, name: "H", kind: "hakkar", fights: 1, kills: 0, wipes: 0, dosed_pct: 40, dosed_cycles: 1, cycles: 3, lower: true } });
  assert.ok(lower.includes("dosed at siphon ≥40 %"), "a capped aura list: a lower bound");
}
// states: loading, an error, no boss fights, hostile rows
assert.ok(run("fdMechHtml(null, { res: null, err: '' }, '')").includes("Loading the boss mechanics"));
assert.ok(run("fdMechHtml(null, { res: null, err: 'HTTP 500 <b>' }, '')").includes("could not be loaded (HTTP 500 &lt;b&gt;)"));
assert.ok(run("fdMechHtml(null, { res: { rows: [] } }, '')").includes("No boss fight of a minute or more"));
assert.ok(run("fdMechHtml(null, { res: { rows: [null, 5, 'x', {}] } }, '')").includes("fd-mechrow"), "a broken row never throws");
assert.ok(run("fdMechHtml(null, { res: { rows: 'x' } }, '')").includes("No boss fight"), "rows that are not a list");
assert.ok(run("fdMechHtml({ text: 'deaths 1', title: '' }, { res: null }, '')").includes("This fight: deaths 1"), "the line shows while the table loads");

// ---- a live tile: the mechanic slot carries the escaped row text and its title
{
  const tile = { name: "Cmd", title: "ZG", group: "Cmd", boss: "Hakkar", ms: 125000, bossHp: 42, alive: 9, total: 10, dps: 1, hps: 2, events: [], href: "#x",
    mech: { text: "Hakkar: dosed " + EVIL + "/2 cycles", title: "Live: \"2 s\"" } };
  const html = run("fdLiveTileHtml(t)", { t: tile });
  assert.ok(/<div class="fd-tile-mech" title="Live: &quot;2 s&quot;">Hakkar: dosed &lt;b&gt;x&lt;\/b&gt;\/2 cycles<\/div>/.test(html), "escaped text and title: " + html);
  assert.strictEqual(count(html, /"/g) % 2, 0, "balanced quotes");
  const bare = run("fdLiveTileHtml(t)", { t: Object.assign({}, tile, { mech: null }) });
  assert.ok(bare.includes('<div class="fd-tile-mech"></div>'), "no row: the slot stays empty (CSS hides it)");
  // the 1 s patch: a text node is rewritten only when it changed, the title follows
  const node = () => { const n = { textContent: "", attrs: {}, writes: 0, kids: [], classList: { toggle() {} }, dataset: {} };
    Object.defineProperty(n, "textContent", { get() { return n._t || ""; }, set(v) { n._t = v; n.writes++; } });
    n.getAttribute = (k) => (k in n.attrs ? n.attrs[k] : null); n.setAttribute = (k, v) => { n.attrs[k] = v; n.writes++; }; n.removeAttribute = (k) => { delete n.attrs[k]; n.writes++; };
    return n; };
  const mech = node(), tileEl = { querySelector: (s) => (s === ".fd-tile-mech" ? mech : null), querySelectorAll: () => [] };
  run("fdLiveTilePatch(el, t)", { el: tileEl, t: tile });
  assert.strictEqual(mech.textContent, tile.mech.text);
  assert.strictEqual(mech.attrs.title, tile.mech.title);
  const w = mech.writes;
  run("fdLiveTilePatch(el, t)", { el: tileEl, t: tile });
  assert.strictEqual(mech.writes, w, "an unchanged tick writes nothing");
  run("fdLiveTilePatch(el, t)", { el: tileEl, t: Object.assign({}, tile, { mech: null }) });
  assert.strictEqual(mech.textContent, "");
  assert.ok(!("title" in mech.attrs), "the title goes with the row");
}

// ---- the rows of a running fight, from FdLive's state: the tile's and the live bar's text
{
  const KEY = "Cmd\u00001\u00002", T0 = 5000000;
  const st = FdLive.create("Cmd");
  const log = [{ k: "cast", t: 35000, n: "Hakkar", s: "Blood Siphon", sid: 24324 }];
  FdLive.applyEventsFrame(st, { key: KEY, from: 0, cur: 1, events: log, fight: { start_ms: T0, boss: 14834, boss_name: "Hakkar", fight_ms: 40000 } }, T0 + 40000);
  const bind = { bots: [{ name: "A", guid: 1, role: "tank", hp: 1, max_hp: 1 }, { name: "B", guid: 2, role: "dps", hp: 1, max_hp: 1 }], layout: null };
  // both carry Poisonous Blood in the sampled lists: 2 of 2 at the cast
  [T0 + 30000, T0 + 32000, T0 + 34000, T0 + 36000].forEach((t) => FdLive.applySample(st, Object.assign({}, bind, { bots: bind.bots.map((b) => Object.assign({}, b, { db: [{ id: 24321, n: "Poisonous Blood" }] })), mobs: [{ id: "m", entry: 11357, hp: 90, victim: "2" }] }), t));
  ctx.Date = { now: () => T0 + 40000 };
  const m = run("fdLiveMechOf('Cmd', st)", { st, cmdFindBind: () => bind });
  assert.strictEqual(m.text, "Hakkar: dosed 1/1 cycles · Son pulled 1/1", "the tile's row: " + JSON.stringify(m));
  assert.ok(m.title.includes("2 s"), "the sampled note");
  assert.strictEqual(run("fdLiveMechOf('Cmd', null)"), null, "no state: no row");
  assert.strictEqual(run("fdLiveMechOf('Cmd', { key: null })"), null, "no fight yet: no row");
  const bar = { textContent: "", attrs: {} };
  const el = { get textContent() { return bar.textContent; }, set textContent(v) { bar.textContent = v; }, getAttribute: (k) => (k in bar.attrs ? bar.attrs[k] : null), setAttribute: (k, v) => { bar.attrs[k] = v; }, removeAttribute: (k) => { delete bar.attrs[k]; } };
  const card = { querySelector: (s) => (s === ".fd-livemech" ? el : null) };
  const lf = FdLive.fight(st, bind, T0 + 40000);
  run("fdLiveMechBar(card, st, lf, bind)", { card, st, lf, bind });
  assert.strictEqual(bar.textContent, "· Hakkar: dosed 1/1 cycles · Son pulled 1/1", "the live bar, after the clock");
  assert.ok(bar.attrs.title.includes("2 s"));
  run("fdLiveMechBar(card, st, null, bind)", { card, st, bind });
  assert.strictEqual(bar.textContent, "", "no fight: empty");
  // a filed fight's samples come from the x=1 track once
  const track = { raw: { frames: [{ t: (T0 + 20000) / 1000, bots: [[1, 0, 0, 100, 1, 0, 0, 0, [24321]]], mobs: [{ id: "m", e: 11357, hp: 80, v: 2 }], w: 10 }] } };
  const s1 = run("fdMechSamples(t, 'Cmd')", { t: track });
  assert.strictEqual(s1.length, 1);
  assert.strictEqual(run("fdMechSamples(t, 'Cmd')", { t: track }), s1, "converted once per track");
  assert.deepStrictEqual([run("fdMechSamples(null, 'Cmd')").length, run("fdMechSamples('loading', 'Cmd')").length, run("fdMechSamples({}, 'Cmd')").length], [0, 0, 0]);
  assert.strictEqual(run("fdMechLine(null, [], null)"), null);
  assert.strictEqual(run("fdMechLine(f, [], null)", { f: { boss: 0, deaths: [{ name: "x" }], dur_ms: 1 } }).text, "deaths 1", "trash: deaths K");
}

// ---- the section: one request per 30 s and per window, only while open
{
  const asked = [];
  const box = { open: false, dataset: {}, listeners: {}, addEventListener(n, f) { this.listeners[n] = f; } };
  const body = { innerHTML: "" };
  const card = { classList: { contains: () => true }, querySelector: (s) => (s === ".fd-mechbox" ? box : s === ".fd-mechbody" ? body : null) };
  const bind = { commander: "Cmd<&>", history: false };
  const fd = { day: "" };
  let now = 1000000, answer = { rows: [rows[0]], fights: 3, window: {} };
  ctx.Date = { now: () => now };
  ctx.fetch = (url) => { asked.push(url); return Promise.resolve({ json: () => Promise.resolve(answer) }); };
  ctx.cmdCards.set("Cmd<&>", card);
  const sync = (line) => run("fdMechSync(card, bind, fd, line)", { card, bind, fd, line: line || null });
  const settle = () => new Promise((r) => setImmediate(r));
  (async () => {
    sync({ text: "deaths 1", title: "" });
    assert.strictEqual(asked.length, 0, "a closed box asks for nothing");
    assert.strictEqual(body.innerHTML, "", "and draws nothing");
    box.open = true;
    sync({ text: "deaths 1", title: "" });
    assert.strictEqual(asked.length, 1, "open: one request");
    assert.ok(/^\/api\/v1\/combat\/mechanics\?commander=Cmd%3C%26%3E&since=100$/.test(asked[0]), "the commander escaped, the window of the underperforming bots, no run: " + asked[0]);
    assert.ok(body.innerHTML.includes("This fight: deaths 1") && body.innerHTML.includes("Loading the boss mechanics"), "this fight's line shows while the table loads");
    sync(); sync();
    assert.strictEqual(asked.length, 1, "one in flight at a time");
    await settle();
    assert.ok(body.innerHTML.includes("fd-mechrow") && body.innerHTML.includes("&lt;b&gt;x&lt;/b&gt;"), "the answer is drawn: " + body.innerHTML.slice(0, 200));
    now += 10000; sync();
    assert.strictEqual(asked.length, 1, "within 30 s: no new request");
    now += 21000; sync(); await settle();
    assert.strictEqual(asked.length, 2, "after 30 s: one more");
    ctx.fdUnderWin = () => "w2"; sync();
    assert.strictEqual(asked.length, 3, "another window: a new request at once");
    assert.ok(!body.innerHTML.includes("fd-mechrow"), "and the old window's table is not shown meanwhile");
    await settle();
    // an answer for a window the card has left is dropped, and the new window is asked for
    {
      ctx.fdUnderWin = () => "w3";
      let release;
      ctx.fetch = (url) => { asked.push(url); return new Promise((res) => { release = () => res({ json: () => Promise.resolve({ rows: [rows[1]], fights: 1, window: {} }) }); }); };
      now += 40000; sync(); // w3: a request is in flight
      const inflight = asked.length;
      ctx.fdUnderWin = () => "w4"; // the reader picks another night meanwhile
      sync();
      assert.strictEqual(asked.length, inflight, "one in flight at a time, even across a window change");
      release(); await settle();
      assert.ok(!body.innerHTML.includes("fd-mechrow"), "the old window's answer is not shown for the new one: " + body.innerHTML.slice(0, 160));
      ctx.fetch = (url) => { asked.push(url); return Promise.resolve({ json: () => Promise.resolve({ rows: [rows[2]], fights: 1, window: {} }) }); };
      sync(); await settle();
      assert.strictEqual(asked.length, inflight + 1, "the next render asks for the new window");
      assert.ok(body.innerHTML.includes("Some Boss") && !body.innerHTML.includes("Jin&#39;do"), "and shows its own answer");
      ctx.fdUnderWin = () => "w2"; ctx.fetch = (url) => { asked.push(url); return Promise.resolve({ json: () => Promise.resolve(answer) }); };
    }
    // a failing answer is said, and asked again only after 30 s
    ctx.fetch = (url) => { asked.push(url); return Promise.reject(new Error("HTTP 500")); };
    now += 40000; sync(); await settle();
    assert.ok(body.innerHTML.includes("could not be loaded (HTTP 500)"), body.innerHTML);
    const n = asked.length; now += 1000; sync();
    assert.strictEqual(asked.length, n, "an error is not retried every render");
    // the toggle listener asks too
    assert.strictEqual(typeof box.listeners.toggle, "function", "the box listens for its own opening");
    console.log("mechpanel ui ok");
  })().catch((e) => { console.error(e); process.exit(1); });
}

// ---- the page wires it in: the module between fightauras.js and app.js, the changed assets carry new versions, the slots exist
{
  const tags = Array.from(indexHtml.matchAll(/<script src="\/static\/js\/([a-z]+\.js)\?v=(\d+)">/g), (m) => m[1]);
  assert.ok(tags.indexOf("fightauras.js") < tags.indexOf("fdmech.js") && tags.indexOf("fdmech.js") < tags.indexOf("fdlive.js") && tags.indexOf("fdmech.js") < tags.indexOf("app.js"), "fdmech.js loads after fightauras.js and before app.js: " + tags.join(","));
  assert.ok(/fdmech\.js\?v=2/.test(indexHtml) && /fdlive\.js\?v=3/.test(indexHtml) && /static\/js\/app\.js\?v=57/.test(indexHtml) && /app\.css\?v=32/.test(indexHtml), "the changed assets carry new versions");
  assert.ok(src.includes('class="fd-mechbox"') && src.includes('class="fd-mechbody"') && src.includes('class="fd-livemech cell-muted"'), "the section and the live bar slot are in the card");
  assert.ok(src.indexOf('class="fd-mechbox"') > src.indexOf('class="fd-underbox"'), "directly under the underperforming bots");
  assert.ok(/fdMechSync\(card, bind, fd, /.test(src) && /fdLiveMechBar\(card, st, lf, bind\)/.test(src) && /fdLiveMechBar\(card, liveSt, liveFight, bind\)/.test(src), "rendered with the card and repainted by the live tick");
  assert.ok(/mech: canon \? fdLiveMechOf\(name, st\) : null/.test(src), "the tile reads the row for a bound commander only");
}
