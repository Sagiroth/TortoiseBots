"use strict";
// Commanders tab: the plain-language and map-view logic, free of the page so a test can run it under node
// (internal/commandersjs). Every function here is pure except the tracker, which only keeps what it is handed.
// app.js turns these results into HTML; nothing here builds markup.
(function (root) {
  const FAR_YD = 40;          // a bot this far from the lead is flagged
  const LOW_HP_PCT = 30;      // a live bot under this is flagged
  const STALE_WARN_SEC = 30;  // no update for this long is flagged
  const HEAL_RANGE_YD = 40;   // a healer's spells reach this far (the ring of the healer-range layer)
  const TRAIL_MAX = 120;      // positions of the lead kept for its trail
  const TRAIL_STEP_YD = 2;    // a new trail point after this much movement
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
    let t = tr.trail[key];
    if (!t || t.map !== lead.map) t = tr.trail[key] = { map: lead.map, pts: [] };
    const last = t.pts[t.pts.length - 1];
    if (!last || Math.hypot(last[0] - lead.x, last[1] - lead.y) >= TRAIL_STEP_YD) {
      if (last) tr.leadMovedAt[key] = nowSec;
      t.pts.push([lead.x, lead.y, nowSec]);
      if (t.pts.length > TRAIL_MAX) t.pts.shift();
    }
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

  // The direction the lead is walking, from the trail (positions [x, y, t] in game coordinates, oldest first): the unit vector, in the
  // drawing plane, from the last point that lies at least minYd behind the newest one. null when the lead has not moved that far, or the
  // newest point is older than maxAgeSec (nowSec given): the module reports no facing, so this is the heading of the movement.
  function heading(pts, nowSec, maxAgeSec, minYd) {
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

  // The picture layers the daemon lists for a map (GET /api/v1/maps/<id>), as the page may use them: only a name, a daemon file URL
  // and a finite rectangle in the drawing plane make it through; anything else is dropped.
  const PIC_URL = /^\/api\/v1\/mapfiles\/[0-9]{1,9}(?:\.[a-z]+(?:-[0-9]+f)?)?\.png$/;
  function pictureLayers(list) {
    if (!Array.isArray(list)) return [];
    return list.filter((l) => l && typeof l.url === "string" && PIC_URL.test(l.url) && typeof l.name === "string"
      && [l.x, l.y, l.w, l.h].every((n) => typeof n === "number" && isFinite(n)) && l.w > 0 && l.h > 0)
      .map((l) => ({ name: l.name.slice(0, 40), url: l.url, x: l.x, y: l.y, w: l.w, h: l.h }));
  }

  const api = {
    HEAL_RANGE_YD, FAR_YD, LOW_HP_PCT, STALE_WARN_SEC, pretty, plural, ageText, roleKey, roleLabel, distance, leadBot, partyRows, orderText, stateWord,
    headerText, headerParts, botHref, warnings, newTracker, note, context, prune, project, fitView, viewBox, zoomAt, panBy, pinch, gridStep, gridLines,
    trailSegments, clampScale, heading, trailStamps, scaleBar, zonesIn, pictureLayers,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.CmdView = api;
})(typeof self !== "undefined" ? self : this);
