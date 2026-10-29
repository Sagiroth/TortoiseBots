"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's map layer state: the 27 layers in 7 groups and their
// defaults, the migration from the 32-switch v2 (and the v1 booleans), effective() (the old keys the drawing code reads),
// the per-layer greying rule, the raid presets, the trail time cut, the mob union of a commander card with its dungeon
// run, and the "N of M" note.
const assert = require("assert");
const L = require(require("path").resolve(process.argv[2]));

// a Storage stand-in
const mem = () => { const d = {}; return { d, getItem: (k) => (k in d ? d[k] : null), setItem: (k, v) => { d[k] = String(v); } }; };
const oldAll = (v) => { const o = {}; L.OLD_KEYS.forEach((k) => { o[k] = v; }); return o; };
const oldDefaults = () => { const o = {}; L.OLD_KEYS.forEach((k) => { o[k] = L.OLD_LAYERS[k].def; }); return o; };

// ---- the 27 layers, 7 groups, and the defaults the brief asks for
const KEYS = ["bg", "grid", "zone", "bots", "botnames", "hpbars", "leadpath", "memberpaths", "commanderline", "planroute",
  "planstands", "stalls", "runinfo", "enemies", "critters", "enemynames", "facing", "mobcast", "threat", "aggro", "actionlines", "subgroups",
  "healtank", "healrange", "deaths", "events", "objects"];
const def = L.defaults();
assert.deepStrictEqual(Object.keys(def).sort(), KEYS.concat(["dim", "trailLen", "trailSec", "style"]).sort());
assert.strictEqual(L.keys().length, 27);
assert.deepStrictEqual(L.groups(), ["Map", "Party", "Route", "Threat", "Raid", "Events", "Objects"]);
assert.deepStrictEqual(L.keys(), KEYS, "the panel's order");
["bg", "grid", "zone", "bots", "botnames", "hpbars", "leadpath", "planroute", "planstands", "runinfo", "enemies", "enemynames",
  "facing", "mobcast", "threat", "aggro", "deaths", "events"].forEach((k) => assert.strictEqual(def[k], true, k));
["memberpaths", "commanderline", "critters", "stalls", "actionlines", "subgroups", "healtank", "healrange", "objects"]
  .forEach((k) => assert.strictEqual(def[k], false, k));
assert.strictEqual(L.DEFS.filter((d) => d.def).length, 18);
assert.strictEqual(new Set(L.keys()).size, L.keys().length);
L.DEFS.forEach((d) => {
  assert.ok(d.label && d.group && d.tip, d.key + " has a label, a group and a tooltip");
  assert.ok(d.label.length <= 48, d.key + " has a short label");
  assert.ok(Array.isArray(d.parts) && d.parts.length && d.parts.every((p) => L.OLD_LAYERS[p]), d.key + " names its old parts");
  assert.ok(d.line ? L.OLD_LAYERS[d.styleFrom] && L.OLD_LAYERS[d.styleFrom].line : !d.styleFrom, d.key + " takes a line style from an old line key");
});
// every old key belongs to exactly one new layer (runsteps is dead since the run leader left this branch)
const owned = L.DEFS.reduce((a, d) => a.concat(d.parts), []);
assert.deepStrictEqual(owned.slice().sort(), L.OLD_KEYS.slice().sort());
assert.strictEqual(new Set(owned).size, owned.length);
// the new parts the raid layers add
["planroute", "planstands", "planrings", "subgroups", "healtank", "deaths"].forEach((k) => assert.ok(L.OLD_LAYERS[k], k + " is an old part"));
assert.strictEqual(L.OLD_LAYERS.planroute.def, true);
assert.strictEqual(L.OLD_LAYERS.planstands.def, true);
assert.strictEqual(L.OLD_LAYERS.planrings.def, true);
assert.strictEqual(L.OLD_LAYERS.subgroups.def, false);
assert.strictEqual(L.OLD_LAYERS.healtank.def, false);
assert.strictEqual(L.OLD_LAYERS.deaths.def, true);
["planroute", "planstands", "planrings"].forEach((k) => {
  assert.strictEqual(L.OLD_LAYERS[k].needs, "plan", k);
  assert.ok(L.OLD_LAYERS[k].why.length > 5, k + " says why");
});
assert.strictEqual(L.OLD_LAYERS.subgroups.needs, "subgroups");
assert.strictEqual(L.OLD_LAYERS.healtank.needs, "layoutHealers");
assert.strictEqual(L.OLD_LAYERS.deaths.needs, "fightDeaths");
// the boss overlay's two layers: their own data, and what the cast one says when nothing casts
assert.strictEqual(L.OLD_LAYERS.facing.needs, "facing");
assert.strictEqual(L.OLD_LAYERS.mobcast.needs, "mobcast");
assert.strictEqual(L.OLD_LAYERS.facing.def, true);
assert.strictEqual(L.OLD_LAYERS.mobcast.def, true);
assert.strictEqual(L.OLD_LAYERS.mobcast.why, "no boss or elite is casting now");
assert.ok(L.OLD_LAYERS.facing.why.length > 5);

