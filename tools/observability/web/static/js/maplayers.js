"use strict";
// Commanders tab map layers: which things a map draws. One choice for every card (and every map), kept in the browser once, not per
// map. Pure logic (no page, no markup) so a test can run it under node (internal/commandersjs). app.js draws and shows the panel.
//
// A layer has a key, a label, a group, a default (what the maps drew before layers existed stays on, anything new is off) and `needs`:
// the data it can only be drawn from. When a map has no such data the layer is greyed out in the panel with the reason, and drawn
// nowhere; the saved choice is kept for when the data arrives.
(function (root) {
  const STORAGE_KEY = "tbdash.mapLayers.v1";

  // needs: "" (always), or a key of availability()'s context. why: what the panel says while it is missing.
  const DEFS = [
    { key: "bg", label: "Game map picture", group: "Map", def: true, needs: "picture", why: "no map picture for this map (the daemon needs TBDASH_MAP_DIR)" },
    { key: "grid", label: "Grid", group: "Map", def: true, needs: "" },
    { key: "zone", label: "Zone outline and name", group: "Map", def: true, needs: "zone", why: "the zone of the party is not known" },
    { key: "zonelabels", label: "Names of the zones around", group: "Map", def: false, needs: "zonesAround", why: "no zone outlines known for this map" },
    { key: "scale", label: "Distance scale bar", group: "Map", def: false, needs: "" },
    { key: "bots", label: "Bot dots", group: "Party", def: true, needs: "bots", why: "no bot on this map" },
    { key: "botnames", label: "Bot names", group: "Party", def: true, needs: "bots", why: "no bot on this map" },
    { key: "lead", label: "Lead mark", group: "Party", def: true, needs: "lead", why: "the lead is not reporting" },
    { key: "trail", label: "Lead's path", group: "Party", def: true, needs: "trail", why: "the lead has not moved yet" },
    { key: "trailtime", label: "Times on the path", group: "Party", def: false, needs: "trail", why: "the lead has not moved yet" },
    { key: "heading", label: "Lead's heading arrow (from its movement)", group: "Party", def: false, needs: "heading", why: "the lead has not moved enough to tell" },
    { key: "healring", label: "Healer range (40 yd)", group: "Party", def: false, needs: "healer", why: "no living healer on this map" },
    { key: "commander", label: "Commander and line to the lead", group: "Party", def: false, needs: "commanderPos", why: "the module does not report the commander's position" },
    { key: "mobhostile", label: "Hostile mobs", group: "Mobs", def: false, needs: "mobs", why: "the module's report carries no mobs" },
    { key: "mobneutral", label: "Neutral mobs", group: "Mobs", def: false, needs: "mobs", why: "the module's report carries no mobs" },
    { key: "mobcritter", label: "Critters", group: "Mobs", def: false, needs: "mobs", why: "the module's report carries no mobs" },
    { key: "threat", label: "Threat lines (mob to whom it targets)", group: "Mobs", def: false, needs: "mobTargets", why: "the module's report carries no mob targets" },
    { key: "bottarget", label: "Bot target lines", group: "Mobs", def: false, needs: "botTargets", why: "the module's report carries no bot targets" },
    { key: "aggro", label: "Aggro range (20 yd) around pull targets", group: "Mobs", def: false, needs: "mobs", why: "the module's report carries no mobs" },
  ];

  const keys = () => DEFS.map((d) => d.key);

  function defaults() {
    const o = {};
    DEFS.forEach((d) => { o[d.key] = d.def; });
    return o;
  }

  // Whatever was stored, as a full set: unknown keys are dropped, non-booleans fall back to the default.
  function normalize(raw) {
    const out = defaults();
    if (raw && typeof raw === "object" && !Array.isArray(raw)) {
      DEFS.forEach((d) => { if (typeof raw[d.key] === "boolean") out[d.key] = raw[d.key]; });
    }
    return out;
  }

  // storage is a Storage-like {getItem, setItem} (or null / one that throws: the defaults are then used and nothing is kept).
  function load(storage) {
    try {
      const s = storage && storage.getItem(STORAGE_KEY);
      return normalize(s ? JSON.parse(s) : null);
    } catch (e) {
      return defaults();
    }
  }

  function save(storage, state) {
    try {
      if (!storage) return false;
      storage.setItem(STORAGE_KEY, JSON.stringify(normalize(state)));
      return true;
    } catch (e) {
      return false;
    }
  }

  function set(state, key, on) {
    const out = normalize(state);
    if (key in out) out[key] = !!on;
    return out;
  }

  // What each layer can be drawn from, for one map (one card): {key: {ok, why}}. ctx has the booleans named in DEFS' `needs`; a missing
  // one counts as false.
  function availability(ctx) {
    const c = ctx || {}, out = {};
    DEFS.forEach((d) => { out[d.key] = !d.needs || c[d.needs] ? { ok: true, why: "" } : { ok: false, why: d.why || "no data" }; });
    return out;
  }

  // Availability over several maps: a layer is available when any of them can draw it.
  function mergeAvailability(list) {
    const out = {};
    DEFS.forEach((d) => {
      const hit = (list || []).find((a) => a && a[d.key] && a[d.key].ok);
      out[d.key] = hit ? { ok: true, why: "" } : { ok: false, why: ((list || [])[0] && list[0][d.key] && list[0][d.key].why) || d.why || "no data" };
    });
    return out;
  }

  // Which layers get drawn: chosen and with data.
  function active(state, avail) {
    const st = normalize(state), out = {};
    DEFS.forEach((d) => { out[d.key] = st[d.key] && !!(avail && avail[d.key] && avail[d.key].ok); });
    return out;
  }

  const groups = () => DEFS.reduce((g, d) => (g.indexOf(d.group) < 0 ? g.concat(d.group) : g), []);

  const api = { STORAGE_KEY, DEFS, keys, defaults, normalize, load, save, set, availability, mergeAvailability, active, groups };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.MapLayers = api;
})(typeof self !== "undefined" ? self : this);
