"use strict";
// Fights panel: the per-fight combat numbers ("a light Turtle Logs") for one commander binding.
// Pure, no markup and no DOM: app.js fetches /api/v1/combat/* and sets innerHTML; unit-tested by
// internal/commandersjs/fights_check.js (node). Mounted under the raid card by A11
// (Fights.render if present); until then it mounts itself under the Commanders card when loaded.
(function (root) {
  const cmd = () => (typeof CmdView !== "undefined" ? CmdView : require("./commanders.js"));

  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);

  // The debuff keys in the panel's order (the module samples these 1 Hz on the focus unit).
  const DEBUFF_KEYS = ["sunder5", "sunder", "faerie_fire", "cor", "coe", "cos", "demo", "thunder_clap", "expose_armor"];

  // One row per fight of GET /api/v1/combat/fights: outcome, how long, boss, deaths, warnings.
  // list: [{id, start, dur, outcome, boss_name, leg, step, deaths, warnings}].
  function fightRows(list) {
    return (Array.isArray(list) ? list : []).map((f) => ({
      id: f.id || "",
      start: num(f.start),
      dur: num(f.dur),
      outcome: f.outcome || "end",
      boss: f.boss_name || "–",
      leg: f.leg || "",
      step: num(f.step),
      deaths: num(f.deaths) || 0,
      warnings: num(f.warnings) || 0,
    }));
  }

  const fmtDur = (ms) => {
    const m = num(ms);
    if (m === null || m < 0) return "–";
    const s = Math.round(m / 1000);
    return Math.floor(s / 60) + "m " + String(s % 60).padStart(2, "0") + "s";
  };
  const fmtPct = (a, b) => {
    const x = num(a), y = num(b);
    if (x === null || y === null || y <= 0) return "–";
    return Math.round((x / y) * 100) + " %";
  };

  // Per-bot numbers of one fight. Rows are by subgroup then name (the raid card's order):
  // role, damage + dps (dmg / dur), healing + hps, overheal %, damage taken, casts,
  // the top 3 spells with count and per-minute, deaths.
  // sortKey: "dps" | "hps" (else subgroup/name). dps/hps are 0 on a fight with no duration.
  function botTable(fight, sortKey) {
    const durS = fight && num(fight.dur_ms) ? fight.dur_ms / 1000 : 0;
    const rows = ((fight && fight.bots) || []).map((b) => {
      const top = (Array.isArray(b.spells) ? b.spells.slice() : []).sort((x, y) => (y.c || 0) - (x.c || 0) || String(x.n).localeCompare(String(y.n))).slice(0, 3).map((s) => ({
        name: s.n || "",
        casts: num(s.c) || 0,
        perMin: durS > 0 ? (num(s.c) || 0) / (durS / 60) : 0,
      }));
      const heal = num(b.heal) || 0, over = num(b.over) || 0;
      return {
        name: b.name || "",
        sub: num(b.sub) === null ? -1 : b.sub,
        role: b.role || "dps",
        cls: b.class || "",
        dmg: num(b.dmg) || 0,
        dps: durS > 0 ? (num(b.dmg) || 0) / durS : 0,
        heal,
        hps: durS > 0 ? heal / durS : 0,
        overhealPct: heal + over > 0 ? (over / (heal + over)) * 100 : 0,
        taken: num(b.taken) || 0,
        casts: num(b.casts) || 0,
        spells: top,
        deaths: num(b.deaths) || 0,
      };
    });
    const byName = (a, b) => String(a.name).localeCompare(String(b.name));
    if (sortKey === "dps") rows.sort((a, b) => b.dps - a.dps || byName(a, b));
    else if (sortKey === "hps") rows.sort((a, b) => b.hps - a.hps || byName(a, b));
    else rows.sort((a, b) => (a.sub === b.sub ? byName(a, b) : (a.sub < 0 ? 1 : b.sub < 0 ? -1 : a.sub - b.sub)));
    return rows;
  }

  // Debuff uptime: percent = up_s / sampled_s per key, in the panel order.
  function debuffBars(fight) {
    const d = (fight && fight.debuffs) || {}, sampled = num(d.sampled_s) || 0, up = d.up_s || {};
    return DEBUFF_KEYS.map((k) => ({ key: k, pct: sampled > 0 ? ((num(up[k]) || 0) / sampled) * 100 : 0 }));
  }

  // Warnings as one strip: red first, then the text of each.
  function warningStrip(warnings) {
    const list = Array.isArray(warnings) ? warnings : [];
    const rank = (w) => (w.level === "red" ? 0 : w.level === "yellow" ? 1 : 2);
    return list.slice().sort((a, b) => rank(a) - rank(b) || String(a.bot).localeCompare(String(b.bot))).map((w) => ({
      rule: w.rule || "",
      level: w.level || "info",
      bot: w.bot || "",
      text: w.text || "",
    }));
  }

  // "planned 11502; engaged 11502 x1, 11673 x2 (adds)": what the run's step
  // wanted (planned entries) against what the members fought (engaged).
  function pullPostMortem(fight) {
    const planned = ((fight && fight.planned) || []).filter((e) => num(e) !== null);
    const engaged = (fight && fight.engaged) || [];
    const plan = "planned " + (planned.length ? planned.join(", ") : "–");
    const eng = (engaged.length ? engaged.map((e) => e.e + " x" + (num(e.c) || 0)).join(", ") : "nothing")
      + (engaged.length > 1 ? " (adds)" : "");
    return plan + "; engaged " + eng;
  }

  // The panel under the raid card: the fights list, the selected fight's
  // warnings, per-bot table, debuff bars and the pull post-mortem.
  // app.js (or A11) calls Fights.render(container, commander) after drawing a card.
  function render(container, commander, fights, selected) {
    if (!container) return;
    const rows = fightRows(fights);
    const body = rows.length
      ? '<table class="data-table fight-table"><thead><tr><th>Fight</th><th>Length</th><th>Boss</th><th>Outcome</th><th>Deaths</th><th>Warnings</th></tr></thead><tbody>'
        + rows.map((r) => '<tr data-fight="' + esc(r.id) + '"><td>' + esc(r.boss) + (r.leg ? ' · ' + esc(r.leg) : "") + '</td><td>' + esc(fmtDur(r.dur)) + "</td><td>" + esc(r.boss) + "</td><td>" + esc(r.outcome) + "</td><td>" + r.deaths + "</td><td>" + r.warnings + "</td></tr>").join("")
        + "</tbody></table>"
      : '<p class="empty-hint">No fights reported for ' + esc(commander) + " yet (the module sends one COMBAT_FIGHT datagram per finished fight while the run's group fights).</p>";
    const detail = selected ? fightDetail(selected) : "";
    const html = '<div class="fight-panel"><h3 class="cmd-h">Fights</h3>' + body + detail + "</div>";
    if (typeof container.innerHTML === "string") container.innerHTML = html;
    else if (typeof container === "function") container(html);
  }

  function fightDetail(sel) {
    const fight = sel.fight || sel, warnings = sel.warnings || [];
    const rows = botTable(fight);
    const bars = debuffBars(fight);
    return '<div class="fight-detail">'
      + (warnings.length ? '<div class="fight-warnings">' + warningStrip(warnings).map((w) => '<span class="cmd-pill ' + (w.level === "red" ? "bad" : "warn") + '">' + esc(w.text) + "</span>").join("") + "</div>" : "")
      + '<table class="data-table fight-bots"><thead><tr><th>Bot</th><th>Sub</th><th>DPS</th><th>HPS</th><th>Taken</th><th>Deaths</th></tr></thead><tbody>'
      + rows.map((r) => '<tr><td>' + esc(r.name) + ' <span class="muted">' + esc(r.role) + "</span></td><td>" + (r.sub >= 0 ? r.sub : "–") + "</td><td>" + Math.round(r.dps) + "</td><td>" + Math.round(r.hps) + "</td><td>" + r.taken + "</td><td>" + r.deaths + "</td></tr>").join("")
      + "</tbody></table>"
      + '<div class="fight-debuffs">' + bars.map((b) => '<span class="fight-debuff">' + esc(b.key) + " " + Math.round(b.pct) + " %</span>").join("") + "</div>"
      + '<p class="empty-hint">' + esc(pullPostMortem(fight)) + "</p></div>";
  }

  const api = { DEBUFF_KEYS, fightRows, fmtDur, fmtPct, botTable, debuffBars, warningStrip, pullPostMortem, render };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.Fights = api;
})(typeof self !== "undefined" ? self : this);