// ---- effective(): the old keys the drawing code reads, computed from the new ones
const eff = L.effective(def);
assert.deepStrictEqual(Object.keys(eff).sort(), L.OLD_KEYS.concat(["runsteps"]).sort());
["bg", "grid", "scale", "zone", "zonelabels", "bots", "botnames", "lead", "trail", "trailtime", "heading", "hpbars", "statusicons",
  "mobhostile", "mobneutral", "hostilenames", "bossmark", "facing", "mobcast", "threatcolors", "threat", "aggro", "events", "feed", "runinfo",
  "planroute", "planstands", "planrings", "deaths"].forEach((k) => assert.strictEqual(eff[k], true, k));
["membertrails", "healring", "commander", "mobcritter", "bottarget", "healline", "ccline", "stalls", "objects", "subgroups", "healtank"]
  .forEach((k) => assert.strictEqual(eff[k], false, k));
assert.strictEqual(eff.runsteps, false, "the run route is not offered (the run leader is on its own branch)");
const e2 = L.effective(L.set(L.set(L.set(def, "enemies", false), "critters", true), "bots", false));
assert.strictEqual(e2.mobhostile, false); assert.strictEqual(e2.mobneutral, false); assert.strictEqual(e2.mobcritter, true);
assert.strictEqual(e2.bots, false); assert.strictEqual(e2.lead, false);
assert.strictEqual(e2.botnames, true, "all bot names has its own layer now");

