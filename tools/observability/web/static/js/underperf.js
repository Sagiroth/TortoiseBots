"use strict";
// Underperforming bots: the pure logic behind the Fight debugger's
// Underperforming bots section. query builds the API window of one card;
// html renders the report the API answers. No DOM and no fetch: unit-tested
// by internal/commandersjs/underperf_check.js under node.
(function (root) {
  const POLL_MS = 30000;
  // The metric a role is compared on (the report's `main`).
  const METRIC = { dps: "dps", hps: "hps", cps: "casts/s" };

  function esc(value) {
    if (value === null || value === undefined) return "";
    return String(value)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
  }

  function fmtInt(n) {
    const v = Math.round(Number(n) || 0);
    return String(v).replace(/\B(?=(\d{3})+(?!\d))/g, ",");
  }

  // One decimal place for dps/hps, two for casts and skill rates; null and
  // not-a-number print as an em dash.
  function fmt(v, digits) {
    if (v === null || v === undefined || !isFinite(Number(v))) return "—";
    let s = Number(v).toFixed(digits);
    if (s.indexOf(".") >= 0) s = s.replace(/0+$/, "").replace(/\.$/, "");
    return s;
  }

  function pct(ratio) {
    if (ratio === null || ratio === undefined || !isFinite(Number(ratio))) return "—";
    return Math.round(Number(ratio) * 100) + "%";
  }

  function utcDay(nowMs) {
    return new Date(nowMs).toISOString().slice(0, 10);
  }

  function pad2(n) {
    return (n < 10 ? "0" : "") + n;
  }

  function clock(unix) {
    const d = new Date(Number(unix) * 1000);
    if (isNaN(d.getTime())) return "";
    return pad2(d.getHours()) + ":" + pad2(d.getMinutes());
  }

  // The window the section asks for: the live binding since it began, the
  // last two hours for a live card without a bound time, one night of the
  // rail, or tonight for a read-only history card. `pick` is the run select's
  // value: "" for the whole window (run=all), "last" for its newest run, or
  // an index. `nightQ` is the picker's tz and rollover params; with them the
  // chosen night is a local night, as the history list groups it, and without
  // them it is the old UTC day.
  function query(bind, fd, nowMs, pick, nightQ) {
    const chosen = pick === null || pick === undefined || pick === "" ? "" : String(pick);
    const run = "&run=" + encodeURIComponent(chosen || "all");
    const zone = nightQ || "";
    if (fd && fd.day) {
      if (zone) return "night=" + encodeURIComponent(fd.day) + zone + run;
      return "day=" + encodeURIComponent(fd.day) + run;
    }
    if (bind && bind.history) {
      const night = zone && typeof FightHistory !== "undefined" ? FightHistory.nightOf(nowMs) : "";
      if (night) return "night=" + encodeURIComponent(night) + zone + run;
      return "day=" + utcDay(nowMs) + run;
    }
    if (bind && Number(bind.bound_at) > 0) return "since=" + Math.floor(Number(bind.bound_at)) + run;
    const since = Math.floor((Number(nowMs) / 1000 - 7200) / 60) * 60;
    return "since=" + since + run;
  }

  // The bound bots the window has no fight for: neither listed nor in ok.
  function missing(res, bindBots) {
    const seen = {};
    ((res && res.bots) || []).forEach((b) => { if (b) seen[b.name] = true; });
    ((res && res.ok) || []).forEach((b) => { if (b) seen[b.name] = true; });
    const out = [];
    (bindBots || []).forEach((b) => {
      const name = typeof b === "string" ? b : b && b.name;
      if (name && !seen[name]) out.push(name);
    });
    return out;
  }

  // The run picker: the whole window first ("" — the whole night of a
  // history card, the current binding of a live one), then its newest run
  // alone ("last"), then each run of the window by index.
  function runSelect(res, opts) {
    const runs = Array.isArray(res.runs) ? res.runs : [];
    const pick = opts.pick === null || opts.pick === undefined || opts.pick === "" ? "" : String(opts.pick);
    const picked = pick || "";
    let out = '<label class="fd-undrunlab">Run <select data-fd-underrun aria-label="Run">';
    out += '<option value=""' + (picked === "" ? " selected" : "") + ">" + (opts.live ? "This run" : "Whole night") + "</option>";
    out += '<option value="last"' + (picked === "last" ? " selected" : "") + ">Last run</option>";
    runs.forEach((r, i) => {
      const label = clock(r.since) + "-" + clock(r.until) + (r.plan ? " · " + r.plan : "") + " · " + fmtInt(r.fights) + " fights";
      const sel = String(i) === picked ? " selected" : "";
      out += '<option value="' + i + '"' + sel + ">" + esc(label) + "</option>";
    });
    return out + "</select></label>";
  }

  function reasonChip(reason) {
    const isRule = String(reason.id || "").indexOf("rule:") === 0;
    const text = isRule ? String(reason.id).slice(5) + " ×" + reason.fights : reason.text;
    return '<span class="fd-undchip ' + (reason.level === "red" ? "red" : "yellow") + '">' + esc(text) + "</span>";
  }

  function refLabel(ref) {
    return ref.boss_name ? ref.boss_name : "step " + (ref.step || 0);
  }

  // The night a fight link opens: the page's local night of the fight's
  // start, as the history list groups nights (06:00 rollover), so a fight
  // after midnight opens on the night it was played in. The answer carries
  // no day of its own; a fight without a start opens on Tonight.
  function refNight(ref) {
    if (ref && ref.start && typeof FightHistory !== "undefined" && FightHistory.nightOf) {
      const night = FightHistory.nightOf(Number(ref.start) * 1000);
      if (night) return night;
    }
    return "";
  }

  function refLink(ref) {
    return '<button type="button" class="fd-undfight" data-fd-open="' + esc(encodeURIComponent(ref.key)) + '" data-fd-day="'
      + esc(refNight(ref)) + '">' + esc(refLabel(ref)) + "</button>";
  }

  function row(bot) {
    const you = bot.you || {};
    const peers = bot.peers || {};
    const ratio = bot.ratio || {};
    const main = bot.main || "dps";
    const digits = main === "cps" ? 2 : 1;
    const dot = '<span class="fd-unddot ' + (bot.level === "red" ? "red" : "yellow") + '" aria-hidden="true"></span>';
    const name = '<button type="button" class="fd-undname" data-fd-profile="' + esc(bot.guid) + '">' + esc(bot.name) + "</button>";
    const sub = '<span class="fd-undsub">' + esc(bot.class || "") + (bot.spec ? " · " + esc(bot.spec) : "") + "</span>";
    const chips = (bot.reasons || []).map(reasonChip).join("");
    const causes = (bot.causes || []).length
      ? '<div class="fd-undcause">' + esc(bot.causes.join(" · ")) + "</div>" : "";
    const refs = (bot.fight_refs || []);
    const links = refs.slice(0, 8).map(refLink).join(" ");
    const more = refs.length > 8 ? ' <span class="fd-undmore">+' + (refs.length - 8) + " more</span>" : "";
    const skill = main === "cps"
      ? '<div class="fd-undsub">skill ' + fmt(you.skill10, 2) + "/10 s vs " + fmt(peers.skill10, 2) + "</div>" : "";
    return "<tr class=\"fd-undrow " + (bot.level === "red" ? "red" : "yellow") + '">'
      + "<td>" + dot + name + sub + "</td>"
      + '<td class="fd-undrole">' + esc(bot.role) + "</td>"
      + "<td>" + esc(METRIC[main] || main) + " " + fmt(you[main], digits) + " vs " + fmt(peers[main], digits)
      + " (" + pct(ratio.main) + ")" + skill + "</td>"
      + "<td>casts/s " + fmt(you.cps, 2) + " vs " + fmt(peers.cps, 2) + "</td>"
      + "<td>deaths " + fmt(you.deaths, 0) + " vs " + fmt(peers.deaths, 1) + "</td>"
      + "<td>flagged in " + fmtInt(bot.flagged) + " of " + fmtInt(bot.judged) + " fights</td>"
      + '<td class="fd-undwhy">' + chips + causes + "</td>"
      + '<td class="fd-undfights">' + links + more + "</td>"
      + "</tr>";
  }

  function table(bots) {
    return '<div class="fd-untable"><table><thead><tr>'
      + "<th>Bot</th><th>Role</th><th>Main metric</th><th>Activity</th><th>Deaths</th><th>Flagged</th><th>Why</th><th>Fights</th>"
      + "</tr></thead><tbody>" + bots.map(row).join("") + "</tbody></table></div>";
  }

  // The bots an answer names, each guid once: a bot whose role changed in
  // the window is two rows, one per role, but still one bot.
  function botCount(res) {
    const seen = {};
    let n = 0;
    const add = (rows) => (rows || []).forEach((b) => {
      if (!b) return;
      const key = b.guid !== null && b.guid !== undefined && b.guid !== "" ? "g" + b.guid : "n" + b.name;
      if (seen[key]) return;
      seen[key] = true;
      n++;
    });
    add(res.bots);
    add(res.ok);
    return n;
  }

  // The `truncated` note: the window held more fights than one answer keeps.
  function truncNote(res) {
    const cap = res.limits && Number(res.limits.max_fights) > 0 ? fmtInt(res.limits.max_fights) : "";
    return "(" + (cap ? "newest " + cap + " fights only" : "newest fights only") + ")";
  }

  function html(st, opts) {
    opts = opts || {};
    st = st || {};
    const res = st.res;
    // A rejected pick (400: the run went away with the window) keeps the
    // picker under the error, so another run can be picked; no table is
    // drawn from the answer of the window before it.
    if (st.err) {
      if (!res) return '<p class="fd-undlead fd-underr">' + esc(st.err) + "</p>";
      return '<div class="fd-undhead">' + runSelect(res, opts) + "</div>"
        + '<p class="fd-undlead fd-underr">' + esc(st.err) + "</p>";
    }
    if (!res) return '<p class="fd-undlead">Loading…</p>';
    if (!res.fights) return '<p class="fd-undlead">No fights recorded in this window.</p>';
    const bots = Array.isArray(res.bots) ? res.bots : [];
    const ok = Array.isArray(res.ok) ? res.ok : [];
    const out = [];
    out.push('<div class="fd-undhead"><span class="fd-undsum">' + fmtInt(res.fights) + " fights, " + fmtInt(res.judged)
      + " judged · " + fmtInt(botCount(res)) + " bots · " + fmtInt(bots.length) + " flagged</span>"
      + runSelect(res, opts) + "</div>");
    if (res.truncated) {
      out.push('<p class="fd-undtrunc">' + esc(truncNote(res)) + "</p>");
    }
    if (!bots.length) {
      out.push('<p class="fd-undlead">No bot underperformed in these ' + fmtInt(res.fights) + " fights.</p>");
    } else {
      out.push(table(bots));
      out.push('<p class="fd-undoek">' + fmtInt(ok.length) + " bots without a flag</p>");
    }
    const miss = missing(res, opts.bindBots);
    if (miss.length) out.push('<p class="fd-undmiss">No fights recorded for: ' + esc(miss.join(", ")) + "</p>");
    out.push('<p class="fd-undnote">Range, line of sight and mana are not in the fight data yet; causes come from casts, damage, healing and deaths only.</p>');
    return out.join("");
  }

  const api = { POLL_MS, esc, fmt, query, html, missing };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.Underperf = api;
})(typeof self !== "undefined" ? self : this);
