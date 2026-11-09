"use strict";
// Run by commandersjs_test.go under node: the Auras panel's markup and wiring in app.js (web/static/js/app.js): one bar per interval,
// the boss-cast markers (a click seeks the replay), every game name escaped, the hints (no rows, capped, sampled live, sampled
// fallback), the post-fight fallback from the track's boss-debuff samples (a copy, never the detail's fight), the pick that survives
// the hand-over, and the pins that the live view repaints the panel and the track request asks for x=1.
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

const FightAuras = require(path.join(jsDir, "fightauras.js"));
const FdLive = require(path.join(jsDir, "fdlive.js"));
const FdView = require(path.join(jsDir, "fdview.js"));
const FightDebug = require(path.join(jsDir, "fightdebug.js"));
const Fights = require(path.join(jsDir, "fights.js"));
const ctx = { console, FightAuras, FdLive, FdView, FightDebug, Fights, document: { activeElement: null } };
vm.createContext(ctx);
const names = ["esc", "setHtml", "fdSet", "fdLiveClock", "fdAuIv", "fdAuraFight", "fdAuState", "fdAuCarry", "fdAuMarkVal", "fdAuMarkParse", "fdAuPct",
  "fdAurasControlsHtml", "fdAurasRestHtml", "fdRangeBandHtml", "fdOutCls", "fdAurasParts", "fdAurasHtml", "fdAurasPaint", "fdAurasSync", "fdAuLiveKey", "fdAuLiveOpts"];
vm.runInContext(["var FD_AU_NAMES_MAX = 8; const fdAuMemo = new WeakMap();"].concat(names.map(grab)).join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

const EVIL = "<b>x</b>";
const POISON = 24321, SIPHON = 24324;
const fight = () => ({
  boss: 14834, dur_ms: 120000,
  bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "healer" }, { name: EVIL, guid: 3, role: "dps" }, { name: "D", guid: 4, role: "dps" }],
  auras: [[10000, 1, POISON, 1], [40000, 1, POISON, -1], [30000, 2, POISON, 1], [5000, 3, POISON, -1], [20000, 4, POISON, 1]],
  log: [{ k: "cast", t: 35000, n: "Hakkar", s: "Blood Siphon", sid: SIPHON }, { k: "cast", t: 95000, n: "Hakkar", s: "Blood Siphon", sid: SIPHON }],
  deaths: [{ name: "D", t_ms: 60000, killer: EVIL }],
});
const count = (s, re) => (s.match(re) || []).length;
const draw = (f, au, opts) => run("fdAurasHtml(f, FightAuras.intervals(f), au, o)", { f, au: au || { key: "k", sid: POISON, marker: { sid: SIPHON, name: "Blood Siphon" } }, o: opts });

