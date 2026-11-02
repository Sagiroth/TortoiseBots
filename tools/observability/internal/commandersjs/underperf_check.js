"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's Underperforming bots section. web/static/js/underperf.js
// builds the API window of a card and renders the report; app.js owns the fetch, the <details> and the run picker.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const jsPath = path.resolve(process.argv[2]);
const appPath = path.resolve(process.argv[3]);
const cssPath = path.resolve(process.argv[4]);
const jsSrc = fs.readFileSync(jsPath, "utf8");
const appSrc = fs.readFileSync(appPath, "utf8");
const css = fs.readFileSync(cssPath, "utf8");

const ctx = { console, encodeURIComponent, decodeURIComponent, Date, isFinite, Math, Number, String, JSON };
vm.createContext(ctx);
vm.runInContext(jsSrc, ctx, { filename: "underperf.js" });
assert.ok(ctx.Underperf && typeof ctx.Underperf.html === "function", "underperf.js loads as the global Underperf");
const U = ctx.Underperf;
const run = (expr) => vm.runInContext(expr, ctx);

// ---- 1. the window each card asks for --------------------------------------------------------------------------------
assert.strictEqual(run("Underperf.POLL_MS"), 30000);
assert.strictEqual(
  run("Underperf.query({commander:'T', bound_at: 1793546503}, {day:''}, 1793547000000, null)"),
  "since=1793546503&run=all",
  "a live bind asks since it began");
const floored = Math.floor((1793547000 - 7200) / 60) * 60;
assert.strictEqual(
  run("Underperf.query({commander:'T'}, {}, 1793547000000, '')"),
  "since=" + floored + "&run=all",
  "a live bind without a bound time asks the last two hours from the floored minute");
assert.strictEqual(
  run("Underperf.query({commander:'T'}, {day:'2026-11-01'}, 1793547000000, null)"),
  "day=2026-11-01&run=last",
  "a chosen night asks that day and its newest run");
const today = new Date(1793547000000).toISOString().slice(0, 10);
assert.strictEqual(
  run("Underperf.query({commander:'T', history:true}, {day:''}, 1793547000000, null)"),
  "day=" + today + "&run=last",
  "a history card on Tonight asks today's UTC day");
assert.strictEqual(
  run("Underperf.query({commander:'T'}, {day:'2026-11-01'}, 1793547000000, '1')"),
  "day=2026-11-01&run=1",
  "the run pick becomes the run parameter");
assert.strictEqual(
  run("Underperf.query({commander:'T'}, {day:'2026-11-01'}, 1793547000000, null, '&tz=Europe%2FOslo&rollover=6')"),
  "night=2026-11-01&tz=Europe%2FOslo&rollover=6&run=last",
  "with the picker's zone a chosen night asks as a local night");
ctx.FightHistory = { nightOf: () => "2026-11-01" };
assert.strictEqual(
  run("Underperf.query({commander:'T', history:true}, {day:''}, 1793547000000, null, '&tz=Europe%2FOslo&rollover=6')"),
  "night=2026-11-01&tz=Europe%2FOslo&rollover=6&run=last",
  "a history card on Tonight asks the current local night");
delete ctx.FightHistory;
assert.ok(appSrc.indexOf("Underperf.query(bind, fd, Date.now(), fd.underPick, fdNightParams())") >= 0,
  "app.js hands the picker's night params to the query");
assert.ok(appSrc.indexOf("/api/v1/combat/underperformers?commander=") >= 0, "app.js calls the route");
assert.ok(appSrc.indexOf("encodeURIComponent(bind.commander)") >= 0, "the commander is URL-encoded");