// ---- availability: greyed only when NONE of a layer's parts has data
let av = L.availability({});
assert.deepStrictEqual(Object.keys(av).sort(), L.keys().slice().sort());
assert.deepStrictEqual(L.keys().filter((k) => av[k].ok).sort(), ["grid", "hpbars", "runinfo"], "only the layers that need nothing are drawn without data");
L.keys().filter((k) => !av[k].ok).forEach((k) => assert.ok(av[k].why.length > 5, k + " says why"));
// the map folder's own reason (the missing minimap's) stands in for the built-in one of the map picture
assert.strictEqual(L.availability({ pictureWhy: "No minimap picture: indoor dungeon" }).bg.why, "No minimap picture: indoor dungeon");
assert.strictEqual(L.availability({ picture: true, pictureWhy: "ignored" }).bg.ok, true);
assert.strictEqual(L.availability({ pictureWhy: "" }).bg.why, L.OLD_LAYERS.bg.why);
// the plan layers say the card's own reason when it has one
assert.strictEqual(av.planroute.why, L.OLD_LAYERS.planroute.why);
assert.strictEqual(L.availability({ planWhy: "No plan named x" }).planroute.why, "No plan named x");
assert.strictEqual(L.availability({ planWhy: "No plan named x" }).planstands.why, "No plan named x");
assert.ok(L.availability({ planWhy: "No plan named x" }).aggro.why.indexOf("No plan named x") >= 0, "the rings part says it too");
assert.strictEqual(L.availability({ plan: true, planWhy: "ignored" }).planroute.ok, true);
assert.strictEqual(L.availability({ plan: true }).planstands.ok, true);
assert.strictEqual(L.availability({ plan: true }).aggro.ok, true, "the wake/aggro rings have their plan");
assert.strictEqual(L.availability({ layoutHealers: true }).healtank.ok, true);
assert.strictEqual(L.availability({ subgroups: true }).subgroups.ok, true);
assert.strictEqual(L.availability({ fightDeaths: true }).deaths.ok, true);
assert.strictEqual(L.availability({ facing: true }).facing.ok, true, "a reported facing makes the facing layer available");
assert.strictEqual(L.availability({ mobcast: true }).mobcast.ok, true, "a reported cast makes the cast layer available");
assert.strictEqual(L.availability({ mobs: true }).facing.ok, false, "mobs alone say nothing about facing");
assert.strictEqual(L.availability({ mobs: true }).mobcast.why, "no boss or elite is casting now");
assert.strictEqual(L.availability({}).healtank.why, "no tank healers in the raid layout");
// bots: the dots and the lead mark need bots or a lead; the names need a bot
av = L.availability({ lead: true });
assert.strictEqual(av.bots.ok, true, "the lead mark alone makes the party layer available");
assert.strictEqual(av.botnames.ok, false, "names need a bot");
assert.strictEqual(L.availability({ bots: true }).botnames.ok, true);
av = L.availability({ mobs: true });
["enemies", "critters", "enemynames", "aggro"].forEach((k) => assert.strictEqual(av[k].ok, true, k));
assert.strictEqual(av.threat.ok, false, "threat colours need the targets, not just the mobs");
assert.strictEqual(av.actionlines.ok, false);
av = L.availability({ mobs: true, mobTargets: true, activity: true, events: true });
["threat", "actionlines", "events"].forEach((k) => assert.strictEqual(av[k].ok, true, k));
av = L.availability({ zone: true });
assert.strictEqual(av.zone.ok, true);
assert.strictEqual(L.availability({ zonesAround: true }).zone.ok, true, "either zone source is enough");
av = L.availability({ healer: true });
assert.strictEqual(av.healrange.ok, true);
assert.strictEqual(L.availability({ commanderPos: true }).commanderline.ok, true);
// several maps: available when any can draw it
const m = L.mergeAvailability([L.availability({}), L.availability({ mobs: true })]);
assert.strictEqual(m.enemies.ok, true); assert.strictEqual(m.grid.ok, true); assert.strictEqual(m.threat.ok, false);
assert.strictEqual(L.mergeAvailability([]).grid.ok, false);
// drawn = chosen by the new key, its merged layer available, and its OWN data there
const act = L.active(L.set(def, "enemies", false), L.availability({ mobs: true }), { mobs: true });
assert.strictEqual(act.mobhostile, false); assert.strictEqual(act.hostilenames, true); assert.strictEqual(act.mobcritter, false);
assert.strictEqual(act.grid, true); assert.strictEqual(act.runsteps, false);
const act2 = L.active(def, L.availability({}));
assert.strictEqual(act2.mobhostile, false, "chosen but no data");
assert.strictEqual(act2.grid, true); assert.strictEqual(act2.hpbars, true);
// one part's data does not switch another part of the same layer on
let part = L.active(def, L.availability({ mobs: true }), { mobs: true });
assert.strictEqual(part.hpbars, true); assert.strictEqual(part.statusicons, false, "no activity data: no status icons");
assert.strictEqual(part.hostilenames, true); assert.strictEqual(part.bossmark, true);
part = L.active(def, L.availability({ lead: true }), { lead: true });
assert.strictEqual(part.lead, true); assert.strictEqual(part.bots, false); assert.strictEqual(part.botnames, false, "no bot on this map");
part = L.active(L.set(def, "healrange", true), L.availability({ healer: true }), { healer: true });
assert.strictEqual(part.healring, true); assert.strictEqual(part.commander, false, "only a healer: no commander mark");
part = L.active(L.set(def, "commanderline", true), L.availability({ commanderPos: true }), { commanderPos: true });
assert.strictEqual(part.commander, true); assert.strictEqual(part.healring, false, "the commander line is its own layer now");
part = L.active(L.set(L.set(def, "planstands", false), "planroute", true), L.availability({ plan: true }), { plan: true });
assert.strictEqual(part.planroute, true); assert.strictEqual(part.planstands, false, "each plan layer has its own switch");
part = L.active(L.set(L.set(def, "planroute", false), "aggro", true), L.availability({ plan: true }), { plan: true });
assert.strictEqual(part.planrings, true); assert.strictEqual(part.planroute, false, "the rings are the aggro layer's part");
part = L.active(def, L.availability({ trail: true }), { trail: true });
assert.strictEqual(part.trail, true); assert.strictEqual(part.trailtime, true); assert.strictEqual(part.heading, false, "no heading");
part = L.active(L.set(def, "enemies", true), L.availability({ mobs: true }), {});
assert.strictEqual(part.mobhostile, false, "the merged layer is available but this part's data is missing");
assert.strictEqual(part.grid, true, "a part that needs nothing is drawn");

// ---- one global choice, kept once
const st = mem();
assert.deepStrictEqual(L.load(st), def);                    // nothing stored: the defaults
let s = L.set(L.load(st), "grid", false);
s = L.set(s, "enemies", false);
assert.strictEqual(L.save(st, s), true);
assert.strictEqual(Object.keys(st.d).length, 1);             // one key, not one per map
assert.strictEqual(Object.keys(st.d)[0], L.STORAGE_KEY);
assert.strictEqual(L.STORAGE_KEY, "tbdash.mapLayers.v3");
const again = L.load(st);
assert.strictEqual(again.grid, false); assert.strictEqual(again.enemies, false); assert.strictEqual(again.bots, true);
assert.strictEqual(L.set(def, "mobhostile", true).mobhostile, undefined, "an old key is not a switch any more");
assert.strictEqual(L.set(def, "nope", true).nope, undefined);

