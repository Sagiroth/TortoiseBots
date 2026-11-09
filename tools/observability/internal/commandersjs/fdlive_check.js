"use strict";
// Run by commandersjs_test.go under node: FdLive (web/static/js/fdlive.js), the pure state of one live fight view: the event
// cursor (frames, duplicates, gaps, resets, catch-up), the bounded buffers, the sampled aura rows, the follow range, the fight
// clock, the synthetic fight object, the hand-over match and the catch-up rate limit.
const assert = require("assert");
const F = require(require("path").resolve(process.argv[2]));

const KEY = "Gbmage\u00007\u000055-6";
const ev = (i) => ({ k: "cast", t: i * 100, n: "Boss", s: "Spell" + i, id: "1" });
const frame = (from, n, extra) => Object.assign({
  key: KEY, from, cur: from + n, commander: "Gbmage",
  fight: { start_ms: 1000000, boss: 11502, boss_name: "Magmadar", fight_ms: 5000 },
  events: Array.from({ length: n }, (_, p) => ev(from + p)),
}, extra || {});
const NOW = 2000000;

// ---- frames 0-3, 3-5: cur 5 and 5 events; a repeat is a dup; a frame past the cursor is a gap and appends nothing
{
  const st = F.create("Gbmage");
  assert.strictEqual(F.applyEventsFrame(st, frame(0, 3), NOW), "ok");
  assert.strictEqual(F.applyEventsFrame(st, frame(3, 2), NOW), "ok");
  assert.strictEqual(st.cur, 5);
  assert.deepStrictEqual(st.events.map((e) => e.i), [0, 1, 2, 3, 4]);
  assert.strictEqual(st.events[2].t, 200, "a stored event carries its t");
  assert.strictEqual(st.bossName, "Magmadar");
  assert.strictEqual(st.startMs, 1000000);
  const v = st.version;
  assert.strictEqual(F.applyEventsFrame(st, frame(3, 2), NOW), "dup", "a repeat");
  assert.strictEqual(st.version, v, "a dup changes nothing");
  assert.strictEqual(F.applyEventsFrame(st, frame(7, 2), NOW), "gap", "events 5 and 6 were missed");
  assert.deepStrictEqual(st.events.map((e) => e.i), [0, 1, 2, 3, 4, 7, 8], "the frame past the hole is kept by its indexes");
  assert.strictEqual(st.cur, 5, "the contiguous cursor waits at the hole");
  assert.strictEqual(st.gap, false, "a hole only asks for a catch-up: st.gap is what the daemon reports as dropped");
  assert.ok(F.needCatchUp(st, NOW), "a hole asks for a catch-up");
  // the catch-up returns 5 and 6 and closes the hole: 9 events in order, the cursor past the held frame
  F.applyCatchUp(st, { key: KEY, cur: 7, gap: false, events: [{ i: 5, e: ev(5) }, { i: 6, e: ev(6) }], start_ms: 1000000, fight_ms: 6000, boss: 11502, boss_name: "Magmadar" }, NOW);
  assert.deepStrictEqual(st.events.map((e) => e.i), [0, 1, 2, 3, 4, 5, 6, 7, 8], "nine events in order");
  assert.strictEqual(st.cur, 9);
  assert.ok(!st.wantCatchUp && !st.gap);
  assert.strictEqual(F.applyEventsFrame(st, frame(7, 2), NOW), "dup", "the held frame again is a dup");
  assert.strictEqual(F.applyEventsFrame(st, frame(9, 2), NOW), "ok");
  assert.strictEqual(st.cur, 11);
  assert.strictEqual(F.applyEventsFrame(st, { from: 0, cur: 1, events: [ev(0)] }, NOW), "dup", "an old daemon's frame has no key: ignored");
}

