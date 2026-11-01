"use strict";
// Trash abilities: the pure half of the dashboard's "Dangerous here" block. It turns the committed trash table
// (/data/trash_abilities.json, written by tools/boss_data.py from data/boss/trash_abilities.csv) into the one-line hints
// the page draws for the trash a live card can meet: which hostile rows matter, the kill order, the spell to kick and the
// roles to expect. No DOM, no fetch at load; unit-tested by internal/commandersjs/trash_check.js.
//
// Table shape (the converter's JSON, one mob per creature entry):
//   { "version": 1, "note": "...",
//     "maps": [ { "key": "zulgurub", "mapId": 309 } ],
//     "mobs": { "15111": { "entry": 15111, "name": "Mad Servant", "map": "zulgurub", "danger": 3,
//         "roles": ["caster", "summoner"], "kill": 3,
//         "abilities": [ { "sid": 24611, "name": "Fireball", "int": true, "t": "victim", "aoe": false,
//                          "ground": false, "cone": false, "r": 0, "ok": "" } ],
//         "notes": [ "..." ] } } }
(function (root) {
  const num = (v) => (typeof v === "number" && isFinite(v) ? v : typeof v === "string" && v.trim() !== "" && isFinite(Number(v)) ? Number(v) : null);
  const str = (v) => (typeof v === "string" ? v : v == null ? "" : String(v));
  const arr = (v) => (Array.isArray(v) ? v : v == null ? [] : [v]);

  // ---- the table -------------------------------------------------------------------------------------------------------

  function ability(row) {
    if (!row || typeof row !== "object") return null;
    const sid = num(row.sid);
    if (sid === null) return null;
    return {
      sid: sid,
      name: str(row.name) || ("spell " + sid),
      int: !!row.int,
      t: str(row.t),
      aoe: !!row.aoe,
      ground: !!row.ground,
      cone: !!row.cone,
      r: num(row.r) || 0,
      ok: str(row.ok),
    };
  }

  function mob(row) {
    if (!row || typeof row !== "object") return null;
    const entry = num(row.entry);
    if (entry === null) return null;
    return {
      entry: entry,
      name: str(row.name) || ("creature " + entry),
      map: str(row.map),
      danger: Math.max(0, Math.min(3, num(row.danger) || 0)),
      kill: Math.max(0, Math.min(3, num(row.kill) || 0)),
      roles: arr(row.roles).map(str).filter(Boolean),
      abilities: arr(row.abilities).map(ability).filter(Boolean),
      notes: arr(row.notes).map(str).filter(Boolean),
    };
  }

  // Accepts the converter's object (mobs keyed by entry) or a bare mob array; an unknown shape gives an empty index.
  function normalize(data) {
    const doc = data && typeof data === "object" ? data : {};
    const maps = arr(doc.maps).map((m) => (m && typeof m === "object" ? { key: str(m.key), mapId: num(m.mapId) } : null)).filter(Boolean);
    const raw = doc.mobs;
    let rows = [];
    if (Array.isArray(raw)) rows = raw;
    else if (raw && typeof raw === "object") rows = Object.keys(raw).map((k) => {
      const v = raw[k];
      if (!v || typeof v !== "object") return null;
      return num(v.entry) === null ? Object.assign({ entry: k }, v) : v;
    });
    const mobs = rows.map(mob).filter(Boolean).sort((a, b) => a.entry - b.entry);
    const byEntry = {};
    mobs.forEach((m) => { if (!(m.entry in byEntry)) byEntry[m.entry] = m; });
    return { maps: maps, mobs: mobs, byEntry: byEntry };
  }

  let cache = null, loading = null;
  // Fetch the table once; a missing file (an old daemon) or one with no mob resolves to null and the page draws no trash UI.
  function load(url) {
    if (cache) return Promise.resolve(cache);
    if (loading) return loading;
    if (typeof fetch !== "function") return Promise.resolve(null);
    loading = fetch(url || "/data/trash_abilities.json", { credentials: "same-origin" })
      .then((r) => (r && r.ok ? r.json() : null))
      .then((d) => { const t = normalize(d); cache = t.mobs.length ? t : null; return cache; })
      .catch(() => { cache = null; return null; })
      .then((v) => { loading = null; return v; });
    return loading;
  }
  function data() { return cache; }

  // The mob of one creature entry (a string from JSON matches as a number, like the rest of the page).
  function mobFor(entry) {
    if (!cache) return null;
    const e = num(entry);
    return e === null ? null : cache.byEntry[e] || null;
  }

  function abilityFor(entry, sid) {
    const m = mobFor(entry);
    if (!m) return null;
    const s = num(sid);
    return s === null ? null : m.abilities.find((a) => a.sid === s) || null;
  }

  // What the page writes before a cast: the core can kick it, or it is an area effect, or nothing.
  function castWord(entry, sid) {
    const a = abilityFor(entry, sid);
    if (!a) return "";
    if (a.int) return "KICK";
    if (a.aoe || a.ground) return "AoE";
    return "";
  }

  function danger(entry) {
    const m = mobFor(entry);
    return m ? m.danger : 0;
  }

  // "4× Mad Servant: kill first · kick Fireball · caster, summoner". count is the live rows of that entry; with no hint at
  // all the head keeps no trailing ": ".
  function line(mob, count) {
    if (!mob) return "";
    const head = (count == null ? "" : count + "× ") + mob.name;
    const parts = [];
    if (mob.kill === 3) parts.push("kill first");
    else if (mob.kill === 2) parts.push("kill early");
    const kick = (mob.abilities || []).find((a) => a.int);
    if (kick) parts.push("kick " + kick.name);
    if (mob.roles && mob.roles.length) parts.push(mob.roles.join(", "));
    return parts.length ? head + ": " + parts.join(" · ") : head;
  }

  // The block the Live card shows: the dangerous rows only (danger >= 2), most dangerous first, at most five lines. A row
  // whose entry the table does not know is ignored (an add, or a newer mob the build has not written yet).
  function block(mobs) {
    if (!cache) return [];
    const counts = {};
    arr(mobs).forEach((m) => {
      if (!m || m.entry == null) return;
      const e = num(m.entry);
      if (e === null || !cache.byEntry[e]) return;
      counts[e] = (counts[e] || 0) + 1;
    });
    return Object.keys(counts).map((k) => cache.byEntry[Number(k)])
      .filter((m) => m.danger >= 2)
      .sort((a, b) => (b.danger - a.danger) || (counts[b.entry] - counts[a.entry]) || (a.entry - b.entry))
      .slice(0, 5)
      .map((m) => ({ entry: m.entry, name: m.name, count: counts[m.entry], danger: m.danger, text: line(m, counts[m.entry]) }));
  }

  const api = { load, data, normalize, for: mobFor, ability: abilityFor, castWord, danger, line, block };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.TrashInfo = api;
})(typeof self !== "undefined" ? self : this);