// ---- a filed fight: one bar per interval, a marker per boss cast that seeks the replay, names escaped
{
  const html = draw(fight());
  assert.strictEqual(count(html, /class="fd-au-on"/g), 4, "A, B, the bot named with markup and D each have one interval: " + html);
  assert.ok(html.includes('class="fd-au-on" style="left:8.33%;width:25.00%"'), "A's 10000 of 120000 ms starts at 8.33%, 30000 ms is 25%");
  assert.ok(html.includes('data-fd-bosscast="35000"') && html.includes('data-fd-bosscast="95000"'), "a marker per Blood Siphon");
  assert.strictEqual(count(html, /class="fd-au-mark"/g), 2, "one marker line per cast, not one per row");
  assert.strictEqual(count(html, /class="fd-au-death"/g), 1, "D died at 60 s: the death is marked on its row");
  assert.ok(!html.includes(EVIL), "no raw markup from a bot name or a killer");
  assert.ok(html.includes("&lt;b&gt;x&lt;/b&gt;"), "the name is shown as text");
  assert.ok(html.includes("Raid") && html.includes("Poisonous Blood"), "the raid row and the aura name");
  assert.ok(html.includes("At each Blood Siphon") && html.includes("Each cycle") && html.includes("Each application"), "the marker, cycle and application tables");
  assert.ok(html.includes("<td>3 / 4</td>") && html.includes("<td>1 / 3</td>"), "had it, of those alive, at each cast");
  assert.ok(!/fd-au-note/.test(html), "a filed fight with its own rows has no sampled or capped note");
  assert.strictEqual(count(html, /<select/g), 2, "the aura and the marker selects");
  assert.ok(/<option value="24321" selected>/.test(html) && /<option value="sid:24324" selected>/.test(html), "the picks are the selected options");
  assert.strictEqual((html.match(/"/g) || []).length % 2, 0, "balanced quotes");
}
// deaths as the marker: victim (killer), had it yes/no, no cycle table; the aura name from the live view's learned names
{
  const f = fight();
  f.auras.push([1000, 9, 555, 1]); f.bots.push({ name: "E", guid: 9, role: "dps" });
  const html = draw(f, { key: "k", sid: 555, marker: "deaths" }, { names: { 555: EVIL } });
  assert.ok(html.includes("At each death") && html.includes("<td>no</td>") && !html.includes("Each cycle"), "deaths: no cycle table");
  assert.ok(html.includes("D (&lt;b&gt;x&lt;/b&gt;)"), "the killer is escaped");
  assert.ok(!html.includes(EVIL) && html.includes("&lt;b&gt;x&lt;/b&gt;</option>"), "a learned aura name is escaped in the select");
}
// the capped note, the names at a marker cut to 8, the whole-fight focus line
{
  const f = fight();
  f.auras = Array.from({ length: 1000 }, (_, k) => [k, 1, POISON, k % 2 ? -1 : 1]);
  assert.ok(draw(f).includes("may be missing (the module keeps the newest 1000)"), "the capped note");
  const big = { dur_ms: 1000, boss: 1, bots: Array.from({ length: 10 }, (_, k) => ({ name: "n" + k, guid: k + 1, role: "dps" })), auras: Array.from({ length: 10 }, (_, k) => [0, k + 1, 7, 1]),
    log: [{ k: "cast", t: 500, s: "Zap", sid: 3 }] };
  const html = draw(big, { key: "k", sid: 7, marker: { sid: 3 } });
  assert.ok(html.includes("n0, n1, n2, n3, n4, n5, n6, n7 +2"), "first 8 names, then +N: " + html);
  const fo = fight(); fo.debuffs = { sampled_s: 100, up_s: { faerie_fire: 80 } };
  assert.ok(draw(fo).includes("On the boss, whole fight (sampled once a second)") && draw(fo).includes("faerie_fire 80 %"), "the focus debuff line");
  assert.ok(!draw(fight()).includes("On the boss"), "no debuffs, no line");
}
// a fight without rows: the watched-list hint and nothing else drawn (no strip)
{
  const f = fight(); f.auras = [];
  const html = draw(f);
  assert.ok(html.includes("No watched aura changed in this fight. Only the auras in the server&#39;s watch list are recorded.".replace("&#39;", "'")), "the empty hint");
  assert.ok(!html.includes("fd-au-strip"), "no strip");
  const g = fight(); delete g.auras;
  assert.ok(draw(g).includes("No watched aura changed"), "missing rows read the same");
  assert.ok(draw(f, null, { pending: true }).includes("Loading the aura data"), "a track still loading says so rather than the empty hint");
}
// ---- (r1) every fight bot is listed, a bot that never had the aura with 0 %
{
  const f = fight(); f.bots.push({ name: "Zed", guid: 7, role: "dps" });
  const html = draw(f);
  assert.ok(/<span class="fd-au-name r-dps">Zed<\/span><span class="fd-au-pct">0 %<\/span>/.test(html), "a bot with no uptime shows 0 %: " + html);
  assert.strictEqual(count(html, /class="fd-au-name/g), 6, "the raid row and the five bots");
}
// ---- (r2) a capped list: minimums, a dash for a bot with no kept row, "?" for the bots unknown at a cast or death, "≥" on the cycles
{
  const rows = Array.from({ length: 996 }, (_, k) => [20000 + k, 2, 8, k % 2 ? -1 : 1]); // T0 = 20 s
  rows.push([30000, 1, POISON, -1], [50000, 1, POISON, 1], [70000, 1, POISON, -1], [40000, 3, POISON, 1]);
  const f = { boss: 1, dur_ms: 100000, bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "dps" }, { name: "C", guid: 3, role: "dps" }, { name: "D", guid: 4, role: "dps" }],
    auras: rows, deaths: [{ name: "D", t_ms: 15000, killer: "k" }], log: [{ k: "cast", t: 10000, s: "Zap", sid: 3 }, { k: "cast", t: 35000, s: "Zap", sid: 3 }] };
  const html = draw(f, { key: "k", sid: POISON, marker: { sid: 3 } });
  assert.ok(html.includes("may be missing") && html.includes('class="fd-au-unk"'), "the unknown stretch is drawn");
  assert.ok(html.includes('<span class="fd-au-name r-tank">A</span><span class="fd-au-pct">≥ 30 %</span>'), "bot A: a minimum (30 s known up): " + html);
  assert.ok(html.includes('<span class="fd-au-name r-dps">B</span><span class="fd-au-pct">—</span>'), "a bot with no kept row: a dash, not 0 %");
  assert.ok(html.includes('<span class="fd-au-name">Raid</span><span class="fd-au-pct">≥ '), "the raid row is a minimum too");
  const casts = html.slice(html.indexOf("At each Zap"), html.indexOf("Each cycle"));
  assert.ok(casts.includes("<td>0 / 4 (4 ?)</td>"), "the cast at 10 s, before T0: nobody provably had it, four unknown: " + casts);
  assert.ok(casts.includes("<td>0 / 3 (1 ?)</td>"), "the cast at 35 s: D is dead, A and C are known not to have it, B (no kept row) stays unknown");
  const cycle = html.slice(html.indexOf("Each cycle"));
  assert.ok(cycle.includes("≥ ") && !/<td>\d+ %<\/td>/.test(cycle), "every cycle's uptime is a minimum or a dash: " + cycle);
  const dh = draw(f, { key: "k", sid: POISON, marker: "deaths" });
  assert.ok(dh.includes("<td>?</td>"), "D died inside the unknown stretch: '?', not 'no'");
}
// ---- (r1) the hints while the track is out and for a raid past the track's limit
{
  const f = fight(); f.auras = [];
  assert.ok(draw(f, null, { pending: true }).includes("Loading the aura data"));
  assert.ok(draw(f, null, { untracked: true }).includes("(the last 20 commanders)"), "an untracked raid has its own hint");
  assert.ok(!draw(f, null, { untracked: true }).includes("No watched aura changed"));
  const g = fight(); g.aura_src = "sampled"; g.aura_slots_full = true;
  assert.ok(draw(g).includes("carried 4 boss debuffs at once"), "the full-slots note");
  g.aura_slots_full = false;
  assert.ok(!draw(g).includes("carried 4 boss debuffs"), "no note without a full slot");
  assert.ok(!draw(fight()).includes("carried 4 boss debuffs"), "and none for exact rows");
}
// ---- LIVE: a live fight object (aura_src "sampled") draws the live hint and its one interval
{
  const st = FdLive.create("Gbmage");
  const start = 1000000;
  const samp = (t, db) => ({ t: start + t, bots: { 1: { db }, 2: { db: [] } }, mobs: [], w: 10 });
  st.samples = [samp(0, []), samp(2000, [POISON]), samp(4000, [])];
  st.startMs = start;
  const live = { live: true, dur_ms: 6000, boss: 14834, bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "healer" }], log: [], deaths: [],
    auras: FdLive.auraRows(st), aura_src: "sampled" };
  const html = draw(live, { key: "live", sid: POISON, marker: null });
  assert.ok(html.includes("Sampled every 2 s from the bots' boss debuffs (up to 4 per bot). The exact rows replace this when the fight ends."), "the live hint");
  assert.strictEqual(count(html, /class="fd-au-on"/g), 1, "one sampled interval");
  assert.ok(!html.includes("may be missing"), "a sampled list is not 'capped'");
  // the same rows on a finished fight: the fallback wording
  const done = Object.assign({}, live, { live: false });
  assert.ok(draw(done, { key: "k", sid: POISON, marker: null }).includes("This fight has no aura rows; sampled every 2 s from the bots' boss debuffs (up to 4 per bot)."), "the fallback hint");
  // no boss debuff seen yet: a live fight says so (not that the watch list lacks it)
  const none = Object.assign({}, live, { auras: [] });
  assert.ok(draw(none, { key: "live", sid: null, marker: null }).includes("No boss debuff has been seen on a bot yet."));
  // a live view with no commander
  assert.ok(run("fdAurasHtml(f, FightAuras.intervals(f), au, o)", { f: live, au: { key: "live", sid: null, marker: null }, o: { unbound: true } }).includes("needs a bound commander"));
  assert.deepStrictEqual(JSON.parse(JSON.stringify(run("fdAuLiveOpts(st, r)", { st: { cmd: null, auraNames: { 5: "x" } }, r: { from: 1, to: 2 } }))), { names: { 5: "x" }, unbound: true, range: { from: 1, to: 2 } });
}
// ---- the picks: reset on another fight, the boss default, a reader's pick stays, the live pick carries over to the filed key
{
  const f = fight(), iv = FightAuras.intervals(f);
  const fd = { au: { key: null, sid: null, marker: null, userSid: false, userMark: false } };
  let au = run("fdAuState(fd, 'live', f, iv)", { fd, f, iv });
  assert.deepStrictEqual([au.key, au.sid, au.marker.sid], ["live", POISON, SIPHON], "the Hakkar default");
  // (r1 L4) a live pick belongs to its fight: the next live fight (an unkept one leaves no hand-over) starts from the defaults
  assert.strictEqual(run("fdAuLiveKey(lf1)", { lf1: { key: "K1" } }), "live:K1");
  const fdl = { au: { key: null } };
  run("fdAuState(fdl, fdAuLiveKey(a), f, iv)", { fdl, a: { key: "K1" } });
  fdl.au.marker = null; fdl.au.userMark = true;
  assert.strictEqual(run("fdAuState(fdl, fdAuLiveKey(a), f, iv)", { a: { key: "K2" } }).userMark, false, "the next live fight does not inherit the pick");
  assert.strictEqual(run("fdAuState(fdl, fdAuLiveKey(a), f, iv)", { a: { key: "K2" } }).marker.sid, SIPHON);
  run("fdAuCarry(fdl, 'live:K2', 'filed2')");
  assert.strictEqual(fdl.au.key, "filed2", "a hand-over carries the pick of that live fight");
  au.marker = null; au.userMark = true;
  au = run("fdAuState(fd, 'live', f, iv)");
  assert.strictEqual(au.marker, null, "the reader's marker pick stays");
  run("fdAuCarry(fd, 'live', 'filed')");
  assert.strictEqual(fd.au.key, "filed", "the hand-over moves the pick to the filed key");
  au = run("fdAuState(fd, 'filed', f, iv)");
  assert.strictEqual(au.marker, null, "and it is kept there");
  run("fdAuCarry(fd, 'live', 'other')");
  assert.strictEqual(fd.au.key, "filed", "a pick that is not the live one is left alone");
  au = run("fdAuState(fd, 'someone-else', f, iv)");
  assert.strictEqual(au.userMark, false, "another fight starts from its defaults");
  // a live view that had no debuff yet picks one as soon as one is seen
  const empty = { dur_ms: 100, boss: 1, bots: [], auras: [] };
  const fd2 = { au: { key: null } };
  assert.strictEqual(run("fdAuState(fd2, 'live', e, FightAuras.intervals(e))", { fd2, e: empty }).sid, null);
  const later = { dur_ms: 100, boss: 1, bots: [], auras: [[1, 1, 77, 1]] };
  assert.strictEqual(run("fdAuState(fd2, 'live', l, FightAuras.intervals(l))", { l: later }).sid, 77);
  // the marker value round trip
  assert.strictEqual(run("fdAuMarkVal(null)"), "none");
  assert.strictEqual(run("fdAuMarkVal('deaths')"), "deaths");
  assert.strictEqual(run("fdAuMarkVal({ sid: 24324, name: 'x' })"), "sid:24324");
  assert.strictEqual(run("fdAuMarkVal({ name: 'Zap' })"), "name:Zap");
  assert.strictEqual(run("fdAuMarkParse('none')"), null);
  assert.strictEqual(run("fdAuMarkParse('deaths')"), "deaths");
  assert.deepStrictEqual(JSON.parse(JSON.stringify(run("fdAuMarkParse('sid:24324')"))), { sid: 24324 });
  assert.deepStrictEqual(JSON.parse(JSON.stringify(run("fdAuMarkParse('name:Zap')"))), { name: "Zap" });
  assert.strictEqual(run("fdAuMarkParse('sid:abc')"), null);
}
// ---- the post-fight fallback: a filed fight with no rows takes the track's sampled debuffs, on a copy
{
  const f = { start: 1000, dur_ms: 8000, boss: 1, bots: [{ name: "A", guid: 1, role: "tank" }], auras: [] };
  const trackOf = (dbAt) => ({ raw: { frames: [0, 2, 4, 6].map((s) => ({ t: 1000 + s, bots: [[1, 0, 0, 100, 1, 0, 0, 0, dbAt(s)]], mobs: [], w: 10 })) } });
  const track = trackOf((s) => (s === 2 || s === 4 ? [POISON] : []));
  const out = run("fdAuraFight(f, tr, 'Gbmage')", { f, tr: track });
  assert.notStrictEqual(out, f, "a copy");
  assert.deepStrictEqual([out.aura_src, JSON.stringify(out.auras)], ["sampled", JSON.stringify([[2000, 1, POISON, 1], [6000, 1, POISON, -1]])]);
  assert.deepStrictEqual(f.auras, [], "the detail's fight is not changed");
  assert.strictEqual(f.aura_src, undefined);
  assert.strictEqual(run("fdAuraFight(f, tr, 'Gbmage')"), out, "the copy is made once per track (so its intervals are too)");
  assert.strictEqual(run("fdAuIv(o)", { o: out }), run("fdAuIv(o)"), "intervals are memoised per fight object");
  // a fight with its own rows is returned as is; so is one when the track has no debuff, no track, or a loading track
  const own = fight();
  assert.strictEqual(run("fdAuraFight(f, tr, 'c')", { f: own, tr: track }), own);
  assert.strictEqual(run("fdAuraFight(f, tr, 'c')", { f, tr: trackOf(() => []) }), f, "no debuff in any frame");
  assert.strictEqual(run("fdAuraFight(f, null, 'c')"), f);
  assert.strictEqual(run("fdAuraFight(f, 'loading', 'c')"), f);
  // a debuff carried in from before the pull is on from the pull: here held at the first frame inside the fight, gone 2 s later
  const early = trackOf((s) => (s < 4 ? [POISON] : []));
  const mk = (start) => ({ start, dur_ms: 4000, boss: 1, bots: f.bots, auras: [] });
  const ea = run("fdAuraFight(f, tr, 'c')", { f: mk(1002), tr: early });
  assert.strictEqual(ea.aura_src, "sampled");
  assert.deepStrictEqual(JSON.parse(JSON.stringify(ea.auras)), [[0, 1, POISON, 1], [2000, 1, POISON, -1]], "carried in: on [0, 2000], not lost");
  assert.deepStrictEqual(FightAuras.intervals(ea).bySid[POISON][1], [[0, 2000]]);
  // one that was already gone at the pull gives no rows at all
  const gone = run("fdAuraFight(f, tr, 'c')", { f: mk(1004), tr: early });
  assert.strictEqual(gone.aura_src, undefined, "gone at the pull: no rows: " + JSON.stringify(gone.auras));
  // (r1 L1) the track runs past the fight's end: rows after dur_ms are not the fight's
  const late = trackOf((s) => (s === 2 || s === 6 ? [POISON] : []));
  const la = run("fdAuraFight(f, tr, 'c')", { f: { start: 1000, dur_ms: 3000, boss: 1, bots: f.bots, auras: [] }, tr: late });
  assert.deepStrictEqual(JSON.parse(JSON.stringify(la.auras)), [[2000, 1, POISON, 1]], "the 6 s sample's rows lie past the 3 s fight and are dropped");
  // (r1 L7) a bot that carried 4 debuffs at once: the note
  const four = trackOf((s) => (s === 2 ? [1, 2, 3, POISON] : []));
  assert.strictEqual(run("fdAuraFight(f, tr, 'c')", { f, tr: four }).aura_slots_full, true);
  assert.strictEqual(run("fdAuraFight(f, tr, 'c')", { f, tr: trackOf((s) => (s === 2 ? [POISON] : [])) }).aura_slots_full, false);
}
// ---- painting: the selects keep their node while only the strip changes; no fight hides the section
{
  const slots = {};
  const slot = (name) => {
    const n = { writes: 0, _h: undefined, contains: () => false };
    Object.defineProperty(n, "innerHTML", { get() { return n._v || ""; }, set(v) { n._v = v; n.writes++; } });
    slots[name] = n;
    return n;
  };
  const body = { set innerHTML(v) { this._v = v; slot("fd-auslot"); slot("fd-aurest"); }, get innerHTML() { return this._v || ""; },
    querySelector(sel) { return slots[sel.slice(1)] || null; } };
  const sec = { hidden: true };
  const card = { querySelector: (sel) => (sel === ".fd-auras" ? sec : sel === ".fd-aubody" ? body : null) };
  const fdv = { au: { key: null } };
  const f1 = fight();
  run("fdAurasSync(card, fd, 'live', f, o)", { card, fd: fdv, f: f1, o: { names: null } });
  assert.strictEqual(sec.hidden, false);
  assert.ok(slots["fd-aurest"].innerHTML.includes("fd-au-strip") && slots["fd-auslot"].innerHTML.includes("data-fd-ausid"));
  const w0 = slots["fd-auslot"].writes, n0 = slots["fd-auslot"];
  const f2 = fight(); f2.dur_ms = 121000; // a tick: the strip moves, the options do not
  run("fdAurasSync(card, fd, 'live', f, o)", { f: f2 });
  assert.strictEqual(slots["fd-auslot"], n0, "the selects' slot is the same node");
  assert.strictEqual(slots["fd-auslot"].writes, w0, "the selects were not rebuilt (an open list would close)");
  assert.notStrictEqual(slots["fd-aurest"]._h, undefined);
  run("fdAurasSync(card, fd, 'live', f, o)", { f: null });
  assert.strictEqual(sec.hidden, true, "no fight: the section is hidden");
}
// ---- wiring pins: app.js as text
{
  const track = grab("fdTrackLoad");
  assert.ok(track.includes("/api/v1/commanders/track?commander=") && track.includes("'&x=1'") && /Array\.isArray\(fight\.auras\) && fight\.auras\.length \? '' : '&x=1'/.test(track), "fdTrackLoad asks for x=1 only when the fight has no aura rows");
  assert.ok(/\+ '&to=' \+ to \+ sampled/.test(track), "and puts the choice on the url");
  const live = grab("fdLivePaintDebug");
  assert.ok(live.includes("fdAurasSync(card, fd, fdAuLiveKey(lf), lf, fdAuLiveOpts(st, fd.liveRange))"), "the 1 s live tick repaints the Auras panel (only its body)");
  const render = src.slice(src.indexOf("  function renderFightDebug("), src.indexOf("  // The window an underperf answer covers"));
  assert.ok(render.includes("if (selLive) fdAurasSync(card, fd, fdAuLiveKey(liveFight), liveFight, fdAuLiveOpts(liveSt, range));"), "the card render draws the live fight's panel");
  assert.ok(render.includes("const pending = !!fight && !hist && (track === undefined || track === 'loading');") && render.includes("track.raw.tracked === false"), "an unasked track is pending (not for a past night); an untracked raid has its own hint");
  assert.ok(render.includes("fdAuraFight(fight, track && track !== 'loading' ? track : null, bind.commander)"), "and the filed fight's, with the track fallback");
  assert.ok(render.includes(".fd-checks, .fd-meters, .fd-timeline, .fd-auras, .fd-events, .fd-roles').forEach(el => { el.hidden = noWin; });"), "the panel hides with the other fight panels");
  assert.ok(src.includes(".fd-checks, .fd-meters, .fd-timeline, .fd-auras, .fd-events, .fd-roles').forEach(el => { el.hidden = !debug; });"), "and outside the debug mode");
  assert.ok(src.includes('<section class="fd-auras" aria-label="Aura uptime" hidden><h3 class="cmd-h">Auras</h3><div class="fd-aubody"></div></section>'), "the card template");
  assert.ok(src.includes("[data-fd-ausid], [data-fd-aumark]") && src.includes("fd.au.marker = fdAuMarkParse(e.target.value);"), "the selects' change handler");
  assert.ok(grab("fdLiveHandoff").includes("fdAuCarry(fd, 'live:' + fd.liveKey, summary.key);"), "the hand-over carries the pick of that live fight");
  assert.ok(grab("fdSyncSel").includes("fdAuCarry(fd, 'live:' + fd.liveKey, filed.key);"), "and so does the list's own hand-over");
  const html = fs.readFileSync(path.join(jsDir, "..", "..", "index.html"), "utf8");
  assert.ok(html.indexOf("/static/js/fightauras.js") > 0 && html.indexOf("/static/js/fightauras.js") < html.indexOf("/static/js/app.js"), "the module loads before app.js");
  assert.ok(fs.readFileSync(path.join(jsDir, "..", "css", "app.css"), "utf8").includes(".fd-au-on {"), "the css is there");
}
console.log("fightauras_ui_check.js ok");