// ---- a frame of another fight resets; mid-fight it asks for a catch-up from 0
{
  const st = F.create("Gbmage");
  F.applyEventsFrame(st, frame(0, 3), NOW);
  F.applySample(st, { bots: [{ guid: 1, hp: 5, max_hp: 10 }] }, NOW);
  const other = "Gbmage\u00007\u000055-7";
  assert.strictEqual(F.applyEventsFrame(st, frame(4, 2, { key: other }), NOW), "reset");
  assert.strictEqual(st.key, other);
  assert.deepStrictEqual([st.events.map((e) => e.i), st.samples.length, st.cur], [[4, 5], 0, 0], "the old fight is gone; the new frame's events are kept by index");
  assert.strictEqual(st.gap, false, "a reset is not a daemon-reported gap");
  assert.ok(F.needCatchUp(st, NOW), "the caller catches up from 0");
  assert.strictEqual(F.applyEventsFrame(st, frame(0, 2, { key: "Gbmage\u00007\u000055-8" }), NOW), "ok", "a new fight's first frame resets and appends");
  assert.strictEqual(st.events.length, 2);
  // a catch-up of yet another key resets first
  F.applyCatchUp(st, { key: KEY, cur: 1, events: [{ i: 0, e: ev(0) }] }, NOW);
  assert.strictEqual(st.key, KEY);
  assert.strictEqual(st.events.length, 1);
}

// ---- EVENTS_MAX + 10: the oldest are dropped
{
  const st = F.create("Gbmage");
  const n = F.EVENTS_MAX + 10;
  F.applyEventsFrame(st, frame(0, n), NOW);
  assert.strictEqual(st.events.length, F.EVENTS_MAX);
  assert.strictEqual(st.events[0].i, 10, "the first ten went");
  assert.strictEqual(st.cur, n);
}

// ---- the end event
{
  const st = F.create("Gbmage");
  F.applyEventsFrame(st, frame(0, 1), NOW);
  const f = frame(1, 1);
  f.events = [{ k: "end", t: 9000, o: "kill", dur: 9000, kept: true }];
  F.applyEventsFrame(st, f, NOW);
  assert.ok(st.ended && st.outcome === "kill" && !st.unkept);
  assert.strictEqual(F.nowFightMs(st, NOW + 60000), 9000, "frozen once ended");
  const st2 = F.create("Gbmage");
  const g = frame(0, 1);
  g.events = [{ k: "end", t: 100, o: "end", dur: 100, kept: false }];
  F.applyEventsFrame(st2, g, NOW);
  assert.ok(st2.ended && st2.unkept, "a fight too short to keep says so");
}