// ---- bad storage never breaks the page
assert.deepStrictEqual(L.load(null), def);
assert.deepStrictEqual(L.load({ getItem() { throw new Error("blocked"); } }), def);
assert.strictEqual(L.save({ setItem() { throw new Error("full"); } }, def), false);
assert.strictEqual(L.save(null, def), false);
const junk = mem(); junk.d[L.STORAGE_KEY] = "{not json";
assert.deepStrictEqual(L.load(junk), def);
junk.d[L.STORAGE_KEY] = JSON.stringify({ grid: "yes", bots: false, zzz: true, trail: null, mobhostile: false });
const jl = L.load(junk);
assert.strictEqual(jl.grid, true);  // a non-boolean falls back to the default
assert.strictEqual(jl.bots, false); // a boolean is kept
assert.strictEqual(jl.zzz, undefined);
assert.strictEqual(jl.trail, undefined, "old v2 keys stored in v3 are ignored");
junk.d[L.STORAGE_KEY] = "[1,2]";
assert.deepStrictEqual(L.load(junk), def);

// ---- v3 users keep their healer layer: partyextras carried over when the split layers have no value of their own
let carry = L.normalize({ partyextras: true });
assert.strictEqual(carry.healrange, true);
assert.strictEqual(carry.commanderline, true);
carry = L.normalize({ partyextras: true, healrange: false });
assert.strictEqual(carry.healrange, false, "an explicit healrange wins");
assert.strictEqual(carry.commanderline, true);
assert.strictEqual(L.normalize({ partyextras: false }).healrange, false);
assert.strictEqual(L.normalize({ partyextras: "yes" }).healrange, def.healrange, "a non-boolean partyextras is ignored");
const v3 = mem();
v3.d[L.STORAGE_KEY] = JSON.stringify({ partyextras: true });
assert.strictEqual(L.load(v3).healrange, true);
assert.strictEqual(L.load(v3).commanderline, true);

// ---- v2 -> v3: a merged layer is on when ANY of its old layers was; dim, path length and line styles carry over
const v2 = mem();
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldDefaults());
let mig = L.load(v2);
L.keys().forEach((k) => assert.strictEqual(mig[k], def[k], "untouched v2 defaults: the new default of " + k));
assert.strictEqual(mig.enemies, true, "a v2 that never touched the mob switches gets the new enemies default");
assert.strictEqual(mig.threat, true); assert.strictEqual(mig.events, true); assert.strictEqual(mig.runinfo, true);
assert.strictEqual(mig.aggro, true, "the aggro rings are on by default now");
assert.strictEqual(mig.deaths, true); assert.strictEqual(mig.planroute, true); assert.strictEqual(mig.healtank, false);
assert.strictEqual(mig.dim, 30); assert.strictEqual(mig.trailLen, 120); assert.strictEqual(mig.trailSec, 0);
assert.deepStrictEqual(mig.style.leadpath, { op: 100, th: 100 }, "no old style: the normal look");

v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldAll(false));
mig = L.load(v2);
// the layers whose old switches were all off by default cannot tell "untouched" from "all off": they take the new default
["enemies", "threat", "events", "runinfo"].forEach((k) => assert.strictEqual(mig[k], true, k + " takes the new default"));
L.keys().filter((k) => ["enemies", "threat", "events", "runinfo"].indexOf(k) < 0).forEach((k) => assert.strictEqual(mig[k], false, k + " stays off"));

v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldAll(true));
mig = L.load(v2);
L.keys().forEach((k) => assert.strictEqual(mig[k], true, k + " on: an old switch was on or its source default was on"));

const mixed = oldDefaults();
Object.assign(mixed, { grid: false, botnames: false, lead: false, mobhostile: true, hostilenames: true, bossmark: false,
  threatcolors: true, events: true, runinfo: true, membertrails: true });
mixed.dim = 55; mixed.trailLen = 60; mixed.style = { trail: { op: 40, th: 250 }, membertrails: { op: 70, th: 150 }, threat: { op: 60, th: 300 }, bottarget: { op: 20, th: 90 } };
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(mixed);
mig = L.load(v2);
assert.deepStrictEqual(["bg", "zone", "bots", "leadpath", "hpbars", "memberpaths", "planroute", "planstands", "runinfo", "enemies",
  "enemynames", "threat", "aggro", "deaths", "events"].map((k) => mig[k]),
  [true, true, true, true, true, true, true, true, true, true, true, true, true, true, true], "the mixed fixture's on bits carry");
["grid", "botnames", "commanderline", "stalls", "critters", "actionlines", "subgroups", "healtank", "healrange", "objects"]
  .forEach((k) => assert.strictEqual(mig[k], false, k));
