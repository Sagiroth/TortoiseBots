"use strict";
// Commanders tab: the plain-language and map-view logic, free of the page so a test can run it under node
// (internal/commandersjs). Every function here is pure except the tracker, which only keeps what it is handed.
// app.js turns these results into HTML; nothing here builds markup.
(function (root) {
  const FAR_YD = 40;          // a bot this far from the lead is flagged
  const LOW_HP_PCT = 30;      // a live bot under this is flagged
  const STALE_WARN_SEC = 30;  // no update for this long is flagged
  const AGGRO_RANGE_YD = 20;  // the ring drawn around a mob nobody fights yet (a rough guide: the real radius depends on levels)
  const HEAL_RANGE_YD = 40;   // a healer's spells reach this far (the ring of the healer-range layer)
  const TRAIL_MAX = 240;      // positions of the lead kept for its trail (the trail-length slider shows up to this many)
  const TRAIL_STEP_YD = 2;    // a new trail point after this much movement
  // Trail breaks: two reports of a bot are joined by a line only while the distance is something a unit can walk or ride in the time
  // between them (same numbers as the daemon's JumpLimitYd, internal/state/commanders.go): 12 yd of slack for rounding and late reports
  // plus 20 yd for each second, the seconds counted from 1 to 30. 20 yd/s is above the fastest ground mount of the 1.12 game (about
  // 14 yd/s with speed buffs); at the 2 s reports that is a break past 52 yd. The gap is capped so a long silence cannot hide a jump.
  const TRAVEL_SLACK_YD = 12;
  const TRAVEL_MAX_YD_PER_SEC = 20;
  const TRAVEL_MAX_GAP_SEC = 30;
  const FIGHT_WINDOW_SEC = 8; // an HP drop this recent counts as a fight
  const MOVE_WINDOW_SEC = 6;  // lead movement this recent counts as walking

  const cap = (s) => (s ? s[0].toUpperCase() + s.slice(1) : "");
  const lowerFirst = (s) => (s ? s[0].toLowerCase() + s.slice(1) : "");
  const pretty = (s) => cap(String(s || "").replace(/[_\-]+/g, " ").trim());
  const plural = (n, one, many) => n + " " + (n === 1 ? one : many || one + "s");
  const pct = (a, b) => (b > 0 ? Math.max(0, Math.min(100, (a / b) * 100)) : 0);

  // ---- party -------------------------------------------------------------------------------------------------------

  // tank | healer | dps | other, and dead for a bot that is down (the colour then is grey).
  function roleKey(b) {
    if (!b || b.alive === false) return "dead";
    return b.role === "tank" || b.role === "healer" || b.role === "dps" ? b.role : "other";
  }
  function roleLabel(b) {
    const r = b && b.role;
    return r === "dps" ? "DPS" : r ? cap(r) : "Bot";
  }

  // Yards between two bots (3D), or null when they are on different maps or either has no position.
  function distance(a, b) {
    if (!a || !b || a.map !== b.map) return null;
    const d = Math.hypot((a.x || 0) - (b.x || 0), (a.y || 0) - (b.y || 0), (a.z || 0) - (b.z || 0));
    return isFinite(d) ? d : null;
  }

  function leadBot(bind) {
    const bots = (bind && bind.bots) || [];
    return bots.find((b) => b.lead) || bots.find((b) => b.name === bind.lead) || null;
  }

  function powerKind(b) {
    return b.power_type === "rage" || b.power_type === "energy" ? b.power_type : "mana";
  }

  // The party as table rows: lead first, then tank, healer, damage, others.
  function partyRows(bind) {
    const lead = leadBot(bind);
    const order = { tank: 0, healer: 1, dps: 2 };
    const rows = ((bind && bind.bots) || []).map((b) => {
      const dist = b === lead ? null : distance(b, lead);
      return {
        name: b.name, guid: b.guid || 0, lead: b === lead, alive: b.alive !== false,
        role: roleKey(b), roleLabel: roleLabel(b), cls: pretty(b.class), level: b.level || 0,
        hp: Math.round(pct(b.hp, b.max_hp)), hasPower: b.max_power > 0, power: Math.round(pct(b.power, b.max_power)),
        powerKind: powerKind(b), hpText: b.hp + " / " + b.max_hp, powerText: b.power + " / " + b.max_power,
        dist: dist == null ? null : Math.round(dist),
        otherMap: !!lead && b !== lead && b.map !== lead.map,
        far: dist != null && dist > FAR_YD,
        map: b.map, zone: b.zone,
        _o: order[b.role] == null ? 3 : order[b.role],
      };
    });
    rows.sort((a, b) => (a.lead ? -1 : 0) - (b.lead ? -1 : 0) || a._o - b._o || (a.name < b.name ? -1 : 1));
    rows.forEach((r) => delete r._o);
    return rows;
  }
  // ---- the raid card (A11) --------------------------------------------------------------------------------------------

  // A binding with more than 6 bots (or any bot with a subgroup) is a raid: the card shows subgroup blocks.
  function isRaid(bind) {
    const bots = (bind && bind.bots) || [];
    return bots.length > 6 || bots.some((b) => b && typeof b.sub === "number");
  }

  // The party as subgroup blocks: [{subgroup: 0..7, label, rows}] with the lead first in its block. A binding whose bots
  // carry no subgroup is one unlabelled block ("Party") in the party order.
  function raidBlocks(bind) {
    const rows = partyRows(bind);
    const byBot = {};
    ((bind && bind.bots) || []).forEach((b) => { byBot[b.guid] = typeof b.sub === "number" ? b.sub : -1; });
    if (!rows.some((r) => byBot[r.guid] >= 0)) return [{ subgroup: -1, label: "Party", rows }];
    const subs = {};
    rows.forEach((r) => {
      const s = byBot[r.guid];
      const sub = s >= 0 && s <= 7 ? s : 7;
      (subs[sub] || (subs[sub] = [])).push(r);
    });
    return Object.keys(subs).map(Number).sort((a, b) => a - b).map((s) => {
      const rs = subs[s];
      rs.sort((a, b) => (a.lead ? -1 : 0) - (b.lead ? -1 : 0));
      return { subgroup: s, label: "Group " + (s + 1), rows: rs };
    });
  }

  // The bound raid's layout (the module's TBM:RAID_LAYOUT), in words: "MT <name> · OTs <a>, <b> · tank healers <h> -> <t> ·
  // raid healers <n>"; null when the module reported none (an older one) or every part is empty. The card shows it as one row.
  function layoutText(bind) {
    const l = bind && bind.layout;
    if (!l || typeof l !== "object") return null;
    const names = (v) => (Array.isArray(v) ? v.filter((n) => typeof n === "string" && n) : []);
    const pairs = (Array.isArray(l.th) ? l.th : [])
      .filter((p) => p && typeof p === "object" && p.h && p.t)
      .map((p) => p.h + " -> " + p.t);
    const parts = [];
    if (typeof l.mt === "string" && l.mt) parts.push("MT " + l.mt);
    const off = names(l.ot);
    if (off.length) parts.push("OTs " + off.join(", "));
    if (pairs.length) parts.push("tank healers " + pairs.join(", "));
    const healers = names(l.rh);
    if (healers.length) parts.push("raid healers " + healers.join(", "));
    return parts.length ? parts.join(" · ") : null;
  }

  // Mobs whose victim is a bound bot that is not a tank (by role), held at least 2 s since the tracker first saw the
  // victim on that mob: [{mob, victim, role, seconds}]. A mob with no victim or on a tank is not listed.
  function threatOffTanks(bind, tr, nowSec) {
    const out = [];
    const bots = {};
    ((bind && bind.bots) || []).forEach((b) => { if (b && b.id) bots[b.id] = b; });
    const scoped = (tr.threat || (tr.threat = {}));
    const seen = scoped[bind.commander] || (scoped[bind.commander] = {});
    ((bind && bind.mobs) || []).forEach((m) => {
      const v = m && m.victim && bots[m.victim];
      if (!v || v.role === "tank") { if (m && m.id) delete seen[m.id]; return; }
      const key = m.id || (m.name + "@" + (m.x || 0) + "," + (m.y || 0));
      if (seen[key] == null) seen[key] = nowSec;
      const seconds = Math.max(0, Math.round((nowSec || 0) - seen[key]));
      if (seconds >= 2) out.push({ mob: m, victim: v.name, role: v.role || "other", seconds });
    });
    return out;
  }

  // The run's status lines (oldest first on the wire) newest first: [{at, text: "<step> <leg> <kind> <phase>: <note>", state}].
  function notesLog(bind) {
    const notes = bind && bind.run && Array.isArray(bind.run.notes) ? bind.run.notes : [];
    return notes.slice().reverse().map((n) => ({
      at: n.at,
      text: [n.step, n.leg, n.kind, n.phase].join(" ") + ": " + n.note,
      state: n.state,
    }));
  }

  // Deaths of the last fights (the A12 combat data) plus the bots now dead first: [{t, name, killer, fight, x, y}].
  function deathsTimeline(fights, bind) {
    const dead = ((bind && bind.bots) || []).filter((b) => b.alive === false);
    const out = dead.map((b) => ({ t: null, name: b.name, killer: "", fight: "", x: b.x || 0, y: b.y || 0 }));
    (Array.isArray(fights) ? fights : []).forEach((f) => {
      (f.deaths || []).forEach((d) => {
        out.push({
          t: f.start != null && d.t_ms != null ? Math.round(f.start + d.t_ms / 1000) : null,
          name: d.name, killer: d.killer || "", fight: f.boss_name || "", x: d.x || 0, y: d.y || 0,
        });
      });
    });
    return out;
  }

  // The buttons' GM lines for this binding. startFromLeg takes the plan's leg name or step number; reviveDead lists one
  // .revive line per dead bound bot; gearPreset gears the raid (a raid binding) or the commander's bot (a party).
  function controlLines(bind) {
    const name = bind && bind.commander;
    const raid = isRaid(bind);
    const dead = ((bind && bind.bots) || []).filter((b) => b.alive === false).map((b) => b.name);
    return {
      pause: ".bot run for " + name + " pause",
      resume: ".bot run for " + name + " resume",
      skip: ".bot run for " + name + " skip",
      stop: ".bot run for " + name + " stop",
      status: ".bot run for " + name + " status",
      // a bare number is a step (`from 4` alone would be read as leg 4), anything else a leg name
      startFromLeg: (leg, untilBoss) => ".bot run for " + name + " start from " + (/^\d+$/.test(String(leg).trim()) ? "step " + String(leg).trim() : "leg " + String(leg).trim()) + (untilBoss ? " until boss" : ""),
      // the trash walk that stops at the boss stand, from the plan's first step
      startUntilBoss: ".bot run for " + name + " start until boss",
      reviveDead: dead.map((n) => ".revive " + n),
      reloadPlans: ".bot run reloadplans",
      gearPreset: (preset) => raid ? ".bot gear raid " + name + " preset " + preset : ".bot gear " + name + " preset " + preset,
    };
  }

  // One sentence: the run's state and the seconds since it changed; "stalled" past 120 s, "stuck" past 300 s.
  function whyWaiting(bind, nowSec) {
    const r = bind && bind.run;
    if (!r || typeof r !== "object") return "";
    const ago = r.changed != null && nowSec ? Math.max(0, Math.round(nowSec - r.changed)) : 0;
    const flag = ago > 300 ? "stuck" : ago > 120 ? "stalled" : "";
    return [r.state, r.note, ageText(ago) + " ago", flag].filter(Boolean).join(" \u00b7 ");
  }


  // The dashboard's own link to a character's detail view (the route app.js parses: #/<tab>?bot=<guid>), or '' for a guid that is not one.
  // The Commanders tab stays the tab underneath, as it does for a bot picked anywhere else.
  function botHref(guid) {
    if (typeof guid === "string" && !/^[0-9]{1,10}$/.test(guid)) return "";
    const n = Number(guid);
    return Number.isInteger(n) && n > 0 && n <= 4294967295 ? "#/commanders?bot=" + n : "";
  }

  // The first part of a card's header with the commander linkable: [commander name, rest]; the rest begins with the arrow.
  function headerParts(bind, place, state) {
    const text = headerText(bind, place, state), name = String((bind && bind.commander) || "");
    return text.indexOf(name) === 0 ? [name, text.slice(name.length)] : ["", text];
  }

  // ---- orders ------------------------------------------------------------------------------------------------------

  const VERBS = {
    attack: "Attack", move: "Move", follow: "Follow", stay: "Stay", rest: "Rest", pull: "Pull", focus: "Focus", cc: "Crowd control",
    loot: "Loot", lootmethod: "Loot method", open: "Open", use: "Use", enter: "Enter", summon: "Summon", revive: "Revive",
    ritual: "Ritual", state: "Check",
  };
  const cut = (s, n) => (s.length > n ? s.slice(0, n - 1) + "…" : s);

  // What the order was aimed at, in words: a creature or object by id, a point, or the arguments as typed.
  function orderTarget(verb, detail) {
    const d = String(detail || "").trim();
    if (!d) return "";
    const entry = /\bentry:(\d+)/i.exec(d);
    if (entry) return "creature " + entry[1];
    if (/0x[0-9a-f]+/i.test(d)) return "a target";
    const pt = /^(-?\d+(?:\.\d+)?)[ ,]+(-?\d+(?:\.\d+)?)(?:[ ,]+(-?\d+(?:\.\d+)?))?$/.exec(d);
    if (pt) return "to " + Math.round(parseFloat(pt[1])) + ", " + Math.round(parseFloat(pt[2]));
    return cut(d, 28);
  }

  // The module's answer in words. ACK carries "<scope> <count>" (party 5, bot:Name 1); ERR carries "<code>: <message>".
  function orderResult(o) {
    const r = String(o.result || "").trim();
    if (o.ok) {
      const m = /^(\S+) (\d+)$/.exec(r);
      if (m) {
        const n = parseInt(m[2], 10);
        if (m[1].indexOf("bot:") === 0) return "OK (" + m[1].slice(4) + ")";
        return "OK (" + plural(n, "bot") + ")";
      }
      return r ? "OK (" + cut(r, 40) + ")" : "OK";
    }
    const m = /^([\w-]+):\s*(.*)$/.exec(r);
    const code = m ? m[1] : "";
    let msg = m ? m[2] : r;
    msg = msg.replace(/[.\s]+$/, "");
    if (!msg) msg = code.replace(/[_\-]+/g, " ") || "no reason given";
    return "refused: " + lowerFirst(cut(msg, 80));
  }

  // "Attack a target → OK (5 bots)", "Move → refused: no walkable path". raw is the whole line for a tooltip.
  function orderText(o) {
    const verb = String(o.verb || "").toLowerCase();
    const label = VERBS[verb] || pretty(verb) || "Order";
    const target = orderTarget(verb, o.detail);
    const result = orderResult(o);
    const raw = (o.ok ? "ACK" : "ERR") + " cmd " + verb + (o.detail ? " " + o.detail : "") + " -> " + (o.result || "");
    return { verb, label, target, result, ok: !!o.ok, text: label + (target ? " " + target : "") + " → " + result, raw, at: o.at || 0 };
  }

  // ---- state of the party -----------------------------------------------------------------------------------------

  const FIGHT_VERBS = { attack: 1, pull: 1, focus: 1, cc: 1 };
  const QUIET_VERBS = { state: 1, lootmethod: 1 }; // asks, changes nothing
  const IDLE_VERB_WORD = {
    follow: "following", stay: "holding position", move: "holding", rest: "resting", loot: "looting", open: "opening something",
    use: "using an object", ritual: "at a ritual", enter: "entering", summon: "summoning", revive: "reviving",
  };

  // The last order that changed something and was accepted (newest last in the module's list).
  function lastMeaningful(orders) {
    for (let i = (orders || []).length - 1; i >= 0; i--) {
      const o = orders[i];
      if (o.ok && !QUIET_VERBS[String(o.verb || "").toLowerCase()]) return o;
    }
    return null;
  }

  // A word for what the party is doing: fighting, moving, resting, ...  ctx = {nowSec, hpDropAt, leadMovedAt} (unix seconds, 0 unknown).
  function stateWord(bind, ctx) {
    const c = ctx || {}, now = c.nowSec || 0;
    const bots = (bind && bind.bots) || [];
    if (!bots.length) return "no bots reporting";
    if (bots.every((b) => b.alive === false)) return "wiped";
    if (bind.run && bind.run.state === "paused") return "run paused";
    if (bots.some((b) => b.alive !== false && b.st === "c")) return "fighting"; // a module that reports the AI state says so itself
    if (c.hpDropAt && now - c.hpDropAt <= FIGHT_WINDOW_SEC) return "fighting";
    const last = lastMeaningful(bind.orders);
    const verb = last ? String(last.verb || "").toLowerCase() : "";
    const age = last && now ? now - last.at : Infinity;
    if (FIGHT_VERBS[verb] && age <= 15) return verb === "pull" ? "pulling" : "attacking";
    if (c.leadMovedAt && now - c.leadMovedAt <= MOVE_WINDOW_SEC) return "moving";
    if (last && age <= 180 && IDLE_VERB_WORD[verb]) return IDLE_VERB_WORD[verb];
    return "idle";
  }

  function headerText(bind, place, state) {
    const n = ((bind && bind.bots) || []).length;
    const parts = [bind.commander + " → " + plural(n, "bot")];
    if (place) parts.push(place);
    if (state) parts.push(state);
    return parts.join(" · ");
  }

  // ---- warnings ----------------------------------------------------------------------------------------------------

  // The few things worth a glance: [{level: "bad" | "warn", kind, text}]. opts = {ageSec}: seconds since the game server last reported.
  function warnings(bind, opts) {
    const o = opts || {}, out = [];
    const bots = (bind && bind.bots) || [];
    const lead = leadBot(bind);
    const down = bots.filter((b) => b.alive === false);
    if (down.length) out.push({ level: "bad", kind: "dead", text: down.length + " down: " + down.map((b) => b.name).join(", ") });
    const low = bots.filter((b) => b.alive !== false && b.max_hp > 0 && pct(b.hp, b.max_hp) < LOW_HP_PCT);
    if (low.length) out.push({ level: "warn", kind: "lowhp", text: "Low health: " + low.map((b) => b.name + " " + Math.round(pct(b.hp, b.max_hp)) + "%").join(", ") });
    if (bind.lead && !lead) out.push({ level: "warn", kind: "nolead", text: "Lead " + bind.lead + " is not reporting" });
    const far = [], away = [];
    if (lead) {
      bots.forEach((b) => {
        if (b === lead) return;
        const d = distance(b, lead);
        if (d == null) away.push(b.name);
        else if (d > FAR_YD) far.push({ name: b.name, d: Math.round(d) });
      });
    }
    if (away.length) out.push({ level: "warn", kind: "othermap", text: away.join(", ") + (away.length === 1 ? " is" : " are") + " on another map than the lead" });
    if (far.length) {
      far.sort((a, b) => b.d - a.d);
      const shown = far.slice(0, 2).map((f) => f.name + " " + f.d + " yd");
      out.push({ level: "warn", kind: "far", text: "Far from the lead (over " + FAR_YD + " yd): " + shown.join(", ") + (far.length > 2 ? " and " + (far.length - 2) + " more" : "") });
    }
    const orders = (bind && bind.orders) || [];
    const newest = orders[orders.length - 1];
    if (newest && !newest.ok) {
      const t = orderText(newest);
      out.push({ level: "warn", kind: "refused", text: "Last order refused: " + t.label + (t.target ? " " + t.target : "") + " (" + t.result.replace(/^refused: /, "") + ")" });
    }
    if (bind && bind.run && bind.run.state === "paused") {
      const note = String(bind.run.note || "").replace(/^-$/, "").trim();
      out.push({ level: "warn", kind: "runpaused", text: "Run paused: " + (note || "no reason given") });
    }
    if (bind && bind.online === false) out.push({ level: "warn", kind: "offline", text: "Commander " + bind.commander + " is offline" });
    if (o.ageSec != null && o.ageSec > STALE_WARN_SEC) {
      out.push({ level: "bad", kind: "stale", text: "No update for " + ageText(o.ageSec) + ": the game server may be down" });
    }
    return out;
  }

  function ageText(sec) {
    sec = Math.max(0, Math.round(sec || 0));
    if (sec < 60) return sec + " s";
    const m = Math.floor(sec / 60);
    return m < 60 ? m + " min " + String(sec % 60).padStart(2, "0") + " s" : Math.floor(m / 60) + " h " + String(m % 60).padStart(2, "0") + " min";
  }

  // ---- what the browser remembers between updates ---------------------------------------------------------------

  // The module reports positions and HP only; "fighting" and "moving" come from how they change, and the lead's trail from where it
  // has been. Keyed by commander. Feed it each fresh report once.
  // ---- breaks in a trail ------------------------------------------------------------------------------------------

  // A trail point is [x, y, t] or, where something happened, [x, y, t, kind]: "d" the bot died here (the line goes on), "t" it
  // teleported (a jump no walking explains: summon, portal), "r" the same jump while it was dead before or after (spirit release,
  // resurrection at the spirit healer), "m" it arrived on another map. A point of kind t, r or m starts a new segment.
  // "g" is a gap with no icon: reports were lost (a very long pause), so the path is not joined across them.
  const BREAK_KINDS = { t: 1, r: 1, m: 1, g: 1 };
  const POINT_KINDS = { d: 1, t: 1, r: 1, m: 1, g: 1 };

  function jumpLimitYd(gapSec) {
    const g = Math.min(TRAVEL_MAX_GAP_SEC, Math.max(1, num(gapSec) ? gapSec : 1));
    return TRAVEL_SLACK_YD + TRAVEL_MAX_YD_PER_SEC * g;
  }

  // What happened between two reports of one bot, {map, x, y, t, alive}: "" nothing, "m" map change, "t" / "r" a jump, "d" died.
  function trailEvent(prev, cur) {
    if (!prev || !cur) return "";
    if (prev.gap) return "g";
    if (prev.map !== cur.map) return "m";
    const jump = Math.hypot(prev.x - cur.x, prev.y - cur.y) > jumpLimitYd(cur.t - prev.t);
    if (jump) return prev.alive === false || cur.alive === false ? "r" : "t";
    if (prev.alive !== false && cur.alive === false) return "d";
    return "";
  }

  // The kind of a point of a trail, or "" (anything else, also from the daemon, is dropped).
  const pointKind = (p) => (Array.isArray(p) && typeof p[3] === "string" && POINT_KINDS[p[3]] ? p[3] : "");

  // Split a trail (points oldest first) into the stretches that really were walked: [[points], ...], none empty.
  function trailRuns(pts) {
    const runs = [];
    (pts || []).forEach((p) => {
      if (!runs.length || BREAK_KINDS[pointKind(p)]) runs.push([]);
      runs[runs.length - 1].push(p);
    });
    return runs;
  }

  // The marks to draw on a trail: [{x, y, t, kind}] for each point that carries a kind (a death, or where a break ends).
  function trailMarks(pts) {
    const out = [];
    (pts || []).forEach((p) => { const kind = pointKind(p); if (kind && num(p[0]) && num(p[1])) out.push({ x: p[0], y: p[1], t: p[2], kind }); });
    return out;
  }

  // A very long pause lost reports: the next point of every trail starts a new piece, marked by nothing (see RefreshRate onGap).
  function markGap(tr) { Object.keys(tr.trail).forEach((k) => { tr.trail[k].seen = { gap: true }; }); }

  function newTracker() { return { hp: {}, hpDropAt: {}, leadMovedAt: {}, trail: {} }; }

  function note(tr, bind, nowSec) {
    const key = bind.commander;
    (bind.bots || []).forEach((b) => {
      const k = key + ":" + b.guid;
      const prev = tr.hp[k];
      if (b.alive !== false && prev != null && b.hp < prev) tr.hpDropAt[key] = nowSec;
      tr.hp[k] = b.alive === false ? null : b.hp;
    });
    const lead = leadBot(bind);
    if (!lead) return;
    const cur = { map: lead.map, x: lead.x, y: lead.y, t: nowSec, alive: lead.alive !== false };
    let t = tr.trail[key], ev = "";
    if (!t) t = tr.trail[key] = { map: lead.map, pts: [], seen: null };
    else if (t.map !== lead.map) { t = tr.trail[key] = { map: lead.map, pts: [], seen: null }; ev = "m"; }
    else ev = trailEvent(t.seen, cur);
    t.seen = cur;
    const last = t.pts[t.pts.length - 1];
    if (ev || !last || Math.hypot(last[0] - lead.x, last[1] - lead.y) >= TRAIL_STEP_YD) {
      if (last) tr.leadMovedAt[key] = nowSec;
      t.pts.push(ev ? [lead.x, lead.y, nowSec, ev] : [lead.x, lead.y, nowSec]);
      if (t.pts.length > TRAIL_MAX) t.pts.shift();
    }
  }

  // Older points of the lead (from the daemon's short history, oldest first, [x, y, t]) put in front of the trail this page has seen: only
  // those older than its first point, and only on the map the trail is on. Returns how many were taken.
  function prependTrail(tr, commander, mapId, pts) {
    const t = tr.trail[commander];
    if (!t || t.map !== mapId || !Array.isArray(pts) || !pts.length) return 0;
    const first = t.pts.length ? t.pts[0][2] : Infinity;
    const older = pts.filter((p) => Array.isArray(p) && num(p[0]) && num(p[1]) && num(p[2]) && p[2] < first).map((p) => (pointKind(p) ? [p[0], p[1], p[2], p[3]] : [p[0], p[1], p[2]]));
    if (!older.length) return 0;
    t.pts = older.concat(t.pts).slice(-TRAIL_MAX);
    return older.length;
  }

  function context(tr, bind, nowSec) {
    return { nowSec, hpDropAt: tr.hpDropAt[bind.commander] || 0, leadMovedAt: tr.leadMovedAt[bind.commander] || 0 };
  }

  // Forget the commanders that are no longer listed.
  function prune(tr, commanders) {
    const live = {};
    commanders.forEach((c) => { live[c] = true; });
    ["hpDropAt", "leadMovedAt", "trail"].forEach((f) => Object.keys(tr[f]).forEach((k) => { if (!live[k]) delete tr[f][k]; }));
    Object.keys(tr.hp).forEach((k) => { if (!live[k.slice(0, k.lastIndexOf(":"))]) delete tr.hp[k]; });
  }

  // ---- the map ---------------------------------------------------------------------------------------------------
  // The drawing plane: north up, east right, one unit a yard. A game position (x north, y west) lands at (-y, -x).
  // A view is {cx, cy, scale}: the plane point in the middle of the panel and the pixels per yard.

  const project = (x, y) => ({ x: -y, y: -x });
  const MIN_SCALE = 0.04, MAX_SCALE = 14;
  const clampScale = (s) => Math.min(Math.max(s, MIN_SCALE), MAX_SCALE);

  // The view that shows all points (plane points {x, y}) in a pw x ph panel with padPx around them; never closer than minSpan yards.
  function fitView(pts, pw, ph, padPx, minSpan) {
    if (!pts.length) return { cx: 0, cy: 0, scale: 1 };
    let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
    pts.forEach((p) => { x0 = Math.min(x0, p.x); x1 = Math.max(x1, p.x); y0 = Math.min(y0, p.y); y1 = Math.max(y1, p.y); });
    const pad = padPx == null ? 36 : padPx, span = minSpan == null ? 60 : minSpan;
    const aw = Math.max(1, pw - 2 * pad), ah = Math.max(1, ph - 2 * pad);
    const scale = clampScale(Math.min(aw / Math.max(x1 - x0, span), ah / Math.max(y1 - y0, span)));
    return { cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, scale };
  }

  // [x, y, w, h] of the plane the panel shows.
  function viewBox(v, pw, ph) { const w = pw / v.scale, h = ph / v.scale; return [v.cx - w / 2, v.cy - h / 2, w, h]; }

  // Zoom by factor keeping the plane point under panel pixel (px, py) where it is.
  function zoomAt(v, factor, pw, ph, px, py) {
    const s = clampScale(v.scale * factor);
    const wx = v.cx + (px - pw / 2) / v.scale, wy = v.cy + (py - ph / 2) / v.scale;
    return { cx: wx - (px - pw / 2) / s, cy: wy - (py - ph / 2) / s, scale: s };
  }

  function panBy(v, dx, dy) { return { cx: v.cx - dx / v.scale, cy: v.cy - dy / v.scale, scale: v.scale }; }

  // Two fingers moved from (a0, b0) to (a1, b1), each {x, y} in panel pixels: zoom by their distance, move with their middle.
  function pinch(v, a0, b0, a1, b1, pw, ph) {
    const d0 = Math.hypot(a0.x - b0.x, a0.y - b0.y), d1 = Math.hypot(a1.x - b1.x, a1.y - b1.y);
    const m0 = { x: (a0.x + b0.x) / 2, y: (a0.y + b0.y) / 2 }, m1 = { x: (a1.x + b1.x) / 2, y: (a1.y + b1.y) / 2 };
    const z = d0 > 1 ? zoomAt(v, d1 / d0, pw, ph, m0.x, m0.y) : v;
    return panBy(z, m1.x - m0.x, m1.y - m0.y);
  }

  // ---- following the party while zooming -------------------------------------------------------------------------
  // Following keeps the party in the middle of the panel. The zoom is the viewer's own once they have set one (wheel, pinch, + / -);
  // until then it is the one that fits the party. Zooming never ends the following (only a drag does), and while following it zooms
  // around the party in the middle, not around the cursor.

  // The view while following: centred on the points (plane), at userScale when the viewer has chosen one, else at the fitting scale.
  function followView(pts, pw, ph, padPx, minSpan, userScale) {
    const fit = fitView(pts, pw, ph, padPx, minSpan);
    if (!pts.length || !(userScale > 0)) return fit;
    return { cx: fit.cx, cy: fit.cy, scale: clampScale(userScale) };
  }

  // One zoom step by factor while following: the middle of the panel (the party) stays where it is.
  function zoomFollow(v, factor) { return { cx: v.cx, cy: v.cy, scale: clampScale(v.scale * factor) }; }

  // Two fingers moved while following: only their distance counts (a pan would be a drag, which pauses the following).
  function pinchFollow(v, a0, b0, a1, b1) {
    const d0 = Math.hypot(a0.x - b0.x, a0.y - b0.y), d1 = Math.hypot(a1.x - b1.x, a1.y - b1.y);
    return d0 > 1 ? zoomFollow(v, d1 / d0) : v;
  }

  // ---- mobs, targets and the commander (data from the module's COMMANDERS report) ---------------------------------------------
  // A mob is {id, entry, name, hp, x, y, victim?, reaction: h|n|f, kind?: k|v|g|u}; ids are decimal strings. Anything malformed is dropped.

  const num = (n) => typeof n === "number" && isFinite(n);

  // The mobs of a binding that can be drawn on mapId: [] when the module reports none or they are on another map.
  function mobsOf(bind, mapId) {
    if (!bind || !Array.isArray(bind.mobs) || bind.mobs_map !== mapId) return [];
    return bind.mobs.filter((m) => m && typeof m.id === "string" && num(m.x) && num(m.y) && (m.reaction === "h" || m.reaction === "n" || m.reaction === "f"));
  }

  // How a mob is drawn: "hostile", "neutral", "critter", or "" (friendly units are not drawn: guards, town folk).
  function mobClass(m) {
    if (!m || m.reaction === "f") return "";
    if (m.kind === "k") return "critter";
    if (m.kind === "v" || m.kind === "g") return "";
    return m.reaction === "h" ? "hostile" : "neutral";
  }

  // Lines from each mob to the bot it targets: [{mob, bot}] (both with game x, y). Only bots on the list count.
  function threatLines(mobs, bots) {
    const byId = {};
    (bots || []).forEach((b) => { if (b.id) byId[b.id] = b; });
    return (mobs || []).filter((m) => m.victim && byId[m.victim] && mobClass(m) !== "").map((m) => ({ mob: m, bot: byId[m.victim] }));
  }

  // Lines from each bot to its target when that is a listed mob: [{bot, mob}].
  function botTargetLines(bots, mobs) {
    const byId = {};
    (mobs || []).forEach((m) => { byId[m.id] = m; });
    return (bots || []).filter((b) => b.alive !== false && b.target && byId[b.target]).map((b) => ({ bot: b, mob: byId[b.target] }));
  }

  // The mobs that get an aggro ring: hostile ones that nobody fights yet, i.e. the next pull. (Critters and neutrals do not aggro.)
  function aggroMobs(mobs) { return (mobs || []).filter((m) => mobClass(m) === "hostile" && !m.victim && num(m.hp) && m.hp > 0); }

  // The commander's own place when it stands on mapId: {x, y} (game coordinates) or null.
  function commanderPlace(bind, mapId) {
    const p = bind && bind.commander_pos;
    return p && p.map === mapId && num(p.x) && num(p.y) ? { x: p.x, y: p.y } : null;
  }

  // Grid spacing in yards so that lines sit about 80 px apart, and the line positions inside a plane box [x, y, w, h].
  function gridStep(scale) {
    const raw = 80 / scale, steps = [5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000];
    return steps.find((s) => s >= raw) || 10000;
  }
  function gridLines(box, step) {
    const xs = [], ys = [];
    for (let x = Math.ceil(box[0] / step) * step; x <= box[0] + box[2]; x += step) xs.push(x);
    for (let y = Math.ceil(box[1] / step) * step; y <= box[1] + box[3]; y += step) ys.push(y);
    return { xs, ys };
  }

  // The trail as chunks that fade with age: up to maxSeg polylines sharing end points, alpha from minAlpha (oldest) to 1.
  function trailSegments(pts, maxSeg, minAlpha) {
    const out = [];
    if (!pts || pts.length < 2) return out;
    const lo = minAlpha == null ? 0.2 : minAlpha, n = pts.length - 1;
    const per = Math.ceil(n / Math.max(1, maxSeg || 12)), count = Math.ceil(n / per);
    for (let i = 0, k = 0; i < n; i += per, k++) {
      out.push({ points: pts.slice(i, Math.min(pts.length, i + per + 1)), alpha: count === 1 ? 1 : lo + (1 - lo) * (k / (count - 1)) });
    }
    return out;
  }

  // The same for a trail with breaks (points as [x, y, t, kind?], game coordinates): each stretch between two breaks is chunked on its own,
  // never joined to the next, and the fade runs over the whole trail. Returns [{points: [{x, y}], alpha}] in the drawing plane.
  function trailSegmentsBroken(pts, maxSeg, minAlpha) {
    const lo = minAlpha == null ? 0.2 : minAlpha;
    const runs = trailRuns(pts).filter((r) => r.length >= 2);
    if (runs.length <= 1) return runs.length ? trailSegments(runs[0].map((p) => project(p[0], p[1])), maxSeg, lo) : [];
    const total = runs.reduce((a, r) => a + r.length - 1, 0);
    const out = [];
    let done = 0;
    runs.forEach((r, ri) => {
      const edges = r.length - 1;
      const proj = r.map((p) => project(p[0], p[1]));
      const per = Math.ceil(edges / Math.max(1, Math.round((maxSeg || 12) * edges / total)));
      for (let i = 0; i < edges; i += per) {
        const newest = ri === runs.length - 1 && i + per >= edges;
        out.push({ points: proj.slice(i, Math.min(proj.length, i + per + 1)), alpha: newest ? 1 : lo + (1 - lo) * Math.min(1, (done + i) / Math.max(1, total - 1)) });
      }
      done += edges;
    });
    return out;
  }

  // The direction the lead is walking, from the trail (positions [x, y, t] in game coordinates, oldest first): the unit vector, in the
  // drawing plane, from the last point that lies at least minYd behind the newest one. null when the lead has not moved that far, or the
  // newest point is older than maxAgeSec (nowSec given): the module reports no facing, so this is the heading of the movement.
  function heading(pts, nowSec, maxAgeSec, minYd) {
    if (pts && pts.length > 1) { const runs = trailRuns(pts); pts = runs[runs.length - 1]; } // only the stretch after the last break
    if (!pts || pts.length < 2) return null;
    const last = pts[pts.length - 1], min = minYd == null ? 4 : minYd;
    if (nowSec && last[2] && nowSec - last[2] > (maxAgeSec == null ? 20 : maxAgeSec)) return null;
    for (let i = pts.length - 2; i >= 0; i--) {
      const d = Math.hypot(last[0] - pts[i][0], last[1] - pts[i][1]);
      if (d >= min) {
        const v = project((last[0] - pts[i][0]) / d, (last[1] - pts[i][1]) / d);
        return { x: v.x, y: v.y };
      }
    }
    return null;
  }

  // Which trail points get a time label: walking back from the newest, one whenever the picture has moved minPx since the last one
  // labelled, at most maxN. Returns [{x, y, t}] in the drawing plane, oldest first (points without a time are skipped).
  function trailStamps(pts, scale, minPx, maxN) {
    const out = [];
    let prev = null;
    for (let i = (pts || []).length - 1; i >= 0 && out.length < (maxN == null ? 8 : maxN); i--) {
      const p = pts[i];
      if (!p[2]) continue;
      const q = project(p[0], p[1]);
      if (prev && Math.hypot(q.x - prev.x, q.y - prev.y) * scale < (minPx == null ? 70 : minPx)) continue;
      out.push({ x: q.x, y: q.y, t: p[2] });
      prev = q;
    }
    return out.reverse();
  }

  // The scale bar: the longest round number of yards that is at most maxPx pixels wide at this scale (pixels per yard): {yd, px}.
  function scaleBar(scale, maxPx) {
    const steps = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000];
    let yd = steps[0];
    steps.forEach((s) => { if (s * scale <= (maxPx == null ? 110 : maxPx)) yd = s; });
    return { yd, px: yd * scale };
  }

  // The zones of a map that overlap a plane box [x, y, w, h] and are not the skipped one: [{name, x, y, w, h}] in the plane, for their labels.
  // bounds is the daemon's zone table {"<map>_<area>": {map_id, area_id, name, loc_left, loc_right, loc_top, loc_bottom}}.
  function zonesIn(bounds, mapId, box, skipArea) {
    const out = [];
    Object.keys(bounds || {}).forEach((k) => {
      const z = bounds[k];
      if (!z || z.map_id !== mapId || z.area_id === skipArea || typeof z.loc_left !== "number") return;
      const a = project(z.loc_top, z.loc_left), b = project(z.loc_bottom, z.loc_right);
      const r = { name: z.name, x: Math.min(a.x, b.x), y: Math.min(a.y, b.y), w: Math.abs(a.x - b.x), h: Math.abs(a.y - b.y) };
      if (r.x < box[0] + box[2] && r.x + r.w > box[0] && r.y < box[1] + box[3] && r.y + r.h > box[1]) out.push(r);
    });
    return out;
  }

  // The picture slots the daemon lists for a map (GET /api/v1/maps/<id>), as the page may use them: the World map and its floors, the
  // Minimap and the Navmesh, in that order whatever order the answer came in. An available slot keeps only a daemon file URL and a
  // finite rectangle in the drawing plane; anything else drops it. A missing slot keeps its label and its one-line reason.
  const PIC_URL = /^\/api\/v1\/mapfiles\/[0-9]{1,9}(?:\.[a-z]+(?:-[0-9]+f)?)?\.png$/;
  const PIC_KINDS = ["worldmap", "minimap", "navmesh"];
  const PIC_LABEL = { worldmap: "World map", minimap: "Minimap", navmesh: "Navmesh" };
  const PIC_FLOOR = /^[1-9][0-9]?f$/;
  function pictureSlots(list) {
    if (!Array.isArray(list)) return [];
    const byKey = {};
    list.forEach((l) => {
      if (!l || typeof l !== "object" || PIC_KINDS.indexOf(l.kind) < 0) return;
      const floor = l.kind === "worldmap" && typeof l.floor === "string" && PIC_FLOOR.test(l.floor) ? l.floor : "";
      const key = l.kind + (floor ? " " + floor : "");
      if (byKey[key]) return;
      const label = typeof l.label === "string" && l.label ? l.label.slice(0, 40) : PIC_LABEL[l.kind] + (floor ? " " + floor : "");
      if (l.available === true) {
        if (typeof l.url !== "string" || !PIC_URL.test(l.url)
          || ![l.x, l.y, l.w, l.h].every((n) => typeof n === "number" && isFinite(n)) || !(l.w > 0) || !(l.h > 0)) return;
        byKey[key] = { kind: l.kind, floor, key, label, available: true, url: l.url, x: l.x, y: l.y, w: l.w, h: l.h };
      } else {
        byKey[key] = { kind: l.kind, floor, key, label, available: false, reason: typeof l.reason === "string" ? l.reason.slice(0, 200) : "" };
      }
    });
    return Object.keys(byKey).map((k) => byKey[k]).sort((a, b) => {
      const ka = PIC_KINDS.indexOf(a.kind), kb = PIC_KINDS.indexOf(b.kind);
      if (ka !== kb) return ka - kb;
      return (a.floor ? parseInt(a.floor, 10) : 0) - (b.floor ? parseInt(b.floor, 10) : 0);
    });
  }

  // Which slot a card draws: its stored choice (ms().pic) when that slot has a picture, else the first available slot (the main World
  // map picture); slot is null when the map has no picture at all. `missing` is the stored choice's slot when it has none, so the page
  // can say why the picture the user picked is gone while it draws the fallback.
  function pictureChoice(slots, stored) {
    const all = Array.isArray(slots) ? slots : [];
    const want = typeof stored === "string" && stored ? all.find((s) => s.key === stored) || null : null;
    const slot = (want && want.available ? want : null) || all.find((s) => s.available) || null;
    return { slot, missing: want && !want.available ? want : null };
  }

  // How long a map's picture slots may stay as they are before the daemon is asked again: 30 s when there is no picture at all (the
  // operator may have run minimap-extract since), 5 min while some are missing, never while all are there. A null (or missing) answer
  // is asked again after 30 s too.
  const PIC_RETRY_MS = 30000, PIC_MISSING_MS = 5 * 60 * 1000;
  function pictureRefreshAfter(slots) {
    if (!Array.isArray(slots) || !slots.length) return PIC_RETRY_MS;
    const have = slots.filter((s) => s && s.available).length;
    if (!have) return PIC_RETRY_MS;
    return have < slots.length ? PIC_MISSING_MS : null;
  }

  // The picture picker of a card: hidden when no slot has a picture; else one option per slot, a missing one disabled with "(none)" and
  // its reason as the tooltip. `reason` is the missing stored choice's reason, for the line under the picker ("" when the picker is hidden).
  function pictureSelect(slots, stored) {
    const all = Array.isArray(slots) ? slots : [];
    const choice = pictureChoice(all, stored);
    return {
      hidden: !choice.slot,
      value: choice.slot ? choice.slot.key : "",
      // no picture at all: the picker is hidden and the bg layer carries the reason, so there is no line to explain
      reason: choice.slot && choice.missing ? choice.missing.reason : "",
      options: all.map((s) => ({
        value: s.key,
        label: s.available ? s.label : s.label + " (none)",
        disabled: !s.available,
        title: s.available ? "" : s.reason,
      })),
    };
  }

  const api = {
    HEAL_RANGE_YD, FAR_YD, LOW_HP_PCT, STALE_WARN_SEC, pretty, plural, ageText, roleKey, roleLabel, distance, leadBot, partyRows, orderText, stateWord,
    headerText, headerParts, botHref, warnings, newTracker, note, context, prune, project, fitView, viewBox, zoomAt, panBy, pinch, gridStep, gridLines,
    trailSegments, markGap, trailSegmentsBroken, trailRuns, trailMarks, trailEvent, jumpLimitYd, TRAVEL_MAX_YD_PER_SEC, TRAVEL_SLACK_YD, TRAVEL_MAX_GAP_SEC, clampScale, prependTrail, heading, trailStamps, scaleBar, zonesIn, pictureSlots, pictureChoice, pictureSelect, pictureRefreshAfter, PIC_RETRY_MS, PIC_MISSING_MS,
    raidBlocks, isRaid, layoutText, threatOffTanks, notesLog, deathsTimeline, controlLines, whyWaiting,
    AGGRO_RANGE_YD, followView, zoomFollow, pinchFollow, mobsOf, mobClass, threatLines, botTargetLines, aggroMobs, commanderPlace,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.CmdView = api;
})(typeof self !== "undefined" ? self : this);
