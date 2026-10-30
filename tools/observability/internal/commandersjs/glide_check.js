"use strict";
// Run by commandersjs_test.go under node: Glide (web/static/js/glide.js), the browser-side buffered interpolation of the reported
// positions. The map is drawn in the server's past: samples in server time, a clock offset smoothed by its minimum, and a delay
// of 1.25 median report gaps, so the dots walk steadily instead of stopping at each report and jumping on the next.
const assert = require("assert");
const G = require(require("path").resolve(process.argv[2]));

const near = (a, b, msg, eps) => assert.ok(Math.abs(a - b) < (eps || 1e-6), (msg || "") + ": " + a + " != " + b);
const deg = (d) => (d * Math.PI) / 180;
const T0 = 50000;       // the page's clock at the first report
const S0 = 1791990030000; // its server time

// ---- the clock: the smallest arrival-minus-server offset is the card's clock (a later slow report does not move it)
{
  const st = G.create();
  assert.strictEqual(st.offset, 0);
  assert.strictEqual(st.delay, 2500, "with no gap yet the map sits behind as for the idle 2 s feed");
  G.sampled(st, T0, S0);
  assert.strictEqual(st.offset, T0 - S0, "the first report sets the offset");
  G.sampled(st, T0 + 2000 + 150, S0 + 2000); // a network spike
  assert.strictEqual(st.offset, T0 - S0, "a slower report does not move the clock");
  G.sampled(st, T0 + 4000 - 40, S0 + 4000); // a faster one
  assert.strictEqual(st.offset, T0 - S0 - 40, "a faster report does");
  assert.strictEqual(st.gaps.length, 2);
  near(G.delayOf(st), 1.25 * 2000, "the delay follows the median gap");
}

// ---- the delay: 1.25× the median of the last 8 gaps, clamped to 300..2600 ms
{
  const st = G.create();
  for (let i = 0; i <= 8; i++) G.sampled(st, i * 500, i * 500);
  near(st.delay, 625, "a 0.5 s feed: about 0.6 s behind");
  assert.ok(st.delay >= G.DELAY_MIN_MS && st.delay <= G.DELAY_MAX_MS);
  for (let i = 9; i <= 20; i++) G.sampled(st, 4000 + i * 2000, 4000 + i * 2000);
  near(st.delay, 2500, "an idle 2 s feed: about 2.5 s behind");
  const fast = G.create();
  G.sampled(fast, 0, 0);
  G.sampled(fast, 50, 50);
  near(fast.delay, G.DELAY_MIN_MS, "a burst is not drawn closer than 300 ms");
  const slow = G.create();
  G.sampled(slow, 0, 0);
  G.sampled(slow, 9000, 9000);
  near(slow.delay, G.DELAY_MAX_MS, "a lost report is not worth more than 2.6 s");
}

// ---- a steady walk at a 0.5 s feed with +-150 ms arrival jitter: no frame stops, none runs over twice the true speed
{
  const st = G.create();
  const jitter = [0, 150, 60, 120, 90]; // the first report happens to have the best delay, as the min keeps it there
  const speed = 10 / 500;              // 10 yd per report, 0.02 yd/ms
  const reports = jitter.map((j, i) => ({ arr: T0 + i * 500 + j, t: S0 + i * 500, x: (i * 500) / 50 }));
  let next = 0, prev = null, moved = 0, frames = 0;
  for (let now = T0; now <= T0 + 2000; now += 16) {
    // the reports arrive when they arrive; the map asks for a position every animation frame
    while (next < reports.length && reports[next].arr <= now) {
      const r = reports[next++];
      G.sampled(st, r.arr, r.t);
      G.note(st, G.botKey(7), r.x, 0, 209, true, r.arr, r.t);
    }
    if (now < T0 + 700) continue; // let the first two reports set the clock and the delay
    frames += 1;
    const p = G.at(st, G.botKey(7), now);
    assert.ok(p, "the dot is drawn");
    if (prev) {
      const v = (p.x - prev.x) / 16;
      assert.ok(v > 0, "the dot never stops while the reports keep coming: " + v + " at " + (now - T0));
      assert.ok(v <= 2 * speed + 1e-9, "and never runs over twice the true speed: " + v);
      moved += p.x - prev.x;
    }
    if (prev) assert.ok(p.x >= prev.x - 1e-9, "the walk is monotone");
    prev = p;
  }
  assert.ok(frames > 50 && moved > 20, "it really walked: " + frames + " frames, " + moved + " yd");
  // the unit is reported as moving (the page keeps drawing), and the dot sits behind the newest sample
  assert.strictEqual(G.active(st, T0 + 1800), true);
  const behind = G.at(st, G.botKey(7), T0 + 1500);
  assert.ok(behind.x < 30, "drawn in the server's past, behind the t = 1500 sample at x = 30: " + behind.x);
}