assert.strictEqual(mig.dim, 55); assert.strictEqual(mig.trailLen, 60);
assert.deepStrictEqual(mig.style.leadpath, { op: 40, th: 250 }, "leadpath takes the old trail style (its first old line key)");
assert.deepStrictEqual(mig.style.memberpaths, { op: 70, th: 150 });
assert.deepStrictEqual(mig.style.threat, { op: 60, th: 300 }, "threat colours have no style; the threat-line style carries");
assert.deepStrictEqual(mig.style.actionlines, { op: 20, th: 90 }, "actionlines takes the bot-target style");
assert.deepStrictEqual(mig.style.aggro, { op: 100, th: 100 });

// a user's real choices survive: a default-on old switch turned OFF keeps its merged layer off, a default-off one turned ON turns it on
const chosen = oldDefaults();
chosen.trail = false; chosen.bots = false; chosen.botnames = false; chosen.lead = false; chosen.mobcritter = true;
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(chosen);
mig = L.load(v2);
assert.strictEqual(mig.leadpath, false, "trail off in v2 survives (trailtime/heading were default off)");
assert.strictEqual(mig.bots, false, "both bots switches off in v2 survive (the merged layer stays off)");
assert.strictEqual(mig.botnames, false, "the names switch off in v2 survives");
assert.strictEqual(mig.critters, true, "a critter switch the user turned on survives");
assert.strictEqual(mig.enemies, true, "the untouched mob switches still get the new default");
// the v2 pins the spec adds: aggro touched on is on; untouched is the new default (on)
const aggroOn = oldDefaults(); aggroOn.aggro = true;
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(aggroOn);
assert.strictEqual(L.load(v2).aggro, true);
const aggroOff = oldDefaults(); delete aggroOff.aggro; delete aggroOff.planrings;
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(aggroOff);
assert.strictEqual(L.load(v2).aggro, true, "untouched: the new default");
// the next save writes v3 and v3 then wins
assert.strictEqual(L.save(v2, L.setDim(mig, 10)), true);
assert.ok(L.STORAGE_KEY in v2.d);
assert.strictEqual(L.load(v2).dim, 10);
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldAll(true));
assert.strictEqual(L.load(v2).bots, false, "v3 wins once it exists");
// a v2 without a layer added later uses that old layer's default
const older = mem(); older.d[L.PREV_STORAGE_KEY] = JSON.stringify({ grid: false });
mig = L.load(older);
assert.strictEqual(mig.grid, false); assert.strictEqual(mig.zone, true); assert.strictEqual(mig.bossmark, undefined);
assert.strictEqual(mig.enemynames, true, "bossmark was on by default in v2, so the merged layer keeps its new default");
assert.strictEqual(mig.hpbars, true); assert.strictEqual(mig.threat, true, "the untouched threat switches get the new default");
assert.strictEqual(mig.aggro, true); assert.strictEqual(mig.deaths, true); assert.strictEqual(mig.planroute, true);

// ---- v1 still migrates, v2 wins over v1, and a broken one gives the defaults
const v1 = mem();
v1.d[L.OLD_STORAGE_KEY] = JSON.stringify({ grid: false, trail: false, threat: true });
mig = L.load(v1);
assert.strictEqual(mig.grid, false); assert.strictEqual(mig.leadpath, false); assert.strictEqual(mig.threat, true);
assert.strictEqual(mig.bots, true); assert.strictEqual(mig.dim, 30, "v1 had no dim: the default");
v1.d[L.PREV_STORAGE_KEY] = JSON.stringify({ grid: true });
assert.strictEqual(L.load(v1).grid, true, "v2 wins over v1");
const bad1 = mem(); bad1.d[L.OLD_STORAGE_KEY] = "{broken";
assert.deepStrictEqual(L.load(bad1), def);
const bad3 = mem(); bad3.d[L.STORAGE_KEY] = "{broken"; bad3.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldAll(true));
assert.deepStrictEqual(L.load(bad3), def, "a broken v3 does not fall back to a stale v2");

// ---- the dim and path-length sliders
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

assert.strictEqual(L.setTrailLen(def, 60).trailLen, 60);
assert.strictEqual(L.setTrailLen(def, 5).trailLen, 10);
assert.strictEqual(L.setTrailLen(def, 5000).trailLen, 240);
assert.strictEqual(L.setTrailLen(def, "abc").trailLen, 120);
const dst = mem();
L.save(dst, L.setTrailLen(L.setDim(L.set(def, "grid", false), 65), 50));
assert.strictEqual(Object.keys(dst.d).length, 1);
assert.strictEqual(L.load(dst).dim, 65); assert.strictEqual(L.load(dst).grid, false); assert.strictEqual(L.load(dst).trailLen, 50);

