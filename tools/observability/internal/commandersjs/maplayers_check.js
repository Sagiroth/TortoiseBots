"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's map layer state (defaults, one global choice, availability).
const assert = require("assert");
const L = require(require("path").resolve(process.argv[2]));

// a Storage stand-in
const mem = () => { const d = {}; return { d, getItem: (k) => (k in d ? d[k] : null), setItem: (k, v) => { d[k] = String(v); } }; };

// ---- defaults: what the maps drew before layers existed is on, everything new is off
const def = L.defaults();
assert.deepStrictEqual(Object.keys(def).sort(), L.keys().concat(["dim", "trailLen", "style"]).sort());
assert.strictEqual(def.dim, 30); // the picture was darkened by 30 % before the slider existed
// ... plus, new with the unit info: the boss marker and the health bars
["bg", "grid", "zone", "bots", "botnames", "lead", "trail", "bossmark", "hpbars", "runsteps"].forEach((k) => assert.strictEqual(def[k], true, k));
["zonelabels", "scale", "trailtime", "heading", "healring", "commander", "mobhostile", "mobneutral", "mobcritter", "threat", "bottarget", "aggro",
  "hostilenames", "threatcolors", "statusicons", "events", "feed", "runinfo", "membertrails", "healline", "ccline", "stalls", "objects"].forEach((k) => assert.strictEqual(def[k], false, k));
assert.strictEqual(L.DEFS.filter((d) => d.def).length, 10);
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
assert.deepStrictEqual(L.groups(), ["Map", "Party", "Mobs", "Combat", "Units", "Run", "Objects"]);

// ---- the dim slider: 0-80 %, saved with the other choices
assert.strictEqual(L.DIM_MAX, 80);
assert.strictEqual(L.setDim(def, 0).dim, 0);
assert.strictEqual(L.setDim(def, 55).dim, 55);
assert.strictEqual(L.setDim(def, 100).dim, 80, "capped at 80");
assert.strictEqual(L.setDim(def, -5).dim, 0, "not below 0");
assert.strictEqual(L.setDim(def, "40").dim, 40, "a slider value arrives as a string");
assert.strictEqual(L.setDim(def, "abc").dim, 30, "junk falls back");
assert.strictEqual(L.setDim(def, NaN).dim, 30);
assert.strictEqual(L.setDim(def, 12.6).dim, 13);
assert.strictEqual(L.set(def, "dim", true).dim, 30, "dim is not a toggle");
const dst = mem();
L.save(dst, L.setDim(L.set(def, "grid", false), 65));
assert.strictEqual(Object.keys(dst.d).length, 1);
assert.strictEqual(L.load(dst).dim, 65); assert.strictEqual(L.load(dst).grid, false);
assert.strictEqual(L.normalize({ dim: "x" }).dim, 30);
assert.strictEqual(L.normalize({ dim: 999 }).dim, 80);

// ---- storage v2 with a migration from v1
assert.strictEqual(L.STORAGE_KEY, "tbdash.mapLayers.v2");
assert.strictEqual(L.OLD_STORAGE_KEY, "tbdash.mapLayers.v1");
const mig = mem();
mig.d["tbdash.mapLayers.v1"] = JSON.stringify({ grid: false, trail: false, threat: true });
const migrated = L.load(mig);
assert.strictEqual(migrated.grid, false); assert.strictEqual(migrated.trail, false); assert.strictEqual(migrated.threat, true);
assert.strictEqual(migrated.bots, true); assert.strictEqual(migrated.dim, 30, "v1 had no dim: the default");
assert.strictEqual(L.save(mig, L.setDim(migrated, 10)), true);
assert.ok("tbdash.mapLayers.v2" in mig.d, "the next save writes v2");
assert.strictEqual(L.load(mig).dim, 10);
mig.d["tbdash.mapLayers.v1"] = JSON.stringify({ grid: true }); // v2 wins once it exists
assert.strictEqual(L.load(mig).grid, false);
const bad1 = mem(); bad1.d["tbdash.mapLayers.v1"] = "{broken";
assert.deepStrictEqual(L.load(bad1), def, "a broken v1 gives the defaults");

// ---- the map-data layers are available once the module reports the data (an empty list counts)
const full = L.availability({ picture: true, commanderPos: true, mobs: true, mobTargets: true, botTargets: true });
["commander", "mobhostile", "mobneutral", "mobcritter", "threat", "bottarget", "aggro"].forEach((k) => assert.strictEqual(full[k].ok, true, k));
assert.ok(/newer module|does not report|not online/.test(L.availability({}).mobhostile.why));