// ---- the buffer runs dry: hold at the newest sample, never walk past it (extrapolating then snapping back read as backward jumps)
{
  const st = G.create();
  G.sampled(st, T0, S0);
  G.note(st, "b1", 0, 0, 209, true, T0, S0);
  G.sampled(st, T0 + 500, S0 + 500);
  G.note(st, "b1", 10, 0, 209, true, T0 + 500, S0 + 500);
  near(st.delay, 625, "one gap of 500 ms puts the map 625 ms behind");
  // the newest sample is at S0+500; past it the dot holds where the unit was last seen
  const atNewest = G.at(st, "b1", T0 + 500 + st.delay);
  near(atNewest.x, 10);
  const after = G.at(st, "b1", T0 + 500 + st.delay + 100);
  near(after.x, 10, "100 ms past the newest sample: holding, not guessing ahead");
  const later = G.at(st, "b1", T0 + 500 + st.delay + 5000);
  near(later.x, 10, "still holding");
  assert.strictEqual(G.active(st, T0 + 500 + st.delay + 1), false, "and the page may stop drawing");
}

// ---- a teleport, a map change and a death snap; a late report never walks the dot back
{
  const st = G.create();
  G.sampled(st, T0, S0);
  G.note(st, "b1", 0, 0, 209, true, T0, S0);
  G.sampled(st, T0 + 1000, S0 + 1000);
  G.note(st, "b1", 200, 0, 209, true, T0 + 1000, S0 + 1000); // jump limit 12 + 20 yd/s = 32 yd
  const t1 = T0 + 1000 + st.delay;
  near(G.at(st, "b1", t1).x, 200, "a teleport snaps");
  assert.strictEqual(G.active(st, t1), false, "a snap leaves nothing to walk");
  G.sampled(st, T0 + 2000, S0 + 2000);
  G.note(st, "b1", 202, 0, 530, true, T0 + 2000, S0 + 2000); // another map
  near(G.at(st, "b1", T0 + 2000).x, 202, "a map change snaps");
  G.sampled(st, T0 + 3000, S0 + 3000);
  G.note(st, "b1", 210, 0, 530, false, T0 + 3000, S0 + 3000); // died
  near(G.at(st, "b1", T0 + 3000).x, 210, "a death snaps");
  assert.strictEqual(G.active(st, T0 + 3000), false, "a dead unit does not move");
  // a report the daemon already superseded: the drawn place stays, the unit's state still follows it
  G.note(st, "b1", 0, 0, 530, true, T0 + 2500, S0 + 2500);
  near(G.at(st, "b1", T0 + 3000).x, 210, "a late sample does not walk the dot back");
}

// ---- a boss turning from 350 deg to 10 deg goes through 0, not 180
{
  near(G.angleLerp(deg(350), deg(10), 0.5), 0, "halfway through the wrap", 1e-9);
  near(G.angleLerp(deg(10), deg(350), 0.5), 0, "and the other way round", 1e-9);
  near(G.angleLerp(deg(350), deg(10), 0.25), deg(355), "a quarter of the way", 1e-9);
  near(G.angleLerp(deg(10), deg(20), 0.5), deg(15), "an ordinary turn", 1e-9);
  const st = G.create();
  G.sampled(st, T0, S0);
  G.note(st, "m5", 0, 0, 249, true, T0, S0, deg(350));
  G.sampled(st, T0 + 1000, S0 + 1000);
  G.note(st, "m5", 0, 0, 249, true, T0 + 1000, S0 + 1000, deg(10));
  const face = G.at(st, "m5", T0 + 500 + st.delay).face;
  near(face, 0, "the drawn facing is past 0", 1e-9);
}

