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

// ---- the buffer runs dry: walk on along the last velocity for at most 300 ms, then hold
{
  const st = G.create();
  G.sampled(st, T0, S0);
  G.note(st, "b1", 0, 0, 209, true, T0, S0);
  G.sampled(st, T0 + 500, S0 + 500);
  G.note(st, "b1", 10, 0, 209, true, T0 + 500, S0 + 500);
  near(st.delay, 625, "one gap of 500 ms puts the map 625 ms behind");
  // the newest sample is at S0+500; the render time passes it 300 ms later
  const atNewest = G.at(st, "b1", T0 + 500 + st.delay);
  near(atNewest.x, 10);
  const after = G.at(st, "b1", T0 + 500 + st.delay + 100);
  near(after.x, 12, "100 ms past the newest sample: still walking");
  const held = G.at(st, "b1", T0 + 500 + st.delay + 300);
  const later = G.at(st, "b1", T0 + 500 + st.delay + 5000);
  near(held.x, 16, "300 ms is as far as it extrapolates");
  near(later.x, 16, "then it holds");
  assert.strictEqual(G.active(st, T0 + 500 + st.delay + 301), false, "and the page may stop drawing");
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

// ---- the frame cap is 60 fps now (16 ms)
assert.strictEqual(G.FRAME_MS, 16, "the dots redraw every animation frame");
assert.strictEqual(G.SAMPLES, 4, "four samples a unit");
assert.strictEqual(G.EXTRAP_MS, 300);

console.log("glide_check.js ok");
