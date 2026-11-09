"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's Events panel in app.js (web/static/js/app.js): the table (500 rows then
// "Show 500 more", every name escaped, every row seeks the replay), the filter builder (school and hit disabled, an index path on
// every control), the edits (new trees, invalid ones refused with the reason), the timeline marks the filter dims, a running fight's
// newest-first list and live note, the 1 s repaint that leaves the builder alone, and the focus a repaint of the builder gives back.
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
function grabLine(re) {
  const m = re.exec(src);
  assert.ok(m, `${re} not found`);
  return src.slice(m.index, src.indexOf("\n", m.index) + 1);
}

const FightAuras = require(path.join(jsDir, "fightauras.js"));
const FightEvents = require(path.join(jsDir, "fightevents.js"));
const FdFilter = require(path.join(jsDir, "fdfilter.js"));
const FdLive = require(path.join(jsDir, "fdlive.js"));
const FdView = require(path.join(jsDir, "fdview.js"));
const FightDebug = require(path.join(jsDir, "fightdebug.js"));
const document = { activeElement: null };
const ctx = { console, FightAuras, FightEvents, FdFilter, FdLive, FdView, FightDebug, document, setTimeout, clearTimeout, cmdFd: {}, cmdCards: new Map() };
vm.createContext(ctx);
const names = ["esc", "setHtml", "fdSet", "fdEvBuilt", "fdEvKeep", "fdEvTime", "fdEvCountText", "fdEventsHtml", "fdEvCutHint", "fdFilterCtl", "fdFilterCondHtml",
  "fdFilterGroupHtml", "fdFilterHtml", "fdEvFilterPaint", "fdEventsSync", "fdFilterEdit", "fdEvRepaint", "fdOutCls", "fdMarkOut", "fdRangeBandHtml", "fdTimelineHtml",
  "fdLiveTimelineHtml"];