// ---- apply: a shallow copy with the interpolated x/y and a boss's facing; the input binding is never changed
{
  const st = G.create();
  G.sampled(st, T0, S0);
  const bind = {
    commander: "Zfmagea", mobs_map: 209,
    bots: [{ guid: 7, name: "Lead", x: 0, y: 0, map: 209, alive: true }, { guid: 8, name: "Other", x: 3, y: 4, map: 209, alive: true }],
    mobs: [{ id: "100", x: 5, y: 6, rank: "b", o: 1 }],
  };
  G.note(st, G.botKey(7), 0, 0, 209, true, T0, S0);
  G.note(st, G.botKey(8), 3, 4, 209, true, T0, S0);
  G.note(st, G.mobKey("100"), 5, 6, 209, true, T0, S0, 1);
  G.sampled(st, T0 + 1000, S0 + 1000);
  G.note(st, G.botKey(7), 10, 10, 209, true, T0 + 1000, S0 + 1000);
  G.note(st, G.mobKey("100"), 7, 6, 209, true, T0 + 1000, S0 + 1000, 2);
  const out = G.apply(bind, st, T0 + 1000 + st.delay);
  assert.notStrictEqual(out, bind);
  assert.notStrictEqual(out.bots, bind.bots, "the bots array is copied");
  assert.notStrictEqual(out.mobs, bind.mobs);
  near(out.bots[0].x, 10, "bot walked to the newest sample it is due");
  near(out.bots[1].y, 4, "a bot without a newer sample keeps its report");
  near(out.mobs[0].x, 7, "mob walked");
  near(out.mobs[0].o, 2, "the boss faces where the newest sample says");
  assert.strictEqual(bind.bots[0].x, 0, "the input binding is not changed");
  assert.strictEqual(bind.mobs[0].x, 5);
  assert.strictEqual(bind.mobs[0].o, 1);
  assert.strictEqual(G.apply(null, st, 0), null);
  // a mob that carries no facing is not given one
  const plain = G.apply({ commander: "C", bots: [], mobs: [{ id: "101", x: 0, y: 0 }] }, st, T0 + 1000);
  assert.ok(!("o" in plain.mobs[0]));
}

// ---- a hole in the feed (a paused page, a lost stretch) is a snap, never a slow walk across it; the idle 2 s feed is not
{
  const hole = G.create();
  G.sampled(hole, T0, S0);
  G.note(hole, "b1", 0, 0, 209, true, T0, S0);
  G.sampled(hole, T0 + 6000, S0 + 6000);
  G.note(hole, "b1", 10, 0, 209, true, T0 + 6000, S0 + 6000);
  near(G.at(hole, "b1", T0 + 6000 + hole.delay).x, 10, "a 6 s hole snaps to the newest sample");
  const idle = G.create();
  idle.units = {};
  G.sampled(idle, T0, S0);
  G.note(idle, "b1", 0, 0, 209, true, T0, S0);
  G.sampled(idle, T0 + 2000, S0 + 2000);
  G.note(idle, "b1", 10, 0, 209, true, T0 + 2000, S0 + 2000);
  const mid = G.at(idle, "b1", T0 + 2000 + idle.delay - 1000);
  assert.ok(mid.x > 1 && mid.x < 10, "the 2 s idle feed still interpolates: " + mid.x);
  assert.strictEqual(G.at(idle, "nope", 1000), null);
  assert.strictEqual(G.median([3, 1, 2]), 2);
  assert.strictEqual(G.median([4, 1, 3, 2]), 2.5);
}

