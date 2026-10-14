"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's map layer state (defaults, one global choice, availability).
const assert = require("assert");
const L = require(require("path").resolve(process.argv[2]));

// a Storage stand-in
const mem = () => { const d = {}; return { d, getItem: (k) => (k in d ? d[k] : null), setItem: (k, v) => { d[k] = String(v); } }; };

// ---- defaults: what the maps drew before layers existed is on, everything new is off
const def = L.defaults();
assert.deepStrictEqual(Object.keys(def).sort(), L.keys().slice().sort());
["bg", "grid", "zone", "bots", "botnames", "lead", "trail"].forEach((k) => assert.strictEqual(def[k], true, k));
["zonelabels", "scale", "trailtime", "heading", "healring", "commander", "mobhostile", "mobneutral", "mobcritter", "threat", "bottarget", "aggro"]
  .forEach((k) => assert.strictEqual(def[k], false, k));
assert.strictEqual(L.DEFS.filter((d) => d.def).length, 7);
assert.strictEqual(new Set(L.keys()).size, L.keys().length);
L.DEFS.forEach((d) => { assert.ok(d.label && d.group, d.key); assert.ok(!d.needs || d.why, d.key + " needs a reason"); });

// ---- one global choice, kept once
const st = mem();
assert.deepStrictEqual(L.load(st), def);                    // nothing stored: the defaults
let s = L.set(L.load(st), "grid", false);
s = L.set(s, "threat", true);
assert.strictEqual(L.save(st, s), true);
assert.strictEqual(Object.keys(st.d).length, 1);             // one key, not one per map
assert.strictEqual(Object.keys(st.d)[0], L.STORAGE_KEY);
const again = L.load(st);
assert.strictEqual(again.grid, false); assert.strictEqual(again.threat, true); assert.strictEqual(again.bots, true);
assert.deepStrictEqual(L.load(st), again);                   // a second map / card reading it sees the same
assert.strictEqual(L.set(def, "nope", true).nope, undefined); // unknown key ignored

// ---- bad storage never breaks the page
assert.deepStrictEqual(L.load(null), def);
assert.deepStrictEqual(L.load({ getItem() { throw new Error("blocked"); } }), def);
assert.strictEqual(L.save({ setItem() { throw new Error("full"); } }, def), false);
assert.strictEqual(L.save(null, def), false);
const junk = mem(); junk.d[L.STORAGE_KEY] = "{not json";
assert.deepStrictEqual(L.load(junk), def);
junk.d[L.STORAGE_KEY] = JSON.stringify({ grid: "yes", bots: false, zzz: true, trail: null });
const jl = L.load(junk);
assert.strictEqual(jl.grid, true);  // a non-boolean falls back to the default
assert.strictEqual(jl.bots, false); // a boolean is kept
assert.strictEqual(jl.zzz, undefined);
assert.strictEqual(jl.trail, true);
junk.d[L.STORAGE_KEY] = "[1,2]";
assert.deepStrictEqual(L.load(junk), def);
// an old stored set that lacks a layer added later gets the new layer's default
junk.d[L.STORAGE_KEY] = JSON.stringify({ grid: false });
assert.strictEqual(L.load(junk).grid, false); assert.strictEqual(L.load(junk).zone, true); assert.strictEqual(L.load(junk).heading, false);

// ---- availability: a layer without data is greyed out, with the reason
let av = L.availability({});
assert.strictEqual(av.grid.ok, true); assert.strictEqual(av.scale.ok, true);
assert.strictEqual(av.bg.ok, false); assert.ok(/TBDASH_MAP_DIR/.test(av.bg.why));
assert.strictEqual(av.threat.ok, false); assert.ok(av.threat.why.length > 5);
assert.strictEqual(av.healring.ok, false);
av = L.availability({ picture: true, bots: true, lead: true, trail: true, healer: true, zone: true });
assert.strictEqual(av.bg.ok, true); assert.strictEqual(av.healring.ok, true); assert.strictEqual(av.trailtime.ok, true);
assert.strictEqual(av.heading.ok, false); assert.strictEqual(av.mobhostile.ok, false);
// several maps: available when any can draw it
const m = L.mergeAvailability([L.availability({ bots: true }), L.availability({ bots: true, healer: true })]);
assert.strictEqual(m.healring.ok, true); assert.strictEqual(m.bots.ok, true); assert.strictEqual(m.threat.ok, false);
assert.strictEqual(L.mergeAvailability([]).grid.ok, false);
// drawn = chosen and available
const act = L.active(s, L.availability({ bots: true }));
assert.strictEqual(act.bots, true); assert.strictEqual(act.grid, false /* turned off */); assert.strictEqual(act.threat, false /* chosen but no data */);
assert.strictEqual(act.trail, false /* no data */);
assert.deepStrictEqual(L.groups(), ["Map", "Party", "Mobs"]);
console.log("maplayers.js ok");
