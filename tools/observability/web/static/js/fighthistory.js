"use strict";
// Fight history: the debugger's night picker and its same-pack table, fed by the daemon's fight history
// (GET /api/v1/combat/history/days, /api/v1/combat/history and /api/v1/combat/packs). Pure, no DOM, no fetch, no markup:
// app.js renders what this returns; unit-tested by internal/commandersjs/fighthistory_check.js under node.
// Nights are local: a night is the local day it starts, rolling over at 06:00 (a fight at 02:17 local on 2 Nov belongs
// to the night of 1 Nov). The daemon keeps filing UTC days; the page fetches the UTC days a night spans (at most two)
// and filters by time. Labels come from fixed English day and month tables, never the browser's locale.
(function (root) {
  const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
  const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
  const DAY_SHAPE = /^(\d{4})-(\d{2})-(\d{2})$/;
  const NIGHT_ROLLOVER_H = 6;

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);
  const pad2 = (n) => (n < 10 ? "0" : "") + n;
  const round1 = (v) => Math.round(v * 10) / 10;

  // A UTC day as milliseconds, or null when the value is not a usable date.
  function ms(v) {
    if (v instanceof Date) return v.getTime();
    const n = num(v);
    return n !== null ? n : null;
  }

  // The YYYY-MM-DD of one UTC instant.
  function dayOf(msValue) {
    const t = ms(msValue);
    if (t === null) return "";
    const d = new Date(t);
    return d.getUTCFullYear() + "-" + pad2(d.getUTCMonth() + 1) + "-" + pad2(d.getUTCDate());
  }

  // The night one instant belongs to: the local day it starts, rolling over at 06:00 local. A fight at 02:17 local on
  // 2 Nov belongs to the night of 1 Nov; one at 07:00 belongs to 2 Nov.
  function nightOf(msValue) {
    const t = ms(msValue);
    if (t === null) return "";
    const d = new Date(t - NIGHT_ROLLOVER_H * 3600000);
    return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate());
  }

  // The UTC days one local night's fights are filed under: the night runs 06:00 local to next 06:00 local, which always
  // spans at most two UTC days, so fetching both covers the night.
  function nightDays(night) {
    const p = parts(night);
    if (!p) return [];
    const lo = new Date(p.y, p.m - 1, p.d, NIGHT_ROLLOVER_H, 0, 0).getTime();
    const hi = new Date(p.y, p.m - 1, p.d + 1, NIGHT_ROLLOVER_H, 0, 0).getTime() - 1;
    const days = {};
    days[dayOf(lo)] = true;
    days[dayOf(hi)] = true;
    return Object.keys(days).sort();
  }

  // The parts of a strict YYYY-MM-DD: a day that rolls over (month 13) is not a day.
  function parts(day) {
    const m = DAY_SHAPE.exec(String(day || ""));
    if (!m) return null;
    const y = parseInt(m[1], 10), mo = parseInt(m[2], 10), d = parseInt(m[3], 10);
    const date = new Date(Date.UTC(y, mo - 1, d));
    if (date.getUTCFullYear() !== y || date.getUTCMonth() !== mo - 1 || date.getUTCDate() !== d) return null;
    return { y, m: mo, d, weekday: date.getUTCDay() };
  }

  // One row of the pack table: when the fight started in words "28 Oct 22:14", in the viewer's own zone.
  function whenLabel(start) {
    const t = ms(start);
    if (t === null) return "";
    const d = new Date(t * 1000);
    return d.getDate() + " " + MONTHS[d.getMonth()] + " " + pad2(d.getHours()) + ":" + pad2(d.getMinutes());
  }

  // The local weekday and month of a strict YYYY-MM-DD, for the night's own label.
  function nightLabel(night) {
    const p = parts(night);
    if (!p) return night;
    const wd = new Date(p.y, p.m - 1, p.d, 12, 0, 0).getDay();
    return DAYS[wd] + " " + p.d + " " + MONTHS[p.m - 1];
  }

  // The night picker's options: Tonight first, then the stored nights newest first. Tonight is the current local night:
  // the rail draws it with the live list on top, so it is not listed a second time (its count belongs in the label).
  // A future night, or one that is not strict or not real, is skipped; a night is listed once. Labels say "(night)" so a
  // 02:17 fight reads under the evening it belongs to, e.g. "Sun 1 Nov (night)".
  function dayOptions(daysJson, todayMs) {
    const out = [{ value: "", label: "Tonight" }];
    const tonight = nightOf(todayMs);
    const list = daysJson && Array.isArray(daysJson.days) ? daysJson.days : [];
    const seen = {};
    list.forEach((r) => {
      if (!r) return;
      const day = String(r.day || "");
      if (seen[day]) return;
      seen[day] = true;
      if (tonight && day > tonight) return;
      if (tonight && day === tonight) {
        const n = num(r.fights) || 0;
        if (n > 0) out[0].label = "Tonight · " + n + (n === 1 ? " fight" : " fights");
        return;
      }
    });
    const rows = [];
    Object.keys(seen).forEach((day) => {
      if (tonight && (day >= tonight)) return;
      const p = parts(day);
      if (!p) return;
      const hit = list.find((r) => r && String(r.day || "") === day);
      const n = num(hit && hit.fights) || 0;
      rows.push({
        value: day,
        label: nightLabel(day) + " (night) · " + n + (n === 1 ? " fight" : " fights"),
        day,
      });
    });
    rows.sort((a, b) => (a.day > b.day ? -1 : a.day < b.day ? 1 : 0));
    rows.forEach((r) => out.push({ value: r.value, label: r.label }));
    return out;
  }

  // The picker value a pack row opens: a fight of tonight is Tonight, whose live list is the current one (and the fight's
  // detail is fetched by its key even when that list no longer holds it); an older row keeps its own night.
  function rowDay(day, todayMs) {
    const tonight = nightOf(todayMs);
    const d = String(day || "");
    return tonight && d === tonight ? "" : d;
  }

  function median(values) {
    if (!values.length) return 0;
    const sorted = values.slice().sort((a, b) => a - b);
    const mid = sorted.length >> 1;
    return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
  }

  // The same pack across runs: one row per stored fight, newest first, and the best attempt (fewest deaths, then shortest =
  // the least time spent, then the newest on a full tie). A wipe is never best: only a kill or a fight that ended counts.
  function packTable(json, selKey) {
    const rows = (json && Array.isArray(json.rows) ? json.rows : []).map((r) => {
      const start = num(r.start) || 0;
      const ntMs = num(r.nontank_ms);
      return {
        key: String(r.key || ""),
        commander: String(r.commander || ""),
        day: nightOf(start * 1000),
        when: whenLabel(start),
        start,
        durS: round1((num(r.dur) || 0) / 1000),
        deaths: num(r.deaths) || 0,
        ntS: ntMs === null ? null : round1(ntMs / 1000),
        outcome: String(r.outcome || "end"),
        sel: !!selKey && r.key === selKey,
        best: false,
      };
    });
    let best = -1;
    rows.forEach((r, i) => {
      if (r.outcome !== "kill" && r.outcome !== "end") return;
      if (best < 0) { best = i; return; }
      const b = rows[best];
      if (r.deaths < b.deaths
        || (r.deaths === b.deaths && (r.durS < b.durS || (r.durS === b.durS && r.start > b.start)))) best = i;
    });
    if (best >= 0) rows[best].best = true;
    return {
      rows,
      best: best >= 0 ? rows[best].key : null,
      median: { durS: round1(median(rows.map((r) => r.durS))), deaths: round1(median(rows.map((r) => r.deaths))) },
    };
  }

  const api = { dayOptions, packTable, whenLabel, dayOf, rowDay, nightOf, nightDays, nightLabel };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FightHistory = api;
})(typeof self !== "undefined" ? self : this);