// ---- samples, aura rows
const T0 = 1000000;
const bind = (t, db, extra) => Object.assign({
  commander: "Gbmage", ts_ms: t,
  bots: [{ guid: 1, name: "Tank", role: "tank", hp: 50, max_hp: 100, alive: true, db: db.map((id) => ({ id, n: "Aura" + id })) }],
  mobs: [{ id: "99", entry: 11502, hp: 80, victim: "1" }],
  combat: { win_s: 10, bots: [{ g: 1, dps: 120, hps: 40 }] },
}, extra || {});
{
  const st = F.create("Gbmage");
  assert.ok(F.applySample(st, bind(T0 + 2000, [24321]), T0 + 2000));
  assert.ok(F.applySample(st, bind(T0 + 4000, [24321]), T0 + 4000));
  assert.ok(F.applySample(st, bind(T0 + 6000, []), T0 + 6000));
  assert.ok(!F.applySample(st, bind(T0 + 6000, []), T0 + 6000), "a sample not newer than the last is skipped");
  assert.deepStrictEqual(F.auraRowsFrom(st.samples, T0), [[2000, 1, 24321, 1], [6000, 1, 24321, -1]]);
  st.startMs = T0;
  assert.deepStrictEqual(F.auraRows(st), [[2000, 1, 24321, 1], [6000, 1, 24321, -1]]);
  assert.strictEqual(st.auraNames[24321], "Aura24321", "the debuff names are kept (the track has ids only)");
  const s = st.samples[0];
  assert.deepStrictEqual(s.bots[1], { hp: 50, a: 1, dps: 120, hps: 40, db: [24321] });
  assert.deepStrictEqual(s.mobs, [{ id: "99", e: 11502, hp: 80, v: "1" }]);
  assert.strictEqual(s.w, 10);
  // an aura applied before the start and still held at the first sample inside the fight is on from the pull (moved to 0), then removed
  const rows = F.auraRowsFrom(st.samples, T0 + 3000);
  assert.deepStrictEqual(rows, [[0, 1, 24321, 1], [3000, 1, 24321, -1]], "carried in: on from 0 until its removal");
  // (r1 M1) carried in and removed at the first sample inside the fight (rel 4000): on [0, 4000]; applied and removed before the start: nothing
  const smp = (t, db) => ({ t: T0 + t, bots: { 7: { db } }, mobs: [], w: 10 });
  assert.deepStrictEqual(F.auraRowsFrom([smp(0, [5]), smp(5000, [])], T0 + 1000), [[0, 7, 5, 1], [4000, 7, 5, -1]], "carried in, removed at rel 4000: [0, 4000]");
  assert.deepStrictEqual(F.auraRowsFrom([smp(0, [5]), smp(500, [])], T0 + 1000), [], "applied and removed before the start: nothing");
  assert.deepStrictEqual(F.auraRowsFrom([smp(0, [5]), smp(2000, [5]), smp(4000, [])], T0 + 1000), [[0, 7, 5, 1], [3000, 7, 5, -1]], "held at the first sample inside the fight, removed later");
  assert.strictEqual(F.slotsFull([smp(0, [1, 2, 3])]), false);
  assert.strictEqual(F.slotsFull([smp(0, [1, 2, 3]), smp(2000, [1, 2, 3, 4])]), true, "a sample with 4 ids: the report's slots were full");
  // a bot missing from a sample keeps its state
  const keep = [{ t: T0 + 1000, bots: { 1: { db: [5] } } }, { t: T0 + 2000, bots: {} }, { t: T0 + 3000, bots: { 1: { db: [] } } }];
  assert.deepStrictEqual(F.auraRowsFrom(keep, T0), [[1000, 1, 5, 1], [3000, 1, 5, -1]]);
  // the cap
  const big = F.create("Gbmage");
  for (let i = 1; i <= F.SAMPLES_MAX + 5; i++) F.applySample(big, bind(T0 + i * 2000, []), T0 + i * 2000);
  assert.strictEqual(big.samples.length, F.SAMPLES_MAX);
  assert.strictEqual(big.samples[0].t, T0 + 6 * 2000);
  assert.strictEqual(F.bossHp(Object.assign(big, { boss: 11502 })), 80);
  assert.strictEqual(F.bossHp(Object.assign(big, { boss: 1 })), null);
}

// ---- the x=1 track: merged by time, no duplicate second, sorted, bots read from the nine-element rows
{
  const st = F.create("Gbmage");
  F.applySample(st, bind(T0 + 6500, []), T0 + 6500);
  const json = { frames: [
    { t: 1006, w: 10, bots: [[1, 1, 2, 40, 1, 249, 99, 11, []]], mobs: [] },
    { t: 1002, w: 10, bots: [[1, 1, 2, 60, 1, 249, 77, 5, [24321]]], mobs: [{ id: "99", e: 11502, hp: 90, v: 1 }] },
    { t: 1004, bots: [[1, 1, 2, 70, 0, 249]], mobs: [] },
  ] };
  assert.strictEqual(F.applyTrackX(st, json), 2, "the second the stream already has is not added twice");
  assert.deepStrictEqual(st.samples.map((s) => s.t), [1002000, 1004000, T0 + 6500]);
  assert.deepStrictEqual(st.samples[0].bots[1], { hp: 60, a: 1, dps: 77, hps: 5, db: [24321] });
  assert.deepStrictEqual(st.samples[0].mobs, [{ id: "99", e: 11502, hp: 90, v: "1" }]);
  assert.deepStrictEqual(st.samples[1].bots[1], { hp: 70, a: 0, dps: 0, hps: 0, db: [] });
}

// ---- positions
{
  const st = F.create("Gbmage");
  assert.ok(F.applyPos(st, { ts_ms: 1, bots: [{ g: 1, x: 2, y: 3, hp: 50, mhp: 100, a: 1 }], mobs: [{ id: "9", x: 4, y: 5, hp: 60 }] }));
  assert.deepStrictEqual(st.pos[0], { t: 1, bots: [[1, 2, 3, 50, 1]], mobs: [["9", 4, 5, 60]] });
  assert.ok(F.applyPos(st, { ts_ms: 1.5, bots: [{ g: 2, x: 1, y: 1, a: 1 }, { g: 3, x: 1, y: 1, hp: 5, a: 1 }], mobs: [{ id: "8", x: 1, y: 1 }] }));
  assert.deepStrictEqual(st.pos[1], { t: 1.5, bots: [[2, 1, 1, null, 1], [3, 1, 1, null, 1]], mobs: [["8", 1, 1, null]] }, "a health the frame did not carry is null, not 0");
  st.pos.pop();
  assert.ok(!F.applyPos(st, { ts_ms: 1, bots: [], mobs: [] }), "not newer");
  for (let i = 2; i <= F.POS_MAX + 10; i++) F.applyPos(st, { ts_ms: i, bots: [], mobs: [] });
  assert.strictEqual(st.pos.length, F.POS_MAX);
}

