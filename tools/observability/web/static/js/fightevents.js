"use strict";
// FightEvents: one normalised, time-ordered event list per fight for the Fight debugger's Events panel. It reads the fight object
// every panel reads (a filed fight's detail or the synthetic one FdLive.fight draws for a running fight): the log (casts, taunts,
// interrupts, deaths), the aura rows, the boss's victim rows and each death's last hits. A running fight has no victim rows and no
// last hits yet, so it has no `target` and `hit` events; the list is the same shape either way. No DOM, no fetch, no timers:
// unit-tested by internal/commandersjs/fightevents_check.js under node.
(function (root) {
  const MAX_EVENTS = 5000; // events kept per fight (a filed fight keeps the first ones, a running fight the newest)

  const KINDS = [
    { k: "cast", label: "Cast" },
    { k: "taunt", label: "Taunt" },
    { k: "interrupt", label: "Interrupt" },
    { k: "death", label: "Death" },
    { k: "aura_on", label: "Aura applied" },
    { k: "aura_off", label: "Aura removed" },
    { k: "target", label: "Target change" },
    { k: "hit", label: "Hit before death" },
  ];

  // The filter fields (fdfilter.js) and the event property each reads.
  const FIELD_PROP = { kind: "k", ability: "ab", source: "src", target: "tgt", srcRole: "srcRole", tgtRole: "tgtRole", amount: "amt" };

  const isNum = (v) => typeof v === "number" && isFinite(v);
  const str = (v) => (typeof v === "string" ? v : v == null || typeof v === "object" ? "" : String(v));
  const rowOk = (r, n) => Array.isArray(r) && r.length >= n;

  function auraNameOf(sid, names) {
    const A = typeof FightAuras !== "undefined" ? FightAuras : root.FightAuras || (typeof require === "function" ? require("./fightauras.js") : null);
    return A && A.auraName ? A.auraName(sid, names) : "spell " + sid;
  }

  // opts: { names } the aura names the live view learned from the bots' debuff lists.
  function build(fight, opts) {
    const f = fight && typeof fight === "object" ? fight : {};
    const names = (opts && opts.names) || null;
    const bots = Array.isArray(f.bots) ? f.bots.filter((b) => b && typeof b === "object") : [];
    const byGuid = new Map();
    const byName = new Map();
    bots.forEach((b) => {
      if (b.guid != null) byGuid.set(String(b.guid), b);
      if (typeof b.name === "string" && b.name && !byName.has(b.name)) byName.set(b.name, b);
    });
    const roleOf = (g, name) => {
      const b = (g != null && byGuid.get(String(g))) || (name && byName.get(name)) || null;
      return b && typeof b.role === "string" ? b.role : "";
    };
    const botName = (g) => {
      const b = g != null ? byGuid.get(String(g)) : null;
      return b && typeof b.name === "string" ? b.name : "";
    };
    const out = [];
    const push = (e) => out.push(e);
    const mk = (t, k, o) => Object.assign({ t, k, src: "", srcG: null, srcRole: "", tgt: "", tgtG: null, tgtRole: "", ab: "", sid: null, amt: null }, o);

    (Array.isArray(f.log) ? f.log : []).forEach((e) => {
      if (!e || typeof e !== "object" || !isNum(e.t)) return;
      if (e.k === "cast" || e.k === "ab") { // "ab" is a boss ability cast the timeline's Boss lane also draws
        // The target is a bot only when its guid is one of the fight's bots; the name is shown either way.
        const tb = e.tg != null && byGuid.has(String(e.tg)) ? byGuid.get(String(e.tg)) : null;
        push(mk(e.t, "cast", {
          src: str(e.n), srcRole: "enemy", ab: str(e.s), sid: isNum(e.sid) ? e.sid : null,
          tgt: str(e.tn), tgtG: tb ? tb.guid : null, tgtRole: tb ? str(tb.role) : roleOf(null, str(e.tn)),
        }));
      } else if (e.k === "taunt" || e.k === "interrupt") {
        push(mk(e.t, e.k, { src: str(e.n), srcG: e.g != null ? e.g : null, srcRole: roleOf(e.g, str(e.n)), ab: str(e.s), tgt: str(e.tn), tgtRole: roleOf(null, str(e.tn)) }));
      } else if (e.k === "death") {
        push(mk(e.t, "death", { src: str(e.by), srcRole: roleOf(null, str(e.by)), tgt: str(e.n), tgtG: e.g != null ? e.g : null, tgtRole: roleOf(e.g, str(e.n)) }));
      }
    });

    (Array.isArray(f.auras) ? f.auras : []).forEach((r) => {
      if (!rowOk(r, 4) || !isNum(r[0]) || !isNum(r[1]) || !isNum(r[2]) || (r[3] !== 1 && r[3] !== -1)) return;
      const nm = botName(r[1]);
      push(mk(r[0], r[3] === 1 ? "aura_on" : "aura_off", { tgt: nm || "#" + r[1], tgtG: r[1], tgtRole: roleOf(r[1], nm), ab: auraNameOf(r[2], names), sid: r[2] }));
    });

    const boss = str(f.boss_name) || "boss";
    (Array.isArray(f.victims) ? f.victims : []).forEach((r) => {
      if (!rowOk(r, 4) || !isNum(r[0])) return;
      const role = str(r[3]);
      push(mk(r[0], "target", { src: boss, srcRole: "enemy", tgt: str(r[2]), tgtG: r[1] != null && typeof r[1] !== "object" ? r[1] : null, tgtRole: role === "none" ? "" : role }));
    });

    (Array.isArray(f.deaths) ? f.deaths : []).forEach((d) => {
      if (!d || typeof d !== "object" || !Array.isArray(d.hits)) return;
      const nm = str(d.name);
      const g = d.guid != null ? d.guid : (byName.get(nm) || {}).guid;
      d.hits.forEach((h) => {
        if (!h || typeof h !== "object" || !isNum(h.t_ms) || !isNum(h.d)) return;
        push(mk(h.t_ms, "hit", { src: str(h.a), srcRole: roleOf(null, str(h.a)), tgt: nm, tgtG: g != null ? g : null, tgtRole: roleOf(g, nm), ab: str(h.s), amt: h.d }));
      });
    });

    // Stable: equal times keep the order the sources were read in.
    const order = out.map((e, i) => i);
    order.sort((a, b) => out[a].t - out[b].t || a - b);
    let events = order.map((i) => out[i]);
    const cut = events.length > MAX_EVENTS;
    if (cut) events = f.live ? events.slice(events.length - MAX_EVENTS) : events.slice(0, MAX_EVENTS);
    return { events, cut };
  }

  // Sorted unique non-empty values of a filter field (or event property): what the value pickers offer.
  function distinct(events, field) {
    const prop = Object.prototype.hasOwnProperty.call(FIELD_PROP, field) ? FIELD_PROP[field] : field;
    const seen = new Set();
    (Array.isArray(events) ? events : []).forEach((e) => {
      const v = e ? e[prop] : null;
      if (typeof v === "string" && v) seen.add(v);
    });
    return Array.from(seen).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  }

  const api = { MAX_EVENTS, KINDS, FIELD_PROP, build, distinct };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FightEvents = api;
})(typeof self !== "undefined" ? self : this);