// ---- the unit-info layers: the names toggles, boss marker, bars, icons, threat colours, events, feed and run line
const bare = L.availability({});
assert.strictEqual(bare.hpbars.ok, true); assert.strictEqual(bare.runinfo.ok, true);   // need no data of the module
["bossmark", "hostilenames", "threatcolors", "statusicons", "events", "feed"].forEach((k) => assert.strictEqual(bare[k].ok, false, k + " is greyed without data"));
const info = L.availability({ mobs: true, mobTargets: true, activity: true, events: true });
["bossmark", "hostilenames", "threatcolors", "statusicons", "events", "feed"].forEach((k) => assert.strictEqual(info[k].ok, true, k));
// the group's names and the hostile names are two separate switches
let toggled = L.set(L.set(def, "botnames", false), "hostilenames", true);
assert.strictEqual(toggled.botnames, false); assert.strictEqual(toggled.hostilenames, true);
assert.strictEqual(L.active(toggled, L.availability({ bots: true, mobs: true })).botnames, false);
assert.strictEqual(L.active(toggled, L.availability({ bots: true, mobs: true })).hostilenames, true);
assert.strictEqual(L.active(def, L.availability({ mobs: true })).bossmark, true, "the boss marker is on by default once mobs are reported");
// the saved choice of an older page (without the new keys) gets the new defaults
const old = mem(); old.d[L.STORAGE_KEY] = JSON.stringify({ grid: false });
assert.strictEqual(L.load(old).bossmark, true); assert.strictEqual(L.load(old).hpbars, true); assert.strictEqual(L.load(old).statusicons, false);

// ---- the drawing layers (paths, run steps, stalls, objects, combat lines): data needs, line styles, trail length
const draw = L.availability({});
["membertrails", "runsteps", "stalls", "objects", "healline", "ccline"].forEach((k) => assert.strictEqual(draw[k].ok, false, k + " is greyed without data"));
const withDraw = L.availability({ memberTrails: true, runSteps: true, stalls: true, objects: true, activity: true });
["membertrails", "runsteps", "stalls", "objects", "healline", "ccline"].forEach((k) => assert.strictEqual(withDraw[k].ok, true, k));
// every line layer has a style; layers that are no lines have none
const lines = L.DEFS.filter((d) => d.line).map((d) => d.key);
["trail", "membertrails", "runsteps", "threat", "bottarget", "healline", "ccline", "aggro"].forEach((k) => assert.ok(lines.includes(k), k + " is a line layer"));
assert.deepStrictEqual(Object.keys(def.style).sort(), lines.slice().sort());
lines.forEach((k) => assert.deepStrictEqual(L.styleOf(def, k), { op: 1, th: 1 }, k));
assert.deepStrictEqual(L.styleOf(def, "grid"), { op: 1, th: 1 }, "a layer without a style reads as normal");
let sty = L.setStyle(def, "trail", "op", 40);
sty = L.setStyle(sty, "trail", "th", "250");
assert.deepStrictEqual(L.styleOf(sty, "trail"), { op: 0.4, th: 2.5 });
assert.strictEqual(L.styleOf(sty, "runsteps").op, 1, "one layer's style does not touch another's");
assert.strictEqual(L.styleOf(L.setStyle(def, "trail", "op", 0), "trail").op, 0.1, "opacity is kept visible (10 % at least)");
assert.strictEqual(L.styleOf(L.setStyle(def, "trail", "th", 9999), "trail").th, 3);
assert.strictEqual(L.styleOf(L.setStyle(def, "trail", "th", 1), "trail").th, 0.5);
assert.strictEqual(L.styleOf(L.setStyle(def, "trail", "op", "junk"), "trail").op, 1, "junk falls back");
assert.deepStrictEqual(L.setStyle(def, "grid", "op", 20).style, def.style, "grid is no line layer");
assert.deepStrictEqual(L.setStyle(def, "trail", "bad", 20).style, def.style);
assert.strictEqual(L.setTrailLen(def, 60).trailLen, 60);
assert.strictEqual(L.setTrailLen(def, 5).trailLen, 10);
assert.strictEqual(L.setTrailLen(def, 5000).trailLen, 240);
assert.strictEqual(L.setTrailLen(def, "abc").trailLen, 120);
// styles survive a save and load; junk in a stored style is cleaned
const sst = mem();
L.save(sst, L.setTrailLen(sty, 50));
const back = L.load(sst);
assert.deepStrictEqual(L.styleOf(back, "trail"), { op: 0.4, th: 2.5 }); assert.strictEqual(back.trailLen, 50);
assert.strictEqual(L.normalize({ style: { trail: { op: "x", th: 99999 }, grid: { op: 5 } } }).style.trail.op, 100);
assert.strictEqual(L.normalize({ style: { trail: { op: "x", th: 99999 } } }).style.trail.th, 300);
assert.ok(!("grid" in L.normalize({ style: { grid: { op: 5 } } }).style), "styles of other keys are dropped");
assert.strictEqual(L.normalize({ style: [1, 2] }).style.trail.op, 100);
assert.strictEqual(L.normalize({ trailLen: "x" }).trailLen, 120);
// an older saved choice (no styles) gets the normal look; the default-on layer count stays honest
assert.strictEqual(L.load(old).style.trail.th, 100); assert.strictEqual(L.load(old).runsteps, true); assert.strictEqual(L.load(old).membertrails, false);
console.log("maplayers.js ok");