// ---- the trail time cut: 0 (no cut) or 10-300 s, saved with the rest
assert.strictEqual(def.trailSec, 0);
assert.strictEqual(L.setTrailSec(def, 5).trailSec, 0);
assert.strictEqual(L.setTrailSec(def, 9).trailSec, 0);
assert.strictEqual(L.setTrailSec(def, 10).trailSec, 10);
assert.strictEqual(L.setTrailSec(def, 30).trailSec, 30);
assert.strictEqual(L.setTrailSec(def, "60").trailSec, 60);
assert.strictEqual(L.setTrailSec(def, 300).trailSec, 300);
assert.strictEqual(L.setTrailSec(def, 301).trailSec, 0);
assert.strictEqual(L.setTrailSec(def, 0).trailSec, 0);
assert.strictEqual(L.setTrailSec(def, "abc").trailSec, 0);
const ts = mem();
L.save(ts, L.setTrailSec(L.set(def, "grid", false), 120));
assert.strictEqual(L.load(ts).trailSec, 120);
assert.strictEqual(L.load(ts).grid, false);

// ---- the raid presets
assert.deepStrictEqual(L.PRESETS.map((p) => p.name), ["pathing", "pull", "heal", "boss", "all"]);
assert.deepStrictEqual(L.PRESET_LABELS, ["Pathing", "Trash pull", "Healing", "Boss", "All"]);
assert.deepStrictEqual(L.RAID_KEYS, ["planroute", "planstands", "leadpath", "memberpaths", "aggro", "threat", "enemynames",
  "subgroups", "botnames", "healtank", "healrange", "deaths"]);
L.RAID_KEYS.forEach((k) => assert.ok(def[k] === true || def[k] === false, k + " is a layer"));
assert.strictEqual(L.RAID_KEYS.indexOf("bg"), -1);
let h = L.applyPreset(L.defaults(), "heal");
["healtank", "healrange", "subgroups", "threat", "deaths"].forEach((k) => assert.strictEqual(h[k], true, k));
["planroute", "aggro", "leadpath", "planstands", "memberpaths", "enemynames", "botnames"].forEach((k) => assert.strictEqual(h[k], false, k));
assert.strictEqual(h.bg, true, "a preset never touches a non-raid key");
assert.strictEqual(h.runinfo, true, "a non-raid key keeps its value");
assert.strictEqual(h.stalls, false, "a non-raid key keeps its value");
assert.strictEqual(h.dim, 30);
assert.strictEqual(h.trailSec, 0);
assert.strictEqual(L.presetOf(h), "heal");
assert.strictEqual(L.presetOf(L.set(h, "bots", false)), "heal", "bots is not a raid key");
assert.strictEqual(L.presetOf(L.set(h, "aggro", true)), "");
assert.strictEqual(L.presetOf(L.setDim(h, 10)), "heal");
assert.strictEqual(L.applyPreset(L.defaults(), "pathing").trailSec, 30);
assert.strictEqual(L.presetOf(L.applyPreset(L.defaults(), "pathing")), "pathing");
assert.strictEqual(L.applyPreset(L.setTrailSec(L.defaults(), 60), "pull").trailSec, 60, "pull keeps the old cut");
assert.strictEqual(L.presetOf(L.setTrailSec(L.applyPreset(L.defaults(), "pathing"), 60)), "", "pathing sets 30 s");
const all = L.applyPreset(L.defaults(), "all");
L.RAID_KEYS.forEach((k) => assert.strictEqual(all[k], true, k));
assert.strictEqual(all.trailSec, 30);
assert.strictEqual(all.bg, true);
assert.strictEqual(L.presetOf(all), "all");
assert.strictEqual(L.presetOf(L.setTrailSec(all, 0)), "", "the cut is part of the preset");
assert.deepStrictEqual(L.applyPreset(L.defaults(), "nope"), L.defaults());
assert.strictEqual(L.applyPreset(L.set(L.defaults(), "bg", false), "heal").bg, false, "a preset leaves a non-raid key alone");
assert.strictEqual(L.applyPreset(L.set(L.defaults(), "stalls", true), "boss").stalls, true);
// the boss overlay's two layers follow enemies when a preset is applied: a preset that leaves the enemies on shows them
assert.strictEqual(L.applyPreset(L.defaults(), "boss").facing, true);
assert.strictEqual(L.applyPreset(L.defaults(), "boss").mobcast, true);
assert.strictEqual(L.applyPreset(L.defaults(), "all").facing, true);
const noEnemies = L.applyPreset(L.set(L.defaults(), "enemies", false), "heal");
assert.strictEqual(noEnemies.facing, false); assert.strictEqual(noEnemies.mobcast, false);
assert.strictEqual(L.presetOf(L.applyPreset(L.defaults(), "boss")), "boss", "the overlay layers are not raid keys");
// a preset is a whole state: the saved choice round-trips it
const ps = mem();
L.save(ps, L.applyPreset(L.defaults(), "boss"));
assert.strictEqual(L.presetOf(L.load(ps)), "boss");