// ---- a backward clock jump (a restarted game server) or a new datagram session starts the card over, and the frame that
// brought the news is a snap; a small reorder is dropped as before
{
  const st = G.create();
  for (let i = 0; i <= 3; i++) {
    G.sampled(st, T0 + i * 500, S0 + i * 500, "s1", true);
    G.note(st, "b1", i * 10, 0, 209, true, T0 + i * 500, S0 + i * 500);
    G.note(st, "b2", i * 10, 5, 209, true, T0 + i * 500, S0 + i * 500);
  }
  near(st.delay, 625, "the 0.5 s feed sets the delay");
  const before = G.at(st, "b1", T0 + 1600);
  // half a second behind the newest frame: a reorder, dropped as today
  G.sampled(st, T0 + 1600, S0 + 1000, "s1", true);
  G.note(st, "b1", 999, 0, 209, true, T0 + 1600, S0 + 1000);
  near(G.at(st, "b1", T0 + 1600).x, before.x, "a reorder does not move the dot");
  near(st.delay, 625, "and does not move the delay");
  assert.strictEqual(st.lastClock, S0 + 1500, "nor the clock");
  // an hour back: the card starts over and the frame is a snap
  const jumpNow = T0 + 2000, jumpTs = S0 - 3600000;
  G.sampled(st, jumpNow, jumpTs, "s1", true);
  G.note(st, "b1", 70, 0, 209, true, jumpNow, jumpTs);
  assert.deepStrictEqual(st.gaps, [], "the gap history of the old clock is gone");
  assert.strictEqual(st.offset, jumpNow - jumpTs, "the offset window starts over at the jump");
  assert.ok(!st.units["b:2"], "the units of the old clock are gone");
  near(G.at(st, "b1", jumpNow + st.delay).x, 70, "the frame after the jump is drawn at once");
  // and the next frame walks again, on the new clock's own gap
  G.sampled(st, jumpNow + 500, jumpTs + 500, "s1", true);
  G.note(st, "b1", 80, 0, 209, true, jumpNow + 500, jumpTs + 500);
  assert.deepStrictEqual(st.gaps, [500], "the new clock measures its own gap");
  assert.strictEqual(G.active(st, jumpNow + 500), true, "the dot walks again while the render is behind the newest sample");
  // a new datagram session on the same clock starts the card over too
  G.note(st, "b3", 1, 1, 209, true, jumpNow + 500, jumpTs + 500);
  G.sampled(st, jumpNow + 1000, jumpTs + 1000, "s2", true);
  G.note(st, "b1", 90, 0, 209, true, jumpNow + 1000, jumpTs + 1000);
  assert.ok(!st.units["b:3"], "the old session's units are gone");
  near(G.at(st, "b1", jumpNow + 1000 + st.delay).x, 90, "the new session's frame is a snap");
}

// ---- the 2 s COMMANDERS report interleaved between 0.5 s position frames never moves the delay
{
  const st = G.create();
  let x = 0;
  for (let i = 0; i <= 12; i++) {
    const now = T0 + i * 500, ts = S0 + i * 500;
    G.sampled(st, now, ts, undefined, true);
    G.note(st, "b1", x, 0, 209, true, now, ts);
    x += 10;
    if (i % 4 === 2) G.sampled(st, now + 250, ts + 250, undefined, false); // a report, newer than the last position frame
  }
  near(st.delay, 625, "only the position frames feed the gap: 1.25 x 500 ms");
  assert.ok(st.gaps.every((g) => g === 500), "every gap is a position frame's own: " + st.gaps.join(","));
  // the report still moves the dots when it is newer
  const rep = G.create();
  G.sampled(rep, T0, S0, undefined, true);
  G.note(rep, "b1", 0, 0, 209, true, T0, S0);
  G.sampled(rep, T0 + 500, S0 + 500, undefined, true);
  G.note(rep, "b1", 10, 0, 209, true, T0 + 500, S0 + 500);
  G.sampled(rep, T0 + 750, S0 + 750, undefined, false);
  G.note(rep, "b1", 15, 0, 209, true, T0 + 750, S0 + 750);
  near(G.at(rep, "b1", T0 + 750 + rep.delay).x, 15, "a newer report still moves the dot");
  near(rep.delay, 625, "but it does not feed the gap");
  // a late, older report is ignored for the clock and the gaps
  const older = G.create();
  G.sampled(older, T0, S0, undefined, true);
  G.note(older, "b1", 0, 0, 209, true, T0, S0);
  G.sampled(older, T0 + 500, S0 + 500, undefined, true);
  G.note(older, "b1", 10, 0, 209, true, T0 + 500, S0 + 500);
  G.sampled(older, T0 + 700, S0 + 300, undefined, false);
  assert.strictEqual(older.lastClock, S0 + 500, "an older frame never moves the clock back");
  near(older.delay, 625, "nor the delay");
}