// ---- the clock and the range
{
  const st = F.create("Gbmage");
  assert.strictEqual(F.nowFightMs(st, NOW), 0, "no header, no clock");
  F.applyEventsFrame(st, frame(0, 1, { fight: { start_ms: T0, fight_ms: 90000, boss: 1, boss_name: "B" } }), NOW);
  assert.strictEqual(F.nowFightMs(st, NOW), 90000);
  assert.strictEqual(F.nowFightMs(st, NOW + 3000), 93000);
  assert.strictEqual(F.nowFightMs(st, NOW + 600000), 100000, "the browser's clock runs on for CLOCK_SLACK_MS only");
  assert.deepStrictEqual(F.range(st, NOW, 60), { from: 30000, to: 90000 });
  assert.deepStrictEqual(F.range(st, NOW, 120), { from: 0, to: 90000 });
  assert.strictEqual(F.range(st, NOW, 0), null, "whole fight");
  assert.deepStrictEqual(F.WINDOWS, [30, 60, 120, 300, 0]);
  assert.strictEqual(F.DEFAULT_WINDOW, 60);
}

// ---- the synthetic fight object
{
  const st = F.create("Gbmage");
  const f = frame(0, 0);
  f.events = [
    { k: "start", t: 0 }, { k: "engage", t: 10, e: 1, n: "Trash" }, ev(1),
    { k: "death", t: 5000, n: "Tanky", by: "Magmadar", x: 1.5, y: 2.5 },
    { k: "interrupt", t: 5100, n: "Kicker", s: "Pummel" },
  ];
  f.cur = 5;
  F.applyEventsFrame(st, f, NOW);
  F.applySample(st, bind(T0 + 2000, [24321]), T0 + 2000);
  const b = { bots: [{ guid: 1, name: "Tank", role: "tank", hp: 1 }] };
  const fo = F.fight(st, b, NOW);
  assert.strictEqual(fo.live, true);
  assert.strictEqual(fo.key, KEY);
  assert.strictEqual(fo.id, "55-6");
  assert.strictEqual(fo.outcome, "live");
  assert.strictEqual(fo.aura_src, "sampled");
  assert.strictEqual(fo.aura_slots_full, false, "no sample held 4 debuff ids");
  assert.deepStrictEqual(fo.log.map((e) => e.k), ["cast", "death", "interrupt"], "log kinds only");
  assert.deepStrictEqual(fo.deaths, [{ name: "Tanky", t_ms: 5000, x: 1.5, y: 2.5, killer: "Magmadar", hits: [] }]);
  assert.deepStrictEqual(fo.bots, [{ name: "Tank", guid: 1, role: "tank" }]);
  assert.strictEqual(fo.start, 1000);
  assert.strictEqual(fo.boss_name, "Magmadar");
  assert.strictEqual(fo.dur_ms, 5000);
  assert.strictEqual(fo.log_dropped, 0);
  assert.strictEqual(F.fight(st, b, NOW + 100), fo, "memoised within the second");
  assert.notStrictEqual(F.fight(st, b, NOW + 1500), fo, "and rebuilt on the next");
  st.gap = true; st.version++;
  assert.strictEqual(F.fight(st, b, NOW).log_dropped, 1);
}

// ---- hand-over
{
  const st = F.create("Gbmage");
  assert.strictEqual(F.handoff(st, { key: KEY }), false, "no fight yet");
  st.key = KEY;
  assert.ok(F.handoff(st, { key: KEY }), "by key");
  assert.ok(F.handoff(st, { key: "raid#4\u00007\u000055-6" }), "by id and session when the key's commander differs");
  assert.ok(!F.handoff(st, { key: "Gbmage\u00007\u000055-7" }), "another fight");
  assert.ok(!F.handoff(st, { key: "Gbmage\u00008\u000055-6" }), "another session");
  assert.ok(!F.handoff(st, null));
}