// ---- line styles: 5 line layers, opacity 10-100 %, thickness 50-300 %
const lines = L.DEFS.filter((d) => d.line).map((d) => d.key);
assert.deepStrictEqual(lines, ["leadpath", "memberpaths", "threat", "aggro", "actionlines"]);
assert.deepStrictEqual(Object.keys(def.style).sort(), lines.slice().sort());
lines.forEach((k) => assert.deepStrictEqual(L.styleOf(def, k), { op: 1, th: 1 }, k));
assert.deepStrictEqual(L.styleOf(def, "grid"), { op: 1, th: 1 }, "a layer without a style reads as normal");
let sty = L.setStyle(def, "leadpath", "op", 40);
sty = L.setStyle(sty, "leadpath", "th", "250");
assert.deepStrictEqual(L.styleOf(sty, "leadpath"), { op: 0.4, th: 2.5 });
assert.strictEqual(L.styleOf(sty, "aggro").op, 1, "one layer's style does not touch another's");
assert.strictEqual(L.styleOf(L.setStyle(def, "leadpath", "op", 0), "leadpath").op, 0.1, "opacity is kept visible (10 % at least)");
assert.strictEqual(L.styleOf(L.setStyle(def, "leadpath", "th", 9999), "leadpath").th, 3);
assert.strictEqual(L.styleOf(L.setStyle(def, "leadpath", "th", 1), "leadpath").th, 0.5);
assert.strictEqual(L.styleOf(L.setStyle(def, "leadpath", "op", "junk"), "leadpath").op, 1, "junk falls back");
assert.deepStrictEqual(L.setStyle(def, "grid", "op", 20).style, def.style, "grid is no line layer");
assert.deepStrictEqual(L.setStyle(def, "trail", "op", 20).style, def.style, "the old key is not a line layer any more");
assert.deepStrictEqual(L.setStyle(def, "planroute", "op", 20).style, def.style, "the plan route has no style");
assert.deepStrictEqual(L.setStyle(def, "leadpath", "bad", 20).style, def.style);
const sst = mem();
L.save(sst, sty);
assert.deepStrictEqual(L.styleOf(L.load(sst), "leadpath"), { op: 0.4, th: 2.5 });
assert.strictEqual(L.normalize({ style: { leadpath: { op: "x", th: 99999 } } }).style.leadpath.op, 100);
assert.strictEqual(L.normalize({ style: { leadpath: { op: "x", th: 99999 } } }).style.leadpath.th, 300);
assert.ok(!("grid" in L.normalize({ style: { grid: { op: 5 } } }).style), "styles of other keys are dropped");
assert.strictEqual(L.normalize({ style: [1, 2] }).style.leadpath.op, 100);
assert.strictEqual(L.normalize({ trailLen: "x" }).trailLen, 120);

// ---- the union by guid: the feed first, it wins conflicts and fills what the run fills in; no id, no union entry
const feed = [
  { id: "1", name: "A", hp: 50, x: 1, y: 2, victim: "9001", reaction: "h" },
  { id: "2", name: "B" },
  { id: "1", name: "A again", hp: 1 },
];
const run = [
  { id: "1", name: "A-run", hp: 80, level: 40, rank: "e" },
  { id: "3", name: "C", hp: 10 },
  { name: "no guid", hp: 90 },
];
const u = L.mobUnion(feed, run);
assert.deepStrictEqual(u.map((m) => m.id), ["1", "2", "3"]);
assert.strictEqual(u[0].name, "A", "the feed wins a conflicting field");
assert.strictEqual(u[0].hp, 50, "the feed's health wins");
assert.strictEqual(u[0].victim, "9001");
assert.strictEqual(u[0].level, 40, "a field the feed lacks is filled from the run");
assert.strictEqual(u[0].rank, "e");
assert.strictEqual(u[1].name, "B");
assert.strictEqual(u[2].name, "C");
assert.strictEqual(u[2].hp, 10);
assert.deepStrictEqual(L.mobUnion([], run).map((m) => m.id), ["1", "3"], "guid-less run mobs are dropped");
assert.deepStrictEqual(L.mobUnion(feed, []).map((m) => m.id), ["1", "2"]);
assert.deepStrictEqual(L.mobUnion(null, null), []);
assert.deepStrictEqual(L.mobUnion([{ id: 5, hp: 20 }], [{ id: "5", name: "N", x: 3 }]), [{ id: "5", hp: 20, name: "N", x: 3 }],
  "a numeric and a string id are the same unit, and the empty name is filled");
