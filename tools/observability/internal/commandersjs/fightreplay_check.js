"use strict";
// Run by commandersjs_test.go under node: the fight replay's pure logic (web/static/js/fightreplay.js).
// The synthetic track pins the interpolation rules; the walk 2 fixture pins the wire shape of GET /api/v1/commanders/track.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const R = require(path.resolve(process.argv[2]));

const bot = (guid, x, y, hp, alive, map) => [guid, x, y, hp === undefined ? 100 : hp, alive === undefined ? 1 : alive, map === undefined ? 249 : map];

const synthetic = {
  commander: "Glacienne",
  frames: [
    { t: 100, map: 249, step: 20, fight: false, bots: [bot(813, 0, 0, 100), bot(814, 0, 0, 100), bot(901, 1, 1, 100)], mobs: [{ id: "m1", e: 7, x: 5, y: 5, hp: 100, v: 813 }] },
    { t: 102, map: 249, step: 20, fight: true, bots: [bot(813, 10, 0, 50), bot(814, 200, 0, 100), bot(900, 3, 3, 100)], mobs: [{ id: "m1", e: 7, x: 6, y: 5, hp: 80, v: 814 }] },
    { t: 104, map: 249, step: 21, fight: true, bots: [bot(813, 10, 0, 50), bot(814, 200, 0, 100)], mobs: [] },
  ],
  names: { "7": "Warder" },
};

// ---- constants ------------------------------------------------------------------------------------------------------
assert.strictEqual(R.TICK_MS, 250);
assert.deepStrictEqual(R.SPEEDS, [0.5, 1, 2, 4]);
assert.strictEqual(R.HOLD_S, 3);
assert.strictEqual(R.jumpLimitYd(2), 52);
assert.strictEqual(R.jumpLimitYd(0.2), 32);
assert.strictEqual(R.jumpLimitYd(60), 612);

// ---- prep -----------------------------------------------------------------------------------------------------------
const tr = R.prep(synthetic);
assert.strictEqual(tr.frames.length, 3);
assert.deepStrictEqual(tr.frames.map((f) => f.t), [100, 102, 104]);
assert.strictEqual(tr.t0, 100);
assert.strictEqual(tr.t1, 104);
assert.strictEqual(tr.frames[0].bots[813].x, 0);
assert.strictEqual(tr.frames[1].mobs.m1.v, 814);
assert.strictEqual(tr.names[7], "Warder");

const unsorted = R.prep({ frames: [synthetic.frames[2], synthetic.frames[0], synthetic.frames[1]], names: synthetic.names });
assert.deepStrictEqual(unsorted.frames.map((f) => f.t), [100, 102, 104]);
const dirty = R.prep({ frames: [null, { t: "x" }, { t: 5, bots: [null, [1, 2, 3]], mobs: [{ id: "bad" }] }] });
assert.strictEqual(dirty.frames.length, 1);
assert.deepStrictEqual(Object.keys(dirty.frames[0].bots), ["1"]);
assert.deepStrictEqual(dirty.frames[0].mobs, {});

// ---- positions over time --------------------------------------------------------------------------------------------
let at = R.botsAt(tr, 101);
assert.deepStrictEqual(Object.keys(at).map(Number).sort((a, b) => a - b), [813, 814, 901]);
assert.strictEqual(at[813].x, 5);
assert.strictEqual(at[813].hp, 75);
assert.strictEqual(at[814].x, 0); // a jump is held, never drawn through
assert.strictEqual(at[900], undefined);
assert.notStrictEqual(at[901], undefined); // held for HOLD_S after it vanishes
assert.strictEqual(R.botsAt(tr, 103.5)[901], undefined);
assert.strictEqual(R.botsAt(tr, 101.5)[813].x, 7.5);

const first = R.botsAt(tr, 99);
assert.strictEqual(first[813].x, 0);
assert.strictEqual(first[901].x, 1);
const last = R.botsAt(tr, 110);
assert.strictEqual(last[813].x, 10);
assert.strictEqual(last[901], undefined);
assert.strictEqual(last[900], undefined);