// ---- the catch-up rate limit
{
  const st = F.create("Gbmage");
  assert.ok(F.needCatchUp(st, 100000), "a fresh view with nothing asks once");
  F.markCatchUp(st, 100000);
  assert.ok(!F.needCatchUp(st, 100000 + F.CATCHUP_MIN_MS - 1), "not twice within 5 s");
  assert.ok(F.needCatchUp(st, 100000 + F.CATCHUP_MIN_MS));
  F.applyCatchUp(st, { key: KEY, cur: 2, events: [{ i: 0, e: ev(0) }, { i: 1, e: ev(1) }] }, 200000);
  assert.ok(!F.needCatchUp(st, 300000), "events held and no gap: nothing to ask");
  st.wantCatchUp = true;
  assert.ok(!F.needCatchUp(st, 200000 + 100), "a gap waits for the limit");
  assert.ok(F.needCatchUp(st, 200000 + F.CATCHUP_MIN_MS));
}

// ---- (review H1) a catch-up answered after newer frames never moves the cursor back; events join by index, none twice
{
  const st = F.create("C");
  F.applyEventsFrame(st, frame(0, 5), NOW);
  F.applyEventsFrame(st, frame(5, 2), NOW);
  F.applyEventsFrame(st, frame(7, 2), NOW); // cur 9
  F.applyCatchUp(st, { key: KEY, cur: 7, events: [{ i: 5, e: ev(5) }, { i: 6, e: ev(6) }] }, NOW);
  assert.strictEqual(st.cur, 9, "the stale answer left the cursor where the stream had it");
  assert.strictEqual(F.applyEventsFrame(st, frame(9, 2), NOW), "ok", "the next frame continues without a gap");
  assert.strictEqual(st.gap, false);
  F.applyCatchUp(st, { key: KEY, cur: 11, events: [7, 8, 9, 10].map((i) => ({ i, e: ev(i) })) }, NOW);
  assert.deepStrictEqual(st.events.map((e) => e.i), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10], "no index twice, in order");
  assert.strictEqual(st.cur, 11);
}

// ---- (review M1) a catch-up of another fight: the daemon answers from 0 for a key it does not hold (route `key=`), and the view resets onto it
{
  const st = F.create("C");
  const K2 = "Gbmage\u00007\u000055-9";
  F.applyEventsFrame(st, frame(0, 4), NOW); // fight A, cursor 4: the request asked since=4 for key A
  const tok = F.token(st);
  F.applyCatchUp(st, { key: K2, cur: 6, events: [0, 1, 2, 3, 4, 5].map((i) => ({ i, e: ev(i) })) }, NOW);
  assert.strictEqual(st.key, K2);
  assert.deepStrictEqual(st.events.map((e) => e.i), [0, 1, 2, 3, 4, 5], "the new fight's events from 0");
  assert.strictEqual(st.cur, 6);
  assert.strictEqual(st.gap, false);
  assert.ok(F.accepts(st, tok, { key: K2 }) === false, "the old request's token no longer matches the view");
}

// ---- (review M2) frames that arrive while a catch-up is in flight are kept, not thrown away
{
  const st = F.create("C");
  F.applyEventsFrame(st, frame(0, 3), NOW);
  F.applyEventsFrame(st, frame(6, 2), NOW); // hole: 3..5 missing; the catch-up is asked (since=3)
  F.markCatchUp(st, NOW);
  assert.strictEqual(F.applyEventsFrame(st, frame(8, 2), NOW), "gap", "a second frame while it is out");
  assert.deepStrictEqual(st.events.map((e) => e.i), [0, 1, 2, 6, 7, 8, 9]);
  F.applyCatchUp(st, { key: KEY, cur: 6, events: [3, 4, 5].map((i) => ({ i, e: ev(i) })) }, NOW + 100);
  assert.deepStrictEqual(st.events.map((e) => e.i), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9], "the answer filled the hole and the held frames joined it");
  assert.strictEqual(st.cur, 10);
  assert.ok(!st.wantCatchUp, "nothing more to ask");
  assert.strictEqual(st.gap, false);
  // the daemon's own gap flag is the only thing that sets st.gap (and log_dropped)
  F.applyCatchUp(st, { key: KEY, cur: 10, gap: true, events: [] }, NOW);
  assert.strictEqual(st.gap, true);
  assert.strictEqual(F.fight(st, null, NOW).log_dropped, 1);
}