// ---- units no report has carried for 15 s are pruned: a rotating pack leaves no trace on the card
{
  const st = G.create();
  let next = 0, now = T0, ts = S0;
  G.sampled(st, now, ts, undefined, false);
  G.note(st, "b1", 0, 0, 209, true, now, ts);
  for (let sec = 0; sec <= 3600; sec += 2) {
    now = T0 + sec * 1000;
    ts = S0 + sec * 1000;
    G.sampled(st, now, ts, undefined, false);
    G.note(st, "b1", sec, 0, 209, true, now, ts);
    for (let i = 0; i < 10; i++) {
      G.note(st, "m" + (next % 1000), i, i, 209, true, now, ts);
      next += 1;
    }
  }
  const units = Object.keys(st.units);
  assert.ok(units.length <= 81, "only the units of the last 15 s are kept: " + units.length);
  assert.ok(units.indexOf("b1") >= 0, "a unit the report keeps carrying stays");
  assert.ok(units.indexOf("m" + ((next - 200 + 1000) % 1000)) < 0, "a mob last carried 40 s ago is gone");
  assert.ok(next > 1000, "the ids really rotated");
}

// ---- the frame cap is 60 fps now (16 ms)
assert.strictEqual(G.FRAME_MS, 16, "the focused map redraws every animation frame");
assert.strictEqual(G.GRID_FRAME_MS, 64, "every other visible map redraws at most about 15 times a second");
assert.strictEqual(G.BACK_MS, 30000, "only a frame 30 s older than the newest server time resets the clock; a stalled report is dropped");
assert.strictEqual(G.PRUNE_MS, 15000, "a unit no report carried for 15 s is gone");
assert.strictEqual(G.POS_WINDOW_MS, 3000, "position frames own the delay while they run");
assert.strictEqual(G.SAMPLES, 4, "four samples a unit");
assert.strictEqual(G.EXTRAP_MS, undefined, "no extrapolation past the newest sample: the dot holds");

// ---- walk 2: a 0.5 s feed with arrival jitter and an occasional seconds-old report frame never walks a monotonic unit back
{
  const st = G.create();
  const speed = 1.2 / 500; // yd per ms, the walk-2 lead's pace
  let prev = null, backs = 0, worst = 0, frames = 0;
  // reports every 2 s, one of them 4 s stale (a stalled fetch under load); pos frames carry the true server time
  for (let now = T0; now <= T0 + 40000; now += 16) {
    const el = now - T0;
    if (el > 0 && el % 500 < 16) {
      const t = S0 + el, arr = now + ((el * 137) % 240 - 120);
      if (arr <= now) {
        G.sampled(st, arr, t, undefined, true);
        G.note(st, G.botKey(1), 1420 + el * speed, 0, 209, true, arr, t);
      }
    }
    if (el > 1000 && el % 2000 < 16) {
      const stale = el % 8000 < 16 ? 4000 : 0; // every fourth report is 4 s late with 4 s old positions
      const t = S0 + el - stale, arr = now;
      G.sampled(st, arr, t, undefined, false);
      G.note(st, G.botKey(1), 1420 + (el - stale) * speed, 0, 209, true, arr, t);
    }
    if (el < 3000) continue;
    const p = G.at(st, G.botKey(1), now);
    assert.ok(p, "the dot is drawn");
    if (prev) {
      const dx = p.x - prev.x;
      if (dx < -0.3) { backs += 1; worst = Math.min(worst, dx); }
    }
    prev = p; frames += 1;
  }
  assert.strictEqual(backs, 0, "no backward step over 0.3 yd in " + frames + " frames (worst " + worst.toFixed(2) + " yd)");
  assert.ok(frames > 2000, "it really sampled every frame: " + frames);
}

console.log("glide_check.js ok");