let mt = R.mobsAt(tr, 101);
assert.strictEqual(mt.m1.x, 5.5);
assert.strictEqual(mt.m1.v, 813);
assert.strictEqual(mt.m1.n, "Warder");
assert.strictEqual(R.mobsAt(tr, 102.5).m1.v, 814);
assert.strictEqual(R.mobsAt(tr, 105.1).m1, undefined);

// ---- the fight window -----------------------------------------------------------------------------------------------
const win = R.window(tr, { start: 100, dur_ms: 4000 });
assert.strictEqual(win.startS, 100);
assert.strictEqual(win.endS, 104);
assert.strictEqual(win.frames, 3);
assert.strictEqual(win.gapMaxS, 2);
assert.strictEqual(win.ok, true);
assert.strictEqual(R.coverageText(win), "positions every 2 s · 3 reports");

const none = R.window(tr, { start: 300, dur_ms: 4000 });
assert.strictEqual(none.frames, 0);
assert.strictEqual(none.ok, false);
assert.strictEqual(R.coverageText(none), "No positions kept for this fight");
assert.strictEqual(R.coverageText({ ok: true, frames: 5, gapMaxS: 8.4 }), "positions every 2 s · 5 reports · gap 8 s");
assert.strictEqual(R.window(R.prep({}), { start: 1, dur_ms: 1 }).ok, false);

// ---- a commander outside the daemon's tracked set says so, not "nothing kept for this fight" --------------------------
const untracked = R.prep({ commander: "Fifth", tracked: false, frames: [], names: {} });
assert.strictEqual(untracked.untracked, true, "the track answer's tracked flag reaches the page");
assert.strictEqual(R.prep({ commander: "A", tracked: true, frames: [] }).untracked, false);
assert.strictEqual(R.prep({ commander: "A", frames: [] }).untracked, false, "an old daemon without the flag keeps the old text");
const untrackedWin = R.window(untracked, { start: 300, dur_ms: 4000 });
assert.strictEqual(untrackedWin.ok, false);
assert.strictEqual(untrackedWin.untracked, true);
assert.strictEqual(R.coverageText(untrackedWin), "Replay kept for the first 20 commanders only (dashboard limit)");
assert.strictEqual(R.UNTRACKED_TEXT, "Replay kept for the first 20 commanders only (dashboard limit)");
assert.strictEqual(R.NO_TRACK_TEXT, "No positions kept for this fight");

// ---- controls -------------------------------------------------------------------------------------------------------
assert.strictEqual(R.nextSpeed(4), 0.5);
assert.strictEqual(R.nextSpeed(1), 2);
assert.strictEqual(R.nextSpeed(0.5), 1);
assert.strictEqual(R.nextSpeed(1.5), 1);
assert.strictEqual(R.clock(12000, 39111), "12.0 / 39.1 s");
assert.strictEqual(R.clock(0, 60000), "0.0 / 60.0 s");
// The run strip's clock: whole minutes and seconds, "m:ss of m:ss".
assert.strictEqual(R.spanClock(0, 491000), "0:00 of 8:11");
assert.strictEqual(R.spanClock(90000, 180000), "1:30 of 3:00");
assert.strictEqual(R.spanClock(59999, 60000), "1:00 of 1:00", "seconds round, never 0:60");

// ---- walk 2, as the daemon serves it (3.2) --------------------------------------------------------------------------
const walk = R.prep(JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-track.json"), "utf8")));
assert.strictEqual(walk.frames.length, 1);
const wb = R.botsAt(walk, 1793210079);
assert.strictEqual(Object.keys(wb).length, 39);
assert.strictEqual(wb[813].x, -98.8);
assert.strictEqual(wb[813].y, -116.3);
assert.strictEqual(R.mobsAt(walk, 1793210079)["17379391165513648980"].v, 814);

// ---- empty input ----------------------------------------------------------------------------------------------------
const empty = R.prep({});
assert.deepStrictEqual(empty.frames, []);
assert.strictEqual(empty.t0, 0);
assert.strictEqual(empty.t1, 0);
assert.deepStrictEqual(R.botsAt(empty, 5), {});
assert.deepStrictEqual(R.mobsAt(empty, 5), {});
const nul = R.prep(null);
assert.deepStrictEqual(nul.frames, []);
assert.deepStrictEqual(R.botsAt(nul, 5), {});
