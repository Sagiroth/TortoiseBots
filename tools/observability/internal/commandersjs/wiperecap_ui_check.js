"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's "first 3 deaths" box in app.js (web/static/js/app.js). A wipe draws
// `.fd-wipe` with three `data-fd-death` rows (who and role, the time, the killer, the spell with "(likely)", the health before and
// the damage of the last hits), every game name escaped; a fight that is not a wipe draws nothing; a live wipe is headed
// "Wiping: first 3 deaths", shows "-" where only a finished fight knows and says when the last hits come; the live box repaints
// only when the number of deaths changed (or a health still missing may have arrived); the page wires it in.
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

const FightDebug = require(path.join(jsDir, "fightdebug.js"));
const WipeRecap = require(path.join(jsDir, "wiperecap.js"));
const FdLive = require(path.join(jsDir, "fdlive.js"));
let firstCalls = 0;
const WipeSpy = Object.assign({}, WipeRecap, { firstDeaths: (...a) => { firstCalls++; return WipeRecap.firstDeaths(...a); } });
const ctx = { console, FightDebug, WipeRecap: WipeSpy, FdLive, document: { activeElement: null } };
vm.createContext(ctx);
vm.runInContext(["esc", "setHtml", "fdSet", "fdWipeHtml", "fdWipeLiveSync"].map(grab).join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const count = (s, re) => (s.match(re) || []).length;
const EVIL = "<b>x</b>";

const START = 1000000; // the fight's start in ms (fight.start is in seconds)
const guidOf = (i) => i + 1;
const bots = Array.from({ length: 20 }, (_, i) => ({ name: i === 4 ? EVIL : "Bot" + (i + 1), guid: guidOf(i), role: i === 0 ? "tank" : i === 1 ? "healer" : i === 4 ? EVIL : "dps" }));
const hit = (t, a, d, s) => Object.assign({ t_ms: t, a, d }, s ? { s } : {});
// 11 deaths. The first three: the markup-named bot (two hits, a named spell), Bot2 (a hit with no spell and a boss cast just before it:
// a likely spell), Bot3 (no hits in the record at all). Then eight more.
const deaths = [
  { name: EVIL, t_ms: 12000, x: 1, y: 2, killer: "<i>m</i>", hits: [hit(10000, "<i>m</i>", 700, EVIL), hit(11500, "<i>m</i>", 300, EVIL)] },
  { name: "Bot2", t_ms: 13000, x: 1, y: 2, killer: "Boss", hits: [hit(12900, "Boss", 4000)] },
  { name: "Bot3", t_ms: 14000, x: 1, y: 2, killer: "Boss", hits: [] },
].concat(Array.from({ length: 8 }, (_, i) => ({ name: "Bot" + (i + 6), t_ms: 20000 + i * 500, x: 1, y: 2, killer: "Boss", hits: [hit(19000 + i * 500, "Boss", 900, "Cleave")] })));
const log = [{ k: "cast", t: 12500, n: "Boss", s: "Shadow Bolt", tg: String(guidOf(1)), tn: "Bot2" }];
const fight = { id: "1-1", dur_ms: 40000, start: START / 1000, outcome: "end", bots, deaths, log };
const recapsOf = (f) => FightDebug.deathRecaps(f);
const frame = (rel, rows) => ({ t: (START + rel) / 1000, bots: rows });
// the replay's prepared track: the markup-named bot was at 40 % three seconds before, 5 % half a second before its death
const track = { frames: [frame(9000, { 5: { hp: 40, alive: true } }), frame(11500, { 5: { hp: 5, alive: true } })], names: { 5: EVIL } };

{
  const html = run("fdWipeHtml(fight, recaps, track, startMs, null)", { fight, recaps: recapsOf(fight), track, startMs: START });
  assert.ok(/class="fd-wipe"/.test(html), "a wipe draws the box: " + html.slice(0, 300));
  assert.ok(html.includes("Wipe: first 3 deaths") && !html.includes("Wiping"), "the finished header");
  assert.strictEqual(count(html, /data-fd-death="/g), 3, "three rows that select a death");
  assert.deepStrictEqual(Array.from(html.matchAll(/data-fd-death="(\d+)"/g), (m) => m[1]), ["0", "1", "2"], "the recap index of each row");
  assert.ok(!html.includes(EVIL) && !html.includes("<i>") && html.includes("&lt;b&gt;x&lt;/b&gt;") && html.includes("&lt;i&gt;m&lt;/i&gt;"), "a name, role, killer and spell with markup is shown as text");
  for (const m of html.matchAll(/="([^"]*)"/g)) assert.ok(!/[<>]/.test(m[1]), "an attribute value broke out: " + m[1]);
  assert.strictEqual(count(html, /"/g) % 2, 0, "balanced quotes");
  assert.ok(/class="fd-whp[^"]*">40%</.test(html), "health before: the 40 % frame (the 5 % frame is too close to the death): " + html);
  assert.ok(/1,000/.test(html) && /4,000/.test(html), "the damage of the last hits: 700 + 300, and 4,000");
  assert.ok(/Shadow Bolt/.test(html) && /\(likely\)/.test(html), "a spell known only from a boss cast is labelled (likely)");
  assert.ok(/class="fd-wsp[^"]*">-</.test(html), "no hits, no spell: a dash");
  assert.ok(/0:12/.test(html) && /0:13/.test(html) && /0:14/.test(html), "the time as m:ss");
  assert.ok(!html.includes("Last hits come when the fight ends"), "no live note on a filed fight");
  // no track: the health cell is a dash
  const bare = run("fdWipeHtml(fight, recaps, null, startMs, null)", { recaps: recapsOf(fight) });
  assert.ok(!/class="fd-whp[^"]*">\d+%</.test(bare) && count(bare, /class="fd-whp[^"]*"[^>]*>-</g) === 3, "no track: a dash in the health column");
  assert.strictEqual(count(bare, /class="fd-whp n" title="no position sample within 10 s">-</g), 3, "a missing health says why");
  assert.ok(!/title="no position sample[^"]*">\d/.test(html), "a known health has no such title");
  // a picked death is marked
  const picked = run("fdWipeHtml(fight, recaps, track, startMs, 1)", { recaps: recapsOf(fight) });
  const onRows = picked.split("<tr").filter((r) => r.includes("fd-wiperow on"));
  assert.strictEqual(onRows.length, 1, "one row is marked");
  assert.ok(onRows[0].includes('data-fd-death="1"'), "the picked death's row");
  assert.ok(!html.includes("fd-wiperow on"), "none marked without a pick");
}

// ---- not a wipe: nothing is drawn (3 of 20 dead and an ordinary outcome)
{
  const calm = Object.assign({}, fight, { deaths: deaths.slice(0, 3) });
  assert.strictEqual(run("fdWipeHtml(calm, recaps, track, startMs, null)", { calm, recaps: recapsOf(calm) }), "", "a non-wipe fight draws nothing");
  assert.strictEqual(run("fdWipeHtml(null, [], null, 0, null)"), "", "no fight");
  const wipeNoDeath = Object.assign({}, fight, { outcome: "wipe", deaths: [] });
  assert.strictEqual(run("fdWipeHtml(w, [], null, 0, null)", { w: wipeNoDeath }), "", "a wipe with no death on record has nothing to list");
  assert.strictEqual(run("fdWipeHtml(5, 'x', 7, 'y', 'z')"), "", "hostile arguments");
}

// ---- a live wipe: 10 of 20 dead, deaths without hits
const liveDeaths = Array.from({ length: 10 }, (_, i) => ({ name: i === 0 ? EVIL : "Bot" + (i + 6), t_ms: 12000 + i * 300, x: 1, y: 2, killer: i === 0 ? EVIL : "Boss", hits: [] }));
const liveFight = { id: "1", key: "k\u00001\u00002", live: true, start: START / 1000, dur_ms: 15000, outcome: "live", bots, deaths: liveDeaths, log: [] };
const samples = [{ t: START + 8000, bots: { 5: { hp: 70, a: 1 } }, mobs: [] }, { t: START + 10000, bots: { 5: { hp: 30, a: 1 } }, mobs: [] }, { t: START + 12000, bots: { 5: { hp: 5, a: 1 } }, mobs: [] }];
{
  const lt = WipeRecap.framesFromSamples(samples, START);
  const html = run("fdWipeHtml(fight, recaps, track, startMs, null)", { fight: liveFight, recaps: recapsOf(liveFight), track: lt, startMs: START });
  assert.ok(html.includes("Wiping: first 3 deaths") && !html.includes("Wipe: first"), "the live header");
  assert.strictEqual(count(html, /data-fd-death="/g), 3);
  assert.ok(html.includes("Last hits come when the fight ends."), "the note says when the last hits come");
  assert.strictEqual(count(html, /class="fd-wsp[^"]*">-</g), 3, "no hits yet: a dash for the spell in each row");
  assert.strictEqual(count(html, /class="fd-wdmg[^"]*">-</g), 3, "and for the damage of the last hits");
  assert.ok(/class="fd-whp[^"]*">30%</.test(html), "the health before the first death (at 12 s: the 10 s sample, a 12 s sample is too close): " + html);
  assert.ok(!html.includes(EVIL) && html.includes("&lt;b&gt;x&lt;/b&gt;"), "escaped");
  const few = Object.assign({}, liveFight, { deaths: liveDeaths.slice(0, 9) });
  assert.strictEqual(run("fdWipeHtml(fight, recaps, null, 0, null)", { fight: few, recaps: recapsOf(few) }), "", "9 of 20 is not (yet) a wipe");
}

// ---- the live box repaints only when the number of deaths changed
{
  const el = { innerHTML: "", contains: () => false };
  let writes = 0;
  const box = { get innerHTML() { return el.innerHTML; }, set innerHTML(v) { writes++; el.innerHTML = v; }, contains: () => false };
  const card = { querySelector: (s) => (s === ".fd-recap" ? box : null) };
  const fd = { death: null, wipe: null };
  const st = { startMs: START, samples: samples.slice(), unkept: false, cmd: "c" };
  const sync = (lf, s) => run("fdWipeLiveSync(card, fd, st, lf)", { card, fd, st: s || st, lf });
  firstCalls = 0;
  sync(liveFight);
  assert.ok(box.innerHTML.includes("Wiping: first 3 deaths"), "the first paint draws the box");
  assert.strictEqual(writes, 1);
  const calls = firstCalls;
  sync(liveFight);
  sync(Object.assign({}, liveFight, { dur_ms: 16000 })); // the clock moved, no death
  assert.strictEqual(firstCalls, calls, "the same number of deaths: nothing is worked out again");
  assert.strictEqual(writes, 1, "and nothing is written");
  const more = Object.assign({}, liveFight, { deaths: liveDeaths.concat([{ name: "Bot20", t_ms: 16000, x: 1, y: 2, killer: "Boss", hits: [] }]) });
  sync(more);
  assert.strictEqual(firstCalls, calls, "an eleventh death changes nothing the box draws: no repaint");
  // the same count, other content: a death replaced (a catch-up repaired the list) repaints
  const swapped = Object.assign({}, liveFight, { deaths: liveDeaths.map((d, i) => (i === 1 ? Object.assign({}, d, { name: "Bot20" }) : d)) });
  sync(swapped);
  assert.ok(firstCalls > calls && box.innerHTML.includes("Bot20"), "a death replaced with the count unchanged repaints");
  sync(liveFight);
  assert.ok(!box.innerHTML.includes("Bot20"), "and back");
  // a role changed with the roster size unchanged
  const calls2 = firstCalls;
  const roleBots = bots.map((b) => (b.name === "Bot2" ? Object.assign({}, b, { role: "tank" }) : b));
  const reroled = Object.assign({}, liveFight, { bots: roleBots, deaths: [{ name: "Bot2", t_ms: 11000, x: 1, y: 2, killer: "Boss", hits: [] }].concat(liveDeaths.slice(0, 9)) });
  sync(Object.assign({}, reroled, { bots: bots }));
  const calls3 = firstCalls;
  sync(reroled);
  assert.ok(firstCalls > calls3 && /Bot2<\/button> <span class="fd-wrole">\(tank\)/.test(box.innerHTML), "a role changed with the roster size unchanged repaints");
  // an old event dropped as a new one arrives: the same count, a different first three
  const slid = Object.assign({}, liveFight, { deaths: liveDeaths.slice(1).concat([{ name: "Bot19", t_ms: 17000, x: 1, y: 2, killer: "Boss", hits: [] }]) });
  sync(liveFight);
  const calls4 = firstCalls;
  sync(slid);
  assert.ok(firstCalls > calls4 && !box.innerHTML.includes("&lt;b&gt;x&lt;/b&gt;</button>"), "an old death dropped as a new one arrives (same count) repaints");
  // a cast near a death arrives late (a catch-up): the likely spell is drawn
  sync(liveFight);
  const calls5 = firstCalls;
  sync(Object.assign({}, liveFight, { log: [{ k: "cast", t: 11000, n: "Boss", s: "Shadow Bolt", tg: "5", tn: EVIL }] }));
  assert.ok(firstCalls > calls5, "a boss cast just before a shown death repaints");
  // one cast replaced by another at the same count repaints (the likely spell follows it)
  const castA = Object.assign({}, liveFight, { log: [{ k: "cast", t: 11000, n: "Boss", s: "Shadow Bolt", tg: "5", tn: EVIL }] });
  sync(castA);
  assert.ok(box.innerHTML.includes("Shadow Bolt"), "the likely spell is drawn");
  const calls6 = firstCalls;
  sync(Object.assign({}, liveFight, { log: [{ k: "cast", t: 11000, n: "Boss", s: "Frost Bolt", tg: "5", tn: EVIL }] }));
  assert.ok(firstCalls > calls6 && box.innerHTML.includes("Frost Bolt") && !box.innerHTML.includes("Shadow Bolt"), "a cast replaced at the same count repaints with the new spell");
  sync(liveFight);
  void calls2;
  // a death count below the wipe line draws nothing and clears the box
  const fewer = Object.assign({}, liveFight, { deaths: liveDeaths.slice(0, 2) });
  sync(fewer);
  assert.strictEqual(box.innerHTML, "", "not a wipe: the box is empty");
  sync(liveFight);
  assert.ok(box.innerHTML.includes("Wiping"), "back to a wipe");
  // another fight (a new key) repaints, a picked death repaints
  const w1 = writes;
  sync(Object.assign({}, liveFight, { key: "other\u00001\u00009" }));
  fd.death = 1;
  sync(Object.assign({}, liveFight, { key: "other\u00001\u00009" }));
  assert.ok(box.innerHTML.includes("fd-wiperow on"), "a picked death repaints the box");
  assert.ok(writes > w1, "the writes go on");
  // the health of a death that the samples did not have yet is looked up again when a sample arrives, and only then
  const el2 = { innerHTML: "", contains: () => false };
  let writes2 = 0;
  const box2 = { get innerHTML() { return el2.innerHTML; }, set innerHTML(v) { writes2++; el2.innerHTML = v; }, contains: () => false };
  const card2 = { querySelector: (s) => (s === ".fd-recap" ? box2 : null) };
  const fd2 = { death: null, wipe: null };
  const st2 = { startMs: START, samples: [], unkept: false, cmd: "c" };
  const sync2 = () => run("fdWipeLiveSync(card2, fd2, st2, lf)", { card2, fd2, st2, lf: liveFight });
  sync2();
  assert.ok(!/class="fd-whp[^"]*">\d+%</.test(box2.innerHTML), "no samples: a dash");
  sync2();
  assert.strictEqual(writes2, 1, "no new sample: no repaint");
  st2.samples.push({ t: START + 10000, bots: { 5: { hp: 30, a: 1 } }, mobs: [] });
  sync2();
  assert.ok(/class="fd-whp[^"]*">30%</.test(box2.innerHTML), "a sample arrived: the health fills in");
  const w2 = writes2;
  st2.samples.push({ t: START + 14000, bots: { 5: { hp: 0, a: 0 } }, mobs: [] });
  sync2();
  assert.strictEqual(writes2, w2, "every health known: a new sample repaints nothing");
  // the samples at their cap: the list stops growing, its newest sample moves: a missing health is still looked for again
  {
    const el4 = { innerHTML: "", contains: () => false };
    let writes4 = 0;
    const box4 = { get innerHTML() { return el4.innerHTML; }, set innerHTML(v) { writes4++; el4.innerHTML = v; }, contains: () => false };
    const card4 = { querySelector: (s) => (s === ".fd-recap" ? box4 : null) };
    const fd4 = { death: null, wipe: null };
    const cap = FdLive.SAMPLES_MAX;
    const other = (t) => ({ t, bots: { 99: { hp: 50, a: 1 } }, mobs: [] });
    const st4 = { startMs: START, samples: Array.from({ length: cap }, (_, i) => other(START - 100000 + i * 2000)), unkept: false, cmd: "c" };
    const sync4 = () => run("fdWipeLiveSync(card4, fd4, st4, lf)", { card4, fd4, st4, lf: liveFight });
    sync4();
    const w0 = writes4;
    sync4();
    assert.strictEqual(writes4, w0, "nothing changed: no repaint");
    st4.samples.shift();
    st4.samples.push({ t: START + 10000, bots: { 5: { hp: 30, a: 1 } }, mobs: [] });
    assert.strictEqual(st4.samples.length, cap, "the list is at its cap");
    sync4();
    assert.ok(/class="fd-whp n">30%</.test(box4.innerHTML), "a new newest sample at the cap fills the health in: " + box4.innerHTML.slice(0, 200));
  }
  // a fight that was not kept, or no live fight, leaves no box
  const st3 = Object.assign({}, st, { unkept: true });
  sync(liveFight, st3);
  assert.strictEqual(box.innerHTML, "", "a fight too short to keep: no box");
  sync(null);
  assert.strictEqual(box.innerHTML, "", "no live fight: no box");
  assert.doesNotThrow(() => run("fdWipeLiveSync(null, null, null, null)"));
}

// ---- wiring pins
{
  const live = grab("fdLivePaintDebug");
  assert.ok(live.includes("fdWipeLiveSync(card, fd, st, lf);"), "the 1 s live tick offers the box (it repaints only on a new death)");
  const render = src.slice(src.indexOf("  function renderFightDebug("), src.indexOf("  // The window an underperf answer covers"));
  assert.ok(render.includes("fdWipeLiveSync(card, fd, liveSt, liveFight)"), "the card render draws the live box");
  assert.ok(/fdWipeHtml\(fight, recaps, track && track !== 'loading' \? track\.prep : null, WipeRecap\.startMs\(fight\), fd\.death\)/.test(render), "the filed fight's box reads the replay's own track (no second fetch)");
  assert.ok(/fd\.wipe = null;/.test(render), "a filed fight forgets the live box's memo, so the hand-over draws the post-fight box");
  assert.ok(!/fdWipe/.test(grab("fdTrackLoad")), "the track request is the one it was");
  const html = fs.readFileSync(path.join(jsDir, "..", "..", "index.html"), "utf8");
  const at = (n) => html.indexOf("/static/js/" + n);
  assert.ok(at("wiperecap.js") > 0 && at("wiperecap.js") < at("app.js"), "wiperecap.js loads before app.js");
  assert.ok(/wiperecap.js\?v=4/.test(html) && /static\/js\/app\.js\?v=59/.test(html) && /app\.css\?v=33/.test(html), "the changed assets carry new versions");
  const css = fs.readFileSync(path.join(jsDir, "..", "css", "app.css"), "utf8");
  assert.ok(/\.fd-wipe \{/.test(css) && /\.fd-wiperow/.test(css), "the box has its styles");
  assert.ok(!/#[0-9a-fA-F]{3,8}\b/.test(css.slice(css.indexOf(".fd-wipe {"), css.indexOf(".fd-wipe {") + 1500).split("\n").filter((l) => /\.fd-wipe/.test(l)).join("\n")), "its rules use the page's variables, no new colour");
}
console.log("wiperecap_ui_check.js ok");