// ---- (review Codex H1, L4) a late answer for fight A after fight B started is dropped by its token
{
  const st = F.create("C");
  F.applyEventsFrame(st, frame(0, 2), NOW);
  const tok = F.token(st);
  const B = "Gbmage\u00007\u000055-7";
  F.applyEventsFrame(st, frame(0, 3, { key: B }), NOW); // the stream moved on to fight B
  const lateA = { key: KEY, cur: 2, events: [{ i: 0, e: ev(0) }, { i: 1, e: ev(1) }] };
  assert.strictEqual(F.accepts(st, tok, lateA), false, "the answer for A is dropped");
  assert.strictEqual(st.key, B);
  assert.strictEqual(st.cur, 3);
  // a view that had no fight when it asked takes the answer of the fight it holds now
  const fresh = F.create("C");
  const t0 = F.token(fresh);
  F.applyEventsFrame(fresh, frame(0, 2), NOW);
  assert.strictEqual(F.accepts(fresh, t0, { key: KEY }), true);
  assert.strictEqual(F.accepts(fresh, t0, { key: B }), false);
  assert.strictEqual(F.accepts(fresh, F.token(fresh), { key: B }), true, "a request asked under the current fight is applied (it resets onto an answer of another one)");
}

// ---- (review M3, L1) elements that are not objects keep their index; a held cursor stays contiguous
{
  const st = F.create("C");
  F.applyEventsFrame(st, { key: KEY, from: 0, cur: 3, events: [ev(0), 7, ev(2)] }, NOW);
  assert.strictEqual(st.cur, 3);
  assert.deepStrictEqual(st.events.map((e) => e.i), [0, 1, 2]);
}

// ---- (review r2 N5) a placeholder for an unreadable element is not a log entry
{
  const st = F.create("C");
  F.applyEventsFrame(st, { key: KEY, from: 0, cur: 3, fight: { start_ms: 1, fight_ms: 5 }, events: [ev(0), 7, ev(2)] }, NOW);
  assert.deepStrictEqual(F.fight(st, null, NOW).log.map((e) => e.k), ["cast", "cast"], "the placeholder is not in the log");
}

// ---- (review r2 N4/N7) a catch-up answer dropped at a fight boundary is asked again after CATCHUP_MIN_MS, not at once and not never
{
  const st = F.create("C");
  const T0 = 1000;
  const tok = F.token(st);           // asked before the view held any fight
  F.markCatchUp(st, T0);
  F.applyEventsFrame(st, frame(3, 1), NOW);                                              // a first frame mid-fight: a hole, a catch-up is wanted
  F.applyEventsFrame(st, frame(2, 1, { key: "Gbmage\u00007\u000055-9" }), NOW);          // and the fight changes under the request
  assert.strictEqual(F.accepts(st, tok, { key: KEY }), false, "the answer of the first fight is dropped");
  assert.ok(st.wantCatchUp, "the hole of the new fight still wants its catch-up");
  assert.ok(!F.needCatchUp(st, T0 + F.CATCHUP_MIN_MS - 1), "not before the rate limit");
  assert.ok(F.needCatchUp(st, T0 + F.CATCHUP_MIN_MS), "and asked again once it allows");
  // an old fight's answer that nobody needs again: the new fight started from 0, so nothing is wanted
  const b = F.create("C");
  F.applyEventsFrame(b, frame(0, 4), NOW);
  const t2 = F.token(b);
  F.markCatchUp(b, T0);
  F.applyEventsFrame(b, frame(0, 2, { key: "Gbmage\u00007\u000055-9" }), NOW);
  assert.strictEqual(F.accepts(b, t2, { key: KEY }), false);
  assert.ok(!F.needCatchUp(b, T0 + 10 * F.CATCHUP_MIN_MS), "a fight that began at 0 needs no catch-up");
}

console.log("fdlive_check.js ok");