// ---- 2. the report's markup -------------------------------------------------------------------------------------------
const KEY = "Tacmdbilo\u00001793546106\u00005-13";
const REPORT = {
  commander: "Tacmdbilo", since: 1793546503, until: 1793550650, run: "last",
  fights: 39, judged: 37, truncated: false,
  runs: [
    { since: 1793546503, until: 1793550650, fights: 39, plan: "zul_gurub", legs: ["high_priestess_jeklik"], session: 1793546106 },
    { since: 1793552000, until: 1793553000, fights: 3, plan: "zul_gurub", legs: [], session: 1793546106 },
  ],
  limits: {},
  bots: [{
    name: "<img src=x onerror=1>", guid: 861, class: "warrior", spec: "Fury", role: "dps", level: "yellow",
    fights: 39, judged: 37, peer_fights: 37, flagged: 5,
    you: { dps: 67.7, hps: 0, hcps: 0, cps: 0.31, skill10: null, deaths: 4 },
    peers: { dps: 180.3, hps: null, hcps: null, cps: 0.51, skill10: null, deaths: null },
    ratio: { main: 0.38, cps: 0.61 },
    main: "dps",
    reasons: [
      { id: "peer:main", level: "yellow", fights: 37, text: "dps 67.7 against a role median of 180.3 (38%) over 37 fights" },
      { id: "rule:dps_no_damage", level: "red", fights: 1, text: "<img src=x onerror=1> did no damage (1 fight)" },
    ],
    causes: [],
    fight_refs: [{
      key: KEY, day: "2026-11-01", start: 1793547200, step: 40, leg: "high_priestess_jeklik",
      boss_name: "", outcome: "end", rules: ["dps_no_damage"], low: true,
    }],
  }],
  ok: [{ name: "Holyface", guid: 870, class: "priest", role: "healer", judged: 37 }],
};
ctx.st = { res: REPORT };
ctx.opts = { pick: "1", bindBots: ["Holyface", "Absent"], live: true };
const out = run("Underperf.html(st, opts)");
assert.ok(out.indexOf("<img") < 0, "the bot name from the game server is escaped");
assert.ok(out.indexOf("&lt;img src=x onerror=1&gt;") >= 0, "the escaped name is shown");
assert.ok(out.indexOf("\u2014") >= 0, "a null peer number renders as an em dash");
assert.ok(out.indexOf('data-fd-open="' + encodeURIComponent(KEY) + '"') >= 0, "a fight link carries the encoded key");
assert.ok(out.indexOf('data-fd-day="2026-11-01"') >= 0, "a fight link carries its day");
assert.ok(out.indexOf('data-fd-profile="861"') >= 0, "the name opens the profile by guid");
assert.ok(out.indexOf('<option value="1" selected>') >= 0, "the picked run is selected");
assert.ok(out.indexOf("No fights recorded for: Absent") >= 0, "a bound bot with no fight is named");
assert.ok(out.indexOf("Range, line of sight and mana are not in the fight data yet") >= 0, "the not-measured note is present");
assert.ok(out.indexOf("1 flagged") >= 0, "the header counts the flagged bots");

ctx.st = { res: { fights: 12, judged: 12, bots: [], ok: [{ name: "A" }], runs: [] } };
assert.ok(run("Underperf.html(st, {})").indexOf("No bot underperformed in these 12 fights.") >= 0,
  "no listed bot says so with the fight count");
ctx.st = { res: { fights: 0, judged: 0, bots: [], ok: [], runs: [] } };
assert.ok(run("Underperf.html(st, {})").indexOf("No fights recorded in this window.") >= 0,
  "an empty window says so");
ctx.st = { err: "<b>bad</b>" };
const errOut = run("Underperf.html(st, {})");
assert.ok(errOut.indexOf("&lt;b&gt;bad&lt;/b&gt;") >= 0 && errOut.indexOf("<b>") < 0, "an error is escaped");
ctx.st = { pending: true };
assert.ok(run("Underperf.html(st, {})").indexOf("Loading\u2026") >= 0, "a pending first load says Loading");

// ---- 3. the CSS and the card wiring -----------------------------------------------------------------------------------
assert.ok(css.indexOf("grid-row: -1") >= 0, "the section takes the row after the card's grid");
assert.ok(css.indexOf(".cmd-card:not(.fd-on) .fd-under") >= 0, "the section is hidden outside Fight debugger mode");
assert.ok(appSrc.indexOf('<section class="fd-under"') >= 0, "the card template carries the section");
assert.ok(appSrc.indexOf("fdUnderSync(card, bind, fd);") >= 0, "renderFightDebug syncs the section last");
assert.ok(appSrc.indexOf("function fdUnderSync(") >= 0, "app.js owns the fetch");
console.log("underperf checks passed");