vm.runInContext([grabLine(/^  const FD_EV_PAGE = /m), grabLine(/^  const FD_EV_DEBOUNCE_MS = /m), grabLine(/^  const FD_EV_LIVE_NOTE = /m), grabLine(/^  const FD_EV_LISTMAX = /m),
  "const fdEvMemo = new WeakMap();"].concat(names.map(grab)).join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const count = (s, re) => (s.match(re) || []).length;
const EVIL = "<img src=x onerror=1>";
const C = (f, op, v, not) => ({ f, op, v, not: !!not });
const G = (op, items, not) => ({ op, not: !!not, items });

const mkEvents = (n, extra) => Array.from({ length: n }, (_, i) => Object.assign({ t: i * 100, k: "cast", src: "Hakkar", srcRole: "enemy", ab: "Zap " + i, sid: i, tgt: "", tgtRole: "", amt: null }, extra && extra(i)));

// ---- the table: 600 events -> 500 rows and "Show 500 more" with the 100 left; the boss-cast click seeks to each row's time
{
  const html = run("fdEventsHtml(list, 500, false, false)", { list: mkEvents(600) });
  assert.strictEqual(count(html, /<tr class="fd-evrow"/g), 500, "500 rows");
  assert.ok(/<button type="button" class="fd-evbtn" data-fd-evmore>Show 500 more<\/button>/.test(html) && html.includes("100 more"), "the button and the count left: " + html.slice(-200));
  assert.strictEqual(count(html, /data-fd-bosscast="\d+"/g), 500, "every row has the seek attribute");
  assert.ok(html.includes('data-fd-bosscast="0"') && html.includes('data-fd-bosscast="49900"') && !html.includes('data-fd-bosscast="50000"'), "the first 500, oldest first");
  assert.ok(!/data-fd-evmore/.test(run("fdEventsHtml(list, 500, false, false)", { list: mkEvents(500) })), "exactly one page: no button");
  const more = run("fdEventsHtml(list, 1000, false, false)", { list: mkEvents(600) });
  assert.strictEqual(count(more, /<tr class="fd-evrow"/g), 600);
  assert.ok(!more.includes("data-fd-evmore"), "all shown: no button");
  assert.ok(run("fdEventsHtml(list, 500, true, false)", { list: mkEvents(3) }).includes("Only the first 5000 events are listed"), "the cap hint");
  assert.ok(run("fdEventsHtml(list, 500, false, false)", { list: [] }).includes("No events match"), "an empty list says so");
  assert.ok(html.includes("<th>Time</th><th>Kind</th><th>Source</th><th>Ability</th><th>Target</th>") && html.includes('<th class="n">Amount</th>'), "the columns");
}
// the time column is m:ss.s; the amount shows on a hit; a kind shows its label
{
  assert.deepStrictEqual([0, 5300, 65500, 3599999].map((t) => run("fdEvTime(t)", { t })), ["0:00.0", "0:05.3", "1:05.5", "59:59.9"]);
  const html = run("fdEventsHtml(list, 5, false, false)", { list: [{ t: 65500, k: "hit", src: "Hakkar", srcRole: "", ab: "", sid: null, tgt: "Dpsy", tgtRole: "dps", amt: 1500 }, { t: 70000, k: "aura_on", src: "", ab: "Hex", tgt: "T", amt: null }] });
  assert.ok(html.includes('<td class="mono">1:05.5</td><td>Hit before death</td><td>Hakkar</td><td></td><td>Dpsy</td><td class="n">1,500</td>'), "a hit row: " + html);
  assert.ok(html.includes("<td>Aura applied</td>"));
}
// every game name is escaped: a source, ability, target, a value in a text box, a value list
{
  const evs = mkEvents(2, (i) => ({ src: EVIL, ab: EVIL, tgt: EVIL }));
  const html = run("fdEventsHtml(list, 5, false, false)", { list: evs });
  assert.ok(!html.includes(EVIL) && html.includes("&lt;img src=x onerror=1&gt;"), "the table: " + html);
  const g = G("and", [C("source", "has", EVIL)]);
  const f = run("fdFilterHtml(g, evs)", { g, evs });
  assert.ok(!f.includes(EVIL), "the builder: no raw markup from a value or a value list");
  assert.ok(f.includes('value="&lt;img src=x onerror=1&gt;"') && f.includes('<option value="&lt;img src=x onerror=1&gt;">'), "the text box and the datalist hold the escaped name");
  assert.ok(!run("fdEventsHtml(list, 5, false, false)", { list: [{ t: '"><script>x</script>', k: "cast", src: "a" }] }).includes("<script>"), "a time is escaped too");
  const why = run("fdFilterHtml(g, [], w)", { g, w: "<b>why</b>" });
  assert.ok(!why.includes("<b>why</b>") && why.includes('role="alert"'), "the refusal text is escaped");
}
// the builder: school and hit are present but disabled with the server note; every control carries its index path; the group
// controls follow the depth (no "+ group" at depth 3, no remove on the root); an op list only holds the ops valid for the field
{
  const g = G("or", [C("kind", "is", "cast"), G("and", [C("ability", "has", "x", true), C("amount", "ge", 100)])]);
  const html = run("fdFilterHtml(g, evs)", { g, evs: [] });
  assert.ok(/<option value="school" disabled title="The game server does not send this">Spell school<\/option>/.test(html), "school is disabled: " + html.slice(0, 700));
  assert.ok(/<option value="hit" disabled title="The game server does not send this">/.test(html), "hit is disabled");
  assert.strictEqual(count(html, /<option value="(school|hit)" disabled/g), 6, "three condition rows, two disabled fields each");
  const paths = (attr) => Array.from(html.matchAll(new RegExp(`${attr}(?=\\s)[^>]*data-fd-fpath="([^"]*)"`, "g")), (m) => m[1]);
  assert.deepStrictEqual(paths("data-fd-fnot"), ["", "0", "1", "1.0", "1.1"], "NOT boxes: the root, the first condition, the group and its two conditions");
  assert.deepStrictEqual(paths("data-fd-fop"), ["", "1"], "All of / Any of per group");
  assert.deepStrictEqual(paths("data-fd-fadd"), ["", "1"]);
  assert.deepStrictEqual(paths("data-fd-fval"), ["0", "1.0", "1.1"], "value boxes");
  assert.deepStrictEqual(paths("data-fd-fdel"), ["0", "1", "1.0", "1.1"], "a remove for each but the root");
  assert.ok(/<option value="or" selected>Any of<\/option>/.test(html) && /<option value="and">All of<\/option>/.test(html));
  assert.ok(/data-fd-fval data-fd-fpath="1\.1"[^>]* value="100"/.test(html), "a number value is shown as text");
  const amount = html.slice(html.indexOf('data-fd-fcop data-fd-fpath="1.1"'));
  assert.ok(amount.slice(0, 260).includes('value="ge" selected>at least') && amount.slice(0, 260).includes('value="le"') && !amount.slice(0, 260).includes('value="has"'), "amount: is / at least / at most");
  const kind = html.slice(html.indexOf('data-fd-fcop data-fd-fpath="0"'), html.indexOf('data-fd-fcop data-fd-fpath="0"') + 200);
  assert.ok(kind.includes('value="has"') && !kind.includes('value="ge"'), "kind: is / has");
  assert.strictEqual(count(html, /data-fd-fpreset="/g), 4, "four presets");
  assert.ok(html.includes("data-fd-fclear"), "Clear");
  assert.ok(html.includes("Showing kind is cast or (not ability has x and amount is at least 100)"), "the line that says what it shows");
  assert.ok(/list="fd-evdl-kind"/.test(html) && /list="fd-evdl-ability"/.test(html) && !/list="fd-evdl-amount"/.test(html), "value lists by field, none for the amount");
  const d3 = G("and", [G("and", [G("and", [])])]);
  const h3 = run("fdFilterHtml(g, [])", { g: d3 });
  assert.strictEqual(count(h3, /data-fd-faddg/g), 2, "'+ group' on the root and the middle group, not on the depth-3 group");
  assert.ok(!/data-fd-fadd [^>]*data-fd-fpath="0\.0\.0"[^>]*>\+ group/.test(h3));
  assert.strictEqual(count(run("fdFilterHtml(g, [])", { g: G("and", []) }), /data-fd-fdel/g), 0, "the root has no remove");
  assert.strictEqual(count(html, /data-fd-fid="[^"]+"/g), count(html, /data-fd-fpath="/g), "every control has a name across repaints");
  const ids = Array.from(html.matchAll(/data-fd-fid="([^"]+)"/g), (m) => m[1]);
  assert.strictEqual(new Set(ids).size, ids.length, "and the names are unique");
  const dl = run("fdFilterHtml(g, evs)", { g: G("and", []), evs: mkEvents(500, (i) => ({ src: "S" + i })) });
  assert.ok(count(dl.slice(dl.indexOf('id="fd-evdl-source"')), /<option value="S/g) <= 200, "a value list is capped");
}
// ---- edits: new trees by path, invalid ones refused with the reason and never stored
{
  const el = (attrs, extra) => Object.assign({ hasAttribute: (a) => a in attrs, getAttribute: (a) => (a in attrs ? attrs[a] : null) }, extra);
  const fd = () => ({ filter: FdFilter.empty(), evShown: 900, evWhy: "" });
  const f = fd();
  assert.ok(run("fdFilterEdit(fd, e)", { fd: f, e: el({ "data-fd-fadd": "", "data-fd-fpath": "" }) }));
  assert.deepStrictEqual(JSON.parse(JSON.stringify(f.filter.items)), [{ f: "kind", op: "is", v: "", not: false }], "+ condition");
  assert.strictEqual(f.evShown, 500, "an edit starts the list at the first page");
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-faddg": "", "data-fd-fpath": "" }) });
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-fadd": "", "data-fd-fpath": "1" }) });
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-fval": "", "data-fd-fpath": "1.0" }, { value: "Hex" }) });
  assert.strictEqual(f.filter.items[1].items[0].v, "Hex", "a value by path");
  // the field: an operator that does not fit the new field becomes "is"
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-fcop": "", "data-fd-fpath": "1.0" }, { value: "has" }) });
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-ffield": "", "data-fd-fpath": "1.0" }, { value: "amount" }) });
  assert.deepStrictEqual([f.filter.items[1].items[0].f, f.filter.items[1].items[0].op], ["amount", "is"], "has does not fit the amount");
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-fcop": "", "data-fd-fpath": "1.0" }, { value: "ge" }) });
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-fval": "", "data-fd-fpath": "1.0" }, { value: "1200" }) });
  assert.strictEqual(f.filter.items[1].items[0].v, 1200, "an amount typed as text is stored as a number");
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-ffield": "", "data-fd-fpath": "1.0" }, { value: "target" }) });
  assert.deepStrictEqual([f.filter.items[1].items[0].f, f.filter.items[1].items[0].op], ["target", "is"], "ge does not fit a text field");
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-fnot": "", "data-fd-fpath": "1" }, { checked: true }) });
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-fop": "", "data-fd-fpath": "" }, { value: "or" }) });
  assert.deepStrictEqual([f.filter.items[1].not, f.filter.op], [true, "or"], "NOT on a group, any of on the root");
  // refused: the stored tree stays, the reason is kept
  const keep = JSON.stringify(f.filter);
  assert.ok(run("fdFilterEdit(fd, e)", { e: el({ "data-fd-ffield": "", "data-fd-fpath": "1.0" }, { value: "school" }) }), "the edit was a filter edit");
  assert.strictEqual(JSON.stringify(f.filter), keep, "school is refused: the tree is not changed");
  assert.ok(/does not send/.test(f.evWhy), "with the reason: " + f.evWhy);
  run("fdFilterEdit(fd, e)", { e: el({ "data-fd-fnot": "", "data-fd-fpath": "" }, { checked: false }) });
  assert.strictEqual(f.evWhy, "", "the next good edit clears the reason");
  // 21 conditions: the 21st is refused
  const g2 = fd();
  for (let i = 0; i < 20; i++) run("fdFilterEdit(fd, e)", { fd: g2, e: el({ "data-fd-fadd": "", "data-fd-fpath": "" }) });
  assert.strictEqual(g2.filter.items.length, 20);
  run("fdFilterEdit(fd, e)", { fd: g2, e: el({ "data-fd-fadd": "", "data-fd-fpath": "" }) });
  assert.strictEqual(g2.filter.items.length, 20, "the 21st condition is refused");
  assert.ok(/20/.test(g2.evWhy));
  // a fourth level of group is refused
  const g3 = fd();
  ["", "0", "0.0"].forEach((p) => run("fdFilterEdit(fd, e)", { fd: g3, e: el({ "data-fd-faddg": "", "data-fd-fpath": p }) }));
  assert.strictEqual(FdFilter.depthOf(g3.filter), 3);
  assert.ok(/nest/.test(g3.evWhy), "the third '+ group' (a fourth level) is refused: " + g3.evWhy);
  // presets, Clear, remove
  const g4 = fd();
  run("fdFilterEdit(fd, e)", { fd: g4, e: el({ "data-fd-fpreset": "deaths" }) });
  assert.strictEqual(FdFilter.describe(g4.filter), "kind is death or kind is hit");
  run("fdFilterEdit(fd, e)", { fd: g4, e: el({ "data-fd-fdel": "", "data-fd-fpath": "0" }) });
  assert.strictEqual(g4.filter.items.length, 1, "remove by path");
  run("fdFilterEdit(fd, e)", { fd: g4, e: el({ "data-fd-fclear": "" }) });
  assert.ok(FdFilter.isEmpty(g4.filter) && g4.filter.items.length === 0, "Clear");
  assert.strictEqual(run("fdFilterEdit(fd, e)", { fd: g4, e: el({ "data-fd-fpreset": "nope" }) }), false, "an unknown preset is not an edit");
  assert.strictEqual(run("fdFilterEdit(fd, e)", { fd: g4, e: el({ "data-other": "" }) }), false, "a control that is not the filter's");
  // an amount that is not a number stays text (the condition is then incomplete and constrains nothing)
  const g5 = fd();
  g5.filter = G("and", [C("amount", "ge", 5)]);
  run("fdFilterEdit(fd, e)", { fd: g5, e: el({ "data-fd-fval": "", "data-fd-fpath": "0" }, { value: "12x" }) });
  assert.strictEqual(g5.filter.items[0].v, "12x");
}
// ---- the timeline: a boss cast or a death whose event the filter does not match is dimmed; the range composes
{
  const fight = { boss: 14834, dur_ms: 100000, bots: [{ name: "Dpsy", guid: 13, role: "dps" }],
    log: [{ k: "cast", t: 10000, n: "Hakkar", s: "Blood Siphon", sid: 24324 }, { k: "cast", t: 30000, n: "Hakkar", s: "Zap", sid: 5 }, { k: "cast", t: 80000, n: "Hakkar", s: "Blood Siphon", sid: 24324 },
      { k: "death", t: 50000, g: 13, n: "Dpsy", by: "Hakkar" }],
    deaths: [{ name: "Dpsy", t_ms: 50000, killer: "Hakkar", hits: [] }] };
  const tl = FightDebug.timeline(fight), recaps = FightDebug.deathRecaps(fight);
  const draw = (filter, range) => run("fdTimelineHtml(tl, recaps, null, range, fdEvKeep(fd, fight, null))", { tl, recaps, range: range || null, fd: { filter }, fight });
  const outs = (html) => ({ casts: Array.from(html.matchAll(/class="fd-bosscast bc-\d( fd-out)?" data-fd-bosscast="(\d+)"/g), (m) => (m[1] ? "out@" : "in@") + m[2]),
    skull: html.includes('fd-skull fd-death fd-out"') ? "out" : html.includes('fd-skull fd-death"') ? "in" : "none" });
  const plain = outs(draw(FdFilter.empty()));
  assert.deepStrictEqual(plain, { casts: ["in@10000", "in@30000", "in@80000"], skull: "in" }, "no filter: nothing dimmed");
  const siphon = outs(draw(G("and", [C("kind", "is", "cast"), C("ability", "has", "siphon")])));
  assert.deepStrictEqual(siphon, { casts: ["in@10000", "out@30000", "in@80000"], skull: "out" }, "only the Blood Siphon casts stay; the death does not match");
  const deaths = outs(draw(G("and", [C("kind", "is", "death")])));
  assert.deepStrictEqual(deaths, { casts: ["out@10000", "out@30000", "out@80000"], skull: "in" }, "deaths only");
  const both = outs(draw(G("and", [C("ability", "has", "siphon")]), { from: 0, to: 60000 }));
  assert.deepStrictEqual(both.casts, ["in@10000", "out@30000", "out@80000"], "the filter and the range both apply: the Siphon at 80 s is outside the range, the Zap fails the filter");
  assert.ok(!draw(G("and", [C("kind", "is", "cast")])).includes("class=\"fd-hit"), "nothing else changes");
  assert.strictEqual(run("fdEvKeep(fd, fight, null)", { fd: { filter: FdFilter.empty() }, fight }), null, "no filter: no keep function, no events built");
  assert.strictEqual(run("fdEvKeep(fd, null, null)", { fd: { filter: G("and", [C("kind", "is", "cast")]) } }), null, "no fight");
  assert.ok(draw(G("and", [C("kind", "is", "cast")])).includes("fd-skull fd-death fd-out"), "a death does not match a cast filter");
}
// ---- a running fight: newest first, the live note, the filter composes with the follow range
{
  const evs = mkEvents(700);
  const live = run("fdEventsHtml(list, 500, false, true)", { list: evs });
  const first = /<tr class="fd-evrow" data-fd-bosscast="(\d+)"/.exec(live);
  assert.strictEqual(first[1], "69900", "a running fight's first row is the newest event: " + first[1]);
  assert.strictEqual(count(live, /<tr class="fd-evrow"/g), 500);
  assert.ok(live.includes('data-fd-bosscast="20000"') && !live.includes('data-fd-bosscast="19900"'), "the 500 newest; Show more pages back in time");
  assert.ok(live.includes("200 more"), "200 left");
  const filed = run("fdEventsHtml(list, 500, false, false)", { list: evs });
  assert.strictEqual(/<tr class="fd-evrow" data-fd-bosscast="(\d+)"/.exec(filed)[1], "0", "a filed fight lists the oldest first");
  assert.ok(run("fdEventsHtml(list, 500, true, true)", { list: evs }).includes("Only the newest 5000 events are listed"), "a running fight keeps the newest");
  assert.strictEqual(run("fdEvCountText(12, 700, true)"), "12 of 700 events (live: target changes and last hits come when the fight ends)");
  assert.strictEqual(run("fdEvCountText(12, 700, false)"), "12 of 700 events");
}
// ---- the panel's paint with fake nodes: the 1 s tick writes the count and the list and never the builder; the builder keeps
// its controls while the reader uses them and gives the keyboard back when it is their own edit
{
  const mkNode = () => {
    const n = { writes: 0, _v: "", _h: undefined, controls: [], hidden: true, children: {} };
    Object.defineProperty(n, "innerHTML", { get() { return n._v; }, set(v) { n._v = v; n.writes++; } });
    n.contains = (el) => !!el && n.controls.indexOf(el) >= 0;
    n.querySelectorAll = (sel) => (sel === "[data-fd-fid]" ? n.controls : []);
    return n;
  };
  const sec = mkNode(), cnt = mkNode(), list = mkNode(), filt = mkNode(), tl = mkNode();
  sec.querySelector = (sel) => ({ ".fd-evcount": cnt, ".fd-evlist": list, ".fd-evfilter": filt })[sel] || null;
  const card = { querySelector: (sel) => (sel === ".fd-events" ? sec : sel === ".fd-tl" ? tl : null) };
  const fight = { key: "K", live: true, dur_ms: 9000, bots: [], log: [{ k: "cast", t: 1000, n: "B", s: "Zap" }, { k: "cast", t: 2000, n: "B", s: "Zip" }], auras: [], deaths: [] };
  const st = FdLive.create("Cmd"); st.version = 4;
  const fd = { filter: G("and", [C("ability", "has", "z")]), evShown: 500, evWhy: "" };
  const ctl = { id: "v:0", focused: 0, sel: null, getAttribute: (a) => (a === "data-fd-fid" ? ctl.id : null), focus() { ctl.focused++; }, setSelectionRange(a, b) { ctl.sel = [a, b]; }, selectionStart: 2, selectionEnd: 3 };
  const sync = (o) => run("fdEventsSync(card, fd, 'live:K', fight, o)", { card, fd, fight, o: Object.assign({ range: null, st }, o) });
  sync({});
  assert.strictEqual(sec.hidden, false, "a fight shows the section");
  assert.ok(cnt.innerHTML.includes("2 of 2 events (live: target changes and last hits come when the fight ends)"), "the count line: " + cnt.innerHTML);
  assert.strictEqual(filt.writes, 1, "the first paint draws the builder");
  const w0 = filt.writes;
  fight.log.push({ k: "cast", t: 3000, n: "B", s: "Zop" });
  st.version++;
  sync({ tick: true });
  assert.ok(cnt.innerHTML.startsWith("3 of 3 events"), "a tick repaints the count: " + cnt.innerHTML);
  assert.ok(list.innerHTML.indexOf("Zop") > 0 && list.innerHTML.indexOf("Zop") < list.innerHTML.indexOf("Zip"), "and the list, newest first");
  assert.strictEqual(filt.writes, w0, "a tick never writes the builder");
  // not a tick (the 2 s report): the builder follows the value lists (a new ability is offered), and an unchanged one is not rewritten
  sync({});
  const w1b = filt.writes;
  assert.ok(filt.innerHTML.includes('<option value="Zop">'), "a report repaint offers the new ability in the value list");
  st.version++;
  sync({});
  assert.strictEqual(filt.writes, w1b, "an unchanged builder is not rewritten");
  const w0b = w1b;
  // the reader has a control in the builder: even a changed builder is left alone, until it is their own edit
  fd.filter = G("and", [C("ability", "has", "zi")]); // changed behind the reader's back (e.g. another card's edit)
  filt.controls = [ctl];
  document.activeElement = ctl;
  sync({});
  assert.strictEqual(filt.writes, w0b, "a focused control keeps its builder on a report repaint");
  sync({ force: true });
  assert.strictEqual(filt.writes, w0b + 1, "their own edit repaints it");
  assert.strictEqual(ctl.focused, 1, "and the keyboard goes back to the control");
  assert.deepStrictEqual(ctl.sel, [2, 3], "with the caret and the selection where they were");
  document.activeElement = null;
  // an edit also repaints the timeline slot through the closure the caller gave
  let asked = null;
  sync({ force: true, tl: (keep) => { asked = keep; return "<i>tl</i>"; } });
  assert.strictEqual(tl.innerHTML, "<i>tl</i>", "the timeline marks follow an edit");
  assert.strictEqual(typeof asked, "function", "with the filter's keep function");
  sync({ tick: true, tl: () => "<i>other</i>" });
  assert.strictEqual(tl.innerHTML, "<i>tl</i>", "a tick leaves the timeline to its own paint");
  // no fight: hidden, nothing else
  const wl = list.writes;
  run("fdEventsSync(card, fd, 'k', null, {})", { card, fd });
  assert.strictEqual(sec.hidden, true);
  assert.strictEqual(list.writes, wl, "nothing drawn without a fight");
  // another fight key starts at the first page; the filter is kept
  fd.evShown = 1500;
  run("fdEventsSync(card, fd, 'live:Other', fight, o)", { card, fd, fight, o: { range: null, st } });
  assert.strictEqual(fd.evShown, 500, "another fight: first page");
  assert.strictEqual(fd.filter.items[0].v, "zi", "the filter is kept across fights");
  fd.evShown = 1500;
  run("fdEventsSync(card, fd, 'live:Other', fight, o)", { card, fd, fight, o: { range: null, st } });
  assert.strictEqual(fd.evShown, 1500, "the same fight keeps the page the reader opened");
  // the range composes with the filter
  fight.log = [{ k: "cast", t: 1000, n: "B", s: "Zap" }, { k: "cast", t: 50000, n: "B", s: "Zip" }];
  st.version++;
  run("fdEventsSync(card, fd, 'live:Other', fight, o)", { card, fd, fight, o: { range: { from: 0, to: 10000 }, st } });
  assert.ok(cnt.innerHTML.startsWith("0 of 2 events"), "the filter (ability has zi) finds nothing in 0-10 s of this log: " + cnt.innerHTML);
  fd.filter = FdFilter.empty();
  run("fdEventsSync(card, fd, 'live:Other', fight, o)", { card, fd, fight, o: { range: { from: 0, to: 10000 }, st } });
  assert.ok(cnt.innerHTML.startsWith("1 of 2 events"), "N is after the range and the filter, M the whole fight: " + cnt.innerHTML);
  // a filed fight's count carries no live note
  run("fdEventsSync(card, fd, 'k', f, o)", { card, fd, f: { dur_ms: 1000, log: [{ k: "cast", t: 1, n: "x", s: "y" }] }, o: { range: null } });
  assert.strictEqual(cnt.innerHTML, "1 of 1 events");
}
// the memo: a running fight is built once per state version, a filed one once per fight object
{
  let builds = 0;
  const real = FightEvents.build;
  ctx.FightEvents = Object.assign({}, FightEvents, { build: (f, o) => { builds++; return real(f, o); } });
  const st = FdLive.create("C"); st.version = 1;
  const f1 = { live: true, log: [], bots: [] }, f2 = { live: true, log: [], bots: [] };
  run("fdEvBuilt(f, st)", { f: f1, st });
  run("fdEvBuilt(f, st)", { f: f2, st }); // a new fight object each second, the same version
  assert.strictEqual(builds, 1, "a running fight is rebuilt only when the state's version changed");
  st.version = 2;
  run("fdEvBuilt(f, st)", { f: f2, st });
  assert.strictEqual(builds, 2);
  const filed = { log: [] };
  run("fdEvBuilt(f, null)", { f: filed });
  run("fdEvBuilt(f, null)", { f: filed });
  assert.strictEqual(builds, 3, "a filed fight once per object");
  ctx.FightEvents = FightEvents;
}
// ---- the repaint after an edit goes through the last paint's inputs
{
  const calls = [];
  vm.runInContext("var fdEventsSync = (card, fd, key, fight, o) => { __calls.push({ key, fight, o }); };", Object.assign(ctx, { __calls: calls }));
  const card = {}, fd = { evCtx: { key: "k", fight: { x: 1 }, opts: { range: null, st: null, tl: null } } };
  ctx.cmdFd.Cmd = fd; ctx.cmdCards.set("Cmd", card);
  run("fdEvRepaint('Cmd')");
  assert.strictEqual(calls.length, 1);
  assert.deepStrictEqual([calls[0].key, calls[0].o.force, calls[0].o.tick], ["k", true, undefined], "a forced repaint, never a tick");
  run("fdEvRepaint('Nobody')");
  assert.strictEqual(calls.length, 1, "no card, nothing to repaint");
}
// ---- wiring pins: app.js as text
{
  const sync = grab("fdEventsSync");
  assert.ok(sync.includes("if (!o.tick) fdEvFilterPaint("), "a tick does not touch .fd-evfilter");
  const tick = grab("fdLivePaintDebug");
  assert.ok(tick.includes("fdEventsSync(card, fd, fdAuLiveKey(lf), lf, { range: fd.liveRange, st, tick: true, tl })"), "the 1 s live repaint paints the Events panel as a tick");
  assert.ok(!tick.includes("fd-evfilter") && !tick.includes("fdEvFilterPaint") && !tick.includes("fdFilterHtml"), "the 1 s repaint never builds or paints the builder");
  assert.ok(tick.includes("fdEvKeep(fd, lf, st)"), "the live timeline's marks follow the filter");
  const render = src.slice(src.indexOf("  function renderFightDebug("), src.indexOf("  // The window an underperf answer covers"));
  assert.ok(render.includes("fdEventsSync(card, fd, evKey, evFight, { range, st: selLive ? liveSt : null, tl: tlHtml });"), "the card render paints the panel with the same range as every panel");
  assert.ok(render.includes("fdEvKeep(fd, evFight, selLive ? liveSt : null)"), "and the filed timeline's marks follow the filter");
  assert.ok(render.includes("fdAuraFight(fight, track && track !== 'loading' ? track : null, bind.commander)"), "the filed fight is the Auras panel's (sampled rows when it has none)");
  assert.ok(src.includes('<section class="fd-events" aria-label="Events" hidden><h3 class="cmd-h">Events <span class="fd-evcount"></span></h3><div class="fd-evfilter"></div><div class="fd-evlist"></div></section>'), "the card template");
  assert.ok(src.indexOf('<section class="fd-events"') > src.indexOf('<section class="fd-auras"'), "after the Auras panel");
  assert.ok(src.includes(".fd-auras, .fd-events, .fd-roles').forEach(el => { el.hidden = !debug; });") && render.includes(".fd-auras, .fd-events, .fd-roles').forEach(el => { el.hidden = noWin; });"), "hidden outside the debug mode and with no fight");
  assert.ok(src.includes("filter: FdFilter.empty(), evShown: FD_EV_PAGE"), "the card's state starts with an empty filter and the first page (it is not reset by a fight change)");
  assert.ok(!/fd\.filter = FdFilter\.empty\(\)/.test(grab("fdSyncSel")) && !/\.filter = /.test(grab("fdLiveHandoff")), "a fight change or the hand-over keeps the filter");
  assert.ok(src.includes("[data-fd-fpreset], [data-fd-fclear], [data-fd-fadd], [data-fd-faddg], [data-fd-fdel], [data-fd-evmore]"), "the buttons' click path");
  assert.ok(src.includes("[data-fd-fnot], [data-fd-fop], [data-fd-ffield], [data-fd-fcop], [data-fd-fval]"), "the selects' and boxes' change path");
  assert.ok(/FD_EV_DEBOUNCE_MS = 300;/.test(src) && /fd\.evTimer = setTimeout\(\(\) => \{ fd\.evTimer = 0; if \(fdFilterEdit\(fd, el\)\) fdEvRepaint\(key\); \}, FD_EV_DEBOUNCE_MS\);/.test(src), "a typed value is applied 300 ms after the last key, once per card");
  assert.ok(src.includes("clearTimeout(fd.evTimer); // a pending typed edit is this one"), "a committed value cancels the pending timer");
  assert.ok(grab("fdEvFilterPaint").includes("if (inside && !force) return;"), "the builder under use is not replaced by a report repaint");
  assert.ok(src.includes("[data-fd-range-clear], [data-fd-range-zoom], [data-fd-follow], [data-fd-range-set]") && src.includes("const bc = e.target.closest && e.target.closest('[data-fd-bosscast]');"), "the rows use the boss-cast marker's seek");
  const html = fs.readFileSync(path.join(jsDir, "..", "..", "index.html"), "utf8");
  const at = (n) => html.indexOf("/static/js/" + n);
  assert.ok(at("fightevents.js") > 0 && at("fdfilter.js") > 0 && at("fightevents.js") < at("app.js") && at("fdfilter.js") < at("app.js") && at("fdview.js") < at("fdfilter.js"), "the modules load before app.js");
  assert.ok(/fightevents\.js\?v=1/.test(html) && /fdfilter\.js\?v=1/.test(html), "with their versions");
  const css = fs.readFileSync(path.join(jsDir, "..", "css", "app.css"), "utf8");
  assert.ok(css.includes(".fd-evgroup {") && css.includes("border-left: 2px solid var(--border-color)") && css.includes(".fd-events[hidden]"), "the css is there");
}
console.log("fdevents_ui_check.js ok");
