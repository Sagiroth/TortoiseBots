"use strict";
// Run by commandersjs_test.go under node: Glide (web/static/js/glide.js), the browser-side interpolation of the reported positions.
const assert = require("assert");
const G = require(require("path").resolve(process.argv[2]));

const near = (a, b, msg) => assert.ok(Math.abs(a - b) < 1e-6, (msg || "") + ": " + a + " != " + b);

// ---- halfway point between two reports
{
  const st = G.create();
  assert.strictEqual(st.lastAt, null);
  assert.strictEqual(st.gap, 2000, "the 2 s report is the default gap");
  G.sampled(st, 0);
  G.note(st, "b7", 0, 0, 209, true, 0);
  G.sampled(st, 1000);
  assert.strictEqual(st.gap, 1000, "the gap follows the reports");
  G.note(st, "b7", 10, 4, 209, true, 1000);
  near(G.at(st, "b7", 1000).x, 0);
  near(G.at(st, "b7", 1500).x, 5, "halfway x");
  near(G.at(st, "b7", 1500).y, 2, "halfway y");
  near(G.at(st, "b7", 3000).x, 10, "the target is held after the gap");
  assert.strictEqual(G.at(st, "nope", 1000), null);
}

// ---- the gap is capped at 2500 ms and floored at 100 ms
{
  const st = G.create();
  G.sampled(st, 0);
  G.sampled(st, 10000);
  assert.strictEqual(st.gap, 2500, "a long silence does not stretch the glide");
  G.sampled(st, 10010);
  assert.strictEqual(st.gap, 100, "a burst does not shrink it below the floor");
}

// ---- a teleport, a map change and a death snap
{
  const st = G.create();
  G.sampled(st, 0);
  G.note(st, "b1", 0, 0, 209, true, 0);
  G.sampled(st, 1000);
  G.note(st, "b1", 200, 0, 209, true, 1000); // jump limit is 12 + 20 yd/s = 32 yd
  near(G.at(st, "b1", 1000).x, 200, "a teleport snaps");
  G.sampled(st, 2000);
  G.note(st, "b1", 202, 0, 530, true, 2000); // another map
  near(G.at(st, "b1", 2000).x, 202, "a map change snaps");
  G.sampled(st, 3000);
  G.note(st, "b1", 210, 0, 530, false, 3000); // died
  near(G.at(st, "b1", 3000).x, 210, "a death snaps");
  assert.strictEqual(G.active(st, 3000), false, "a dead unit does not glide");
}

// ---- a late sample starts from the position on screen, never jumping back
{
  const st = G.create();
  G.sampled(st, 0);
  G.note(st, "b2", 0, 0, 209, true, 0);
  G.sampled(st, 1000);
  G.note(st, "b2", 10, 0, 209, true, 1000);
  assert.strictEqual(G.active(st, 1000), true, "a moving unit is active");
  near(G.at(st, "b2", 1500).x, 5);
  G.sampled(st, 2000);
  G.note(st, "b2", 10.4, 0, 209, true, 2000); // a report a little behind the shown 10
  near(G.at(st, "b2", 2000).x, 10, "starts from the shown position");
  near(G.at(st, "b2", 2000 + st.gap).x, 10.4);
  assert.strictEqual(G.active(st, 2000 + st.gap + 1), false, "still after the gap");
}

// ---- apply: a shallow copy with the glided x/y
{
  const st = G.create();
  G.sampled(st, 0);
  const bind = {
    commander: "Zfmagea", mobs_map: 209,
    bots: [{ guid: 7, name: "Lead", x: 0, y: 0, map: 209, alive: true }, { guid: 8, name: "Other", x: 3, y: 4, map: 209, alive: true }],
    mobs: [{ id: "100", x: 5, y: 6 }],
  };
  G.note(st, G.botKey(7), 0, 0, 209, true, 0);
  G.note(st, G.botKey(8), 3, 4, 209, true, 0);
  G.note(st, G.mobKey("100"), 5, 6, 209, true, 0);
  G.sampled(st, 1000);
  G.note(st, G.botKey(7), 10, 10, 209, true, 1000);
  G.note(st, G.mobKey("100"), 7, 6, 209, true, 1000);
  const out = G.apply(bind, st, 1500);
  assert.notStrictEqual(out, bind);
  assert.notStrictEqual(out.bots, bind.bots, "the bots array is copied");
  near(out.bots[0].x, 5, "bot glided");
  near(out.bots[1].y, 4, "a bot without a target keeps its report");
  near(out.mobs[0].x, 6, "mob glided");
  assert.strictEqual(bind.bots[0].x, 0, "the input binding is not changed");
  assert.strictEqual(bind.mobs[0].x, 5);
  assert.strictEqual(G.apply(null, st, 0), null);
}

console.log("glide_check.js ok");