assert.strictEqual(feed.length, 3, "the inputs are not changed");
// caps: the commander feed's 24 and a raid run's 48 are drawn together, every guid once (a cap is not a reason to drop one side)
const capFeed = Array.from({ length: 24 }, (_, i) => ({ id: String(i + 1), name: "feed " + i, reaction: "h" }));
const capRun = Array.from({ length: 48 }, (_, i) => ({ id: String(i + 1), name: "run " + i, reaction: i % 2 ? "h" : "n" }));
const capped = L.mobUnion(capFeed, capRun);
assert.strictEqual(capped.length, 48, "the run's list covers the feed's ids and adds the rest");
assert.strictEqual(new Set(capped.map((m) => m.id)).size, 48);
assert.strictEqual(capped[0].name, "feed 0", "the shared ids keep the feed's fields");
assert.strictEqual(capped[30].name, "run 30");
assert.strictEqual(L.enemiesText(capped.length, 57), "48 of 57 enemies shown");
const filled = L.mobUnion([{ id: "7", name: "", hp: 5 }], [{ id: "7", name: "Seven", hp: 99 }])[0];
assert.strictEqual(filled.name, "Seven"); assert.strictEqual(filled.hp, 5);

// ---- the card's run: same map and one shared bot; guids decide whenever both sides have them, names only when one side does not
const bind = { commander: "C", bots: [{ name: "A", guid: 1, map: 70 }, { name: "B", guid: 2, map: 70 }] };
const r1 = { key: "70:1", map: 70, instance: 1, members: [{ name: "A", guid: 1 }, { name: "P", guid: 9 }] };
const r2 = { key: "70:2", map: 70, instance: 2, members: [{ name: "B", guid: 2 }] };
assert.strictEqual(L.findRun(bind, [r1], 70), r1);
assert.strictEqual(L.findRun(bind, [r1], 71), null, "another map is no match");
assert.strictEqual(L.findRun({ bots: [{ name: "B", guid: 2, map: 70 }] }, [r1], 70), null, "no shared bot");
assert.strictEqual(L.findRun({ bots: [{ name: "A", guid: 1, map: 70 }] }, [r1, r2], 70), r1, "one shared bot picks its copy");
assert.strictEqual(L.findRun({ bots: [{ name: "B", guid: 2, map: 70 }] }, [r1, r2], 70), r2, "the other copy stays isolated");
assert.strictEqual(L.findRun(bind, [r1, r2], 70), null, "a party cannot be in two copies: no clean match");
// guids win over names: same name, different guid is another unit
assert.strictEqual(L.findRun({ bots: [{ name: "A", guid: 1, map: 70 }] }, [{ key: "70:1", map: 70, members: [{ name: "A", guid: 99 }] }], 70), null,
  "equal names with different guids do not match");
assert.strictEqual(L.findRun({ bots: [{ name: "A", guid: 1, map: 70 }] }, [{ key: "70:1", map: 70, members: [{ name: "Other", guid: 1 }] }], 70).key, "70:1",
  "equal guids match however the names read");
assert.strictEqual(L.findRun({ bots: [{ name: "A", map: 70 }] }, [r1], 70), r1, "the name is used when the binding has no guid");
assert.strictEqual(L.findRun({ bots: [{ name: "A", guid: 1, map: 70 }] }, [{ key: "70:1", map: 70, members: [{ name: "A" }] }], 70).key, "70:1",
  "the name is used when the run has no guid");
assert.strictEqual(L.findRun({ ...bind, demo: true }, [r1], 70), null, "the demo never matches a real run (its guids can collide)");
assert.strictEqual(L.findRun({ ...bind, dungeon: true }, [r1], 70), null, "a Dungeons-tab binding already carries its run");
assert.strictEqual(L.findRun(bind, null, 70), null);
assert.strictEqual(L.findRun({ bots: [] }, [r1], 70), null);
assert.strictEqual(L.findRun(bind, [{ map: 70, members: [] }], 70), null);

// ---- "N of M enemies shown" only when the cap hid some
assert.strictEqual(L.enemiesText(24, 57), "24 of 57 enemies shown");
assert.strictEqual(L.enemiesText(57, 57), "");
assert.strictEqual(L.enemiesText(0, 0), "");
assert.strictEqual(L.enemiesText(10, 0), "");
assert.strictEqual(L.enemiesText(0, 3), "0 of 3 enemies shown");
assert.strictEqual(L.enemiesText("x", "y"), "");
console.log("maplayers.js ok");
