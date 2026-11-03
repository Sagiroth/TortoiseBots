"use strict";
// Raid coverage: the pure half of the dashboard's coverage view and the boss panel's chips. It answers, boss by boss,
// which of the boss table's abilities the bots have an answer for. The rule is the audit spec's (fd-tactics-coverage §1):
// every ability and every add line carries needs (interrupt, kill, front, tail, avoid, mark, dispel, purge, resist) and a
// need is answered by a tactic row, a stand set or a code answer; the level ranks full > partly > unknown > none.
//
// The same rule runs in tools/boss_coverage.py (the CLI prints it, the driver reads the migrations). The two are kept
// equal by data/boss/testdata/coverage_cases.json, which both test suites run. No DOM, no fetch, no boss fact: every
// entry, spell id and raid key comes from the table, the rows or the fixture.
//
// Table = the built boss_abilities.json ({maps, bosses, codeAnswers}); rows = GET /api/v1/boss/tactics
// ({tactics, stands, planKillFirst, source, loadedAt}) or null when the world read failed.
(function (root) {
  const NEED_ORDER = ["interrupt", "kill", "front", "tail", "avoid", "mark", "dispel", "purge", "resist", "classify"];
  // A need a world table row (tactic row, stand set, plan list) could answer.
  const DB_NEEDS = { interrupt: true, kill: true, front: true, tail: true, avoid: true, mark: true };
  const LEVEL_ORDER = { none: 0, unknown: 1, partly: 2, full: 3 };
  const COUNT_KEYS = ["answered", "partly", "missing", "notneeded", "unknown"];
  const GLYPH = { answered: "✓", partly: "◐", missing: "✗", notneeded: "–", unknown: "?" };
  // A need's level, not a status: "none" (no answer at all) reads as a cross, "ok" (waived) as a dash.
  const NEED_GLYPH = { full: "✓", partly: "◐", none: "✗", ok: "–", unknown: "?" };

  function esc(v) {
    return String(v == null ? "" : v).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  }

  function fmtNum(v) {
    const n = Number(v);
    if (!isFinite(n)) return "0";
    if (Number.isInteger(n)) return String(n);
    return String(Number(n.toFixed(6)));
  }

  function levels(needs) {
    const out = {};
    needs.forEach((need) => { out[need] = "none"; });
    return out;
  }

  function deriveNeeds(ability) {
    const needs = [];
    if (ability.int) needs.push("interrupt");
    if (ability.summon) needs.push("kill");
    if (ability.cone) needs.push("front");
    if (ability.tail) needs.push("tail");
    if (ability.ground) needs.push("avoid");
    if (ability.mark) needs.push("mark");
    if (ability.aura) needs.push("dispel");
    if (ability.self) needs.push("purge");
    return needs;
  }

  function statusOf(levelsOut, okAll) {
    if (okAll) return "notneeded";
    const keys = Object.keys(levelsOut);
    if (!keys.length) return "missing";
    const real = keys.filter((need) => levelsOut[need] !== "ok");
    if (!real.length) return "notneeded";
    if (real.every((need) => levelsOut[need] === "full")) return "answered";
    if (real.some((need) => levelsOut[need] === "full" || levelsOut[need] === "partly")) return "partly";
    if (real.some((need) => levelsOut[need] === "unknown")) return "unknown";
    return "missing";
  }

  function standText(stands) {
    const byRole = {};
    stands.forEach((stand) => { byRole[stand.role] = (byRole[stand.role] || 0) + stand.seats; });
    const roles = Object.keys(byRole).sort();
    const parts = roles.map((role) => role + " " + byRole[role]);
    const total = roles.reduce((sum, role) => sum + byRole[role], 0);
    return "stand set: " + parts.join(", ") + " " + (total === 1 ? "seat" : "seats");
  }

  function codeText(answer) {
    return (answer.map === "*" ? "generic: " : "code: ") + answer.name;
  }

  // ---- the boss worker: one boss's lines, warnings and extra rows (§1) --------------------------------------------------

  function bossWorker(doc, mapId, bossLookup, tactics, stands, plans, steps, codes, haveRows, truncated) {
    const entry = doc.entry;
    const sids = {};
    (doc.abilities || []).forEach((ability) => { sids[String(ability.sid)] = true; });
    const mapTactics = tactics.filter((row) => row.map === mapId);
    const ownTactics = mapTactics.filter((row) => row.boss === entry);
    const ownStands = stands.filter((stand) => stand.map === mapId && stand.boss === entry);
    const kills = mapTactics.filter((row) => row.kind === "kill_first");
    const ownPlans = plans.filter((plan) => plan.map === mapId);
    const ownSteps = steps.filter((step) => step.map === mapId);
    const warnings = [];
    const usedTactics = new Set();
    const usedStands = new Set();

    function warn(text) {
      if (warnings.indexOf(text) < 0) warnings.push(text);
    }

    function otherBoss(id) {
      const other = bossLookup[String(id)];
      return other ? other.name + " (" + id + ")" : String(id);
    }

    function codeMatches(answer, ability, sid, addEntry) {
      if (answer.map !== "*" && answer.map !== doc.map) return false;
      if (answer.boss !== 0 && answer.boss !== entry) return false;
      if (addEntry) return answer.add === addEntry;
      if (answer.spell) return answer.spell === sid;
      if (answer.flag) return !!(ability && ability[answer.flag]);
      return false;
    }

    function abilityLine(ability) {
      const sid = ability.sid;
      const derived = deriveNeeds(ability);
      const needs = derived.slice();
      const interruptRows = [];
      if (haveRows) mapTactics.forEach((row) => {
        if (row.kind !== "interrupt" || row.spell !== sid) return;
        const belongs = row.boss === entry;
        const matches = (belongs && (row.target === 0 || row.target === entry)) || row.target === entry;
        if (belongs) usedTactics.add(row);
        if (matches || belongs) interruptRows.push({ row, matches });
      });
      if (interruptRows.length && needs.indexOf("interrupt") < 0) {
        needs.unshift("interrupt");
        if (!ability.int) {
          warn("interrupt row on " + sid + " " + ability.name + ": the table does not flag it int; "
            + "check that the core can interrupt it");
        }
      }
      const standsHere = haveRows ? ownStands.filter((stand) => stand.windowSpell === sid) : [];
      if (standsHere.length) {
        standsHere.forEach((stand) => usedStands.add(stand));
        if (!derived.some((need) => need === "front" || need === "tail" || need === "avoid" || need === "mark")) {
          if (needs.indexOf("avoid") < 0) needs.push("avoid");
        }
      }

      const lv = levels(needs);
      const answers = {};
      needs.forEach((need) => { answers[need] = []; });
      const okAll = ability.ok || "";
      const okNeeds = ability.okNeeds || {};
      const codeLevels = {};
      function noteCode(code) {
        if (LEVEL_ORDER[code.level] > LEVEL_ORDER[codeLevels[code.need] || "none"]) {
          codeLevels[code.need] = code.level;
        }
      }

      function answer(need, level, text) {
        if (!(need in lv)) {
          lv[need] = "none";
          answers[need] = [];
          needs.push(need);
        }
        if (LEVEL_ORDER[level] > LEVEL_ORDER[lv[need]]) lv[need] = level;
        answers[need].push({ level, text });
      }

      if (haveRows) {
        interruptRows.forEach((match) => {
          if (!match.matches) return;
          answer("interrupt", "full", match.row.comment ? "interrupt row (" + match.row.comment + ")" : "interrupt row");
        });
        if (standsHere.length) {
          const text = standText(standsHere);
          ["front", "tail", "avoid", "mark"].forEach((need) => {
            if (need in lv) answer(need, "full", text);
          });
        }
      }

      if (ability.summon) killAnswers(ability.summon, answer, noteCode);
      ownTactics.forEach((row) => {
        if (row.kind === "behind" && haveRows && ability.cone) answer("front", "full", "behind " + fmtNum(row.param) + " yd");
      });

      codes.forEach((code) => {
        if (!codeMatches(code, ability, sid, 0)) return;
        answer(code.need, code.level, codeText(code));
        noteCode(code);
      });

      if (okAll) {
        Object.keys(lv).forEach((need) => { lv[need] = "ok"; });
      } else {
        Object.keys(okNeeds).forEach((need) => { if (need in lv) lv[need] = "ok"; });
      }
      if (!Object.keys(lv).length && !okAll) {
        lv.classify = "none";
        answers.classify = [];
        needs.push("classify");
      }
      if (truncated) {
        // The snapshot stops at a query cap: a missing or present row proves
        // nothing, so a need only the database can answer is unknown. A code
        // answer is not a row and keeps the best level it gave.
        Object.keys(lv).forEach((need) => {
          if (!DB_NEEDS[need] || lv[need] === "ok") return;
          const codeLevel = codeLevels[need];
          lv[need] = codeLevel && codeLevel !== "none" ? codeLevel : "unknown";
        });
      } else if (!haveRows) {
        Object.keys(lv).forEach((need) => {
          if (DB_NEEDS[need] && lv[need] !== "ok" && lv[need] !== "full") lv[need] = "unknown";
        });
      }

      const ordered = {};
      NEED_ORDER.forEach((need) => { if (need in lv) ordered[need] = lv[need]; });
      const answerList = [];
      Object.keys(ordered).forEach((need) => {
        (answers[need] || []).forEach((item) => answerList.push({ need, level: item.level, text: item.text }));
      });
      return {
        key: String(sid),
        kind: "ability",
        sid,
        add: 0,
        name: ability.name,
        phase: ability.phase || "",
        status: statusOf(ordered, okAll),
        needs: ordered,
        answers: answerList,
        ok: okAll,
        okNeeds: Object.assign({}, okNeeds),
      };
    }

    function killAnswers(id, answer, onCode) {
      if (haveRows) {
        kills.forEach((row) => {
          if (row.target !== id) return;
          if (row.boss === entry) {
            answer("kill", "full", "kill_first " + id + " (prio " + row.priority + ")");
            usedTactics.add(row);
          } else {
            answer("kill", "partly", "kill_first " + id + " (row under boss " + otherBoss(row.boss) + ")");
            usedTactics.add(row);
            warn("kill_first on " + id + " is under boss " + otherBoss(row.boss) + ": check it is the right boss");
          }
        });
        ownSteps.forEach((step) => {
          if (step.entries.indexOf(id) < 0) return;
          if (step.boss === entry) answer("kill", "full", "plan " + step.plan + " boss step kill list");
          else answer("kill", "partly", "plan " + step.plan + " boss step of " + otherBoss(step.boss));
        });
        ownPlans.forEach((plan) => {
          if (plan.entries.indexOf(id) >= 0) answer("kill", "partly", "plan " + plan.plan + " kill_first (run leader only)");
        });
      }
      codes.forEach((code) => {
        if (!codeMatches(code, null, 0, id)) return;
        answer("kill", code.level, codeText(code));
        if (onCode) onCode(code);
      });
    }

    function addLine(add) {
      const id = add.entry;
      const lv = { kill: "none" };
      const list = [];
      let codeLevel = "none";
      function answer(need, level, text) {
        if (LEVEL_ORDER[level] > LEVEL_ORDER[lv[need]]) lv[need] = level;
        list.push({ need, level, text });
      }
      function noteCode(code) {
        if (LEVEL_ORDER[code.level] > LEVEL_ORDER[codeLevel]) codeLevel = code.level;
      }
      killAnswers(id, answer, noteCode);
      if (truncated) {
        // A code answer is not a row and keeps its level; without one the kill
        // need depends on rows the cut may have dropped.
        lv.kill = codeLevel !== "none" ? codeLevel : "unknown";
      } else if (!haveRows && lv.kill !== "full") lv.kill = "unknown";
      return {
        key: "add:" + id,
        kind: "add",
        sid: 0,
        add: id,
        name: add.name,
        phase: "",
        status: statusOf(lv, ""),
        needs: Object.assign({}, lv),
        answers: list,
        ok: "",
        okNeeds: {},
      };
    }

    function extras() {
      const out = [];
      if (!haveRows) return out;
      const hasCone = (doc.abilities || []).some((ability) => ability.cone);
      ownTactics.forEach((row) => {
        if (usedTactics.has(row)) return;
        let why = "";
        if (row.kind === "interrupt" && haveRows) {
          if (sids[String(row.spell)]) return;
          why = "no ability with spell " + row.spell + " in the table";
        } else if (row.kind === "behind" && haveRows) {
          if (hasCone) return;
          why = sids[String(row.spell)] ? "boss has no cone ability" : "no ability with spell " + row.spell + " in the table";
        } else if (row.kind === "kill_first" && haveRows) {
          why = "kill_first on entry " + row.target + ": neither a summon nor an add of the table";
        } else if (row.kind === "air_target" && haveRows) {
          why = "air_target: keeps the boss a target in its air phase (not an ability answer)";
        } else {
          return;
        }
        out.push({ key: tacticKey(row), ident: tacticIdent(row), why });
      });
      ownStands.forEach((stand) => {
        if (usedStands.has(stand)) return;
        if (sids[String(stand.windowSpell)]) return;
        const why = stand.windowSpell === 0
          ? "window_spell 0: applies always, not an ability answer"
          : "no ability with spell " + stand.windowSpell + " in the table";
        out.push({ key: "stand:" + stand.windowSpell, ident: "stand " + stand.windowSpell, why });
      });
      return dedupeExtras(out);
    }

    function result() {
      const lines = (doc.abilities || []).map(abilityLine);
      (doc.adds || []).forEach((add) => {
        if ((add.priority || 0) > 0) lines.push(addLine(add));
      });
      const counts = {};
      COUNT_KEYS.forEach((key) => { counts[key] = 0; });
      lines.forEach((line) => { counts[line.status] += 1; });
      return {
        entry,
        name: doc.name,
        map: doc.map,
        lines,
        warnings: warnings.slice(),
        extra: extras(),
        counts,
      };
    }

    return { result };
  }

  function tacticIdent(row) {
    return row.kind === "kill_first" ? "kill_first " + row.target : row.kind + " " + row.spell;
  }

  function tacticKey(row) {
    const ident = row.kind === "kill_first" ? row.target : row.spell;
    return "tactic:" + row.kind + ":" + ident;
  }

  function dedupeExtras(list) {
    const seen = {};
    const out = [];
    list.forEach((item) => {
      if (seen[item.key]) return;
      seen[item.key] = true;
      out.push(item);
    });
    return out;
  }

  // ---- the rule (§1) ---------------------------------------------------------------------------------------------------

  function compute(table, rows) {
    const doc = table || {};
    const maps = doc.maps || [];
    const bosses = doc.bosses || {};
    const codes = doc.codeAnswers || [];
    const rowsRead = rows !== null && rows !== undefined;
    const truncated = rowsRead && !!rows.truncated;
    // A truncated snapshot only holds the rows before the query cap: they
    // cannot answer a need or show an extra row, so they count as unread.
    const haveRows = rowsRead && !truncated;
    const tactics = (rows && rows.tactics) || [];
    const stands = (rows && rows.stands) || [];
    const plans = (rows && rows.planKillFirst) || [];
    const steps = (rows && rows.bossKillFirst) || [];
    const out = [];
    maps.forEach((raid) => {
      const mapId = raid.mapId;
      const entries = (raid.bosses || []).filter((entry) => String(entry) in bosses);
      const bossResults = entries.map((entry) => bossWorker(bosses[String(entry)], mapId, bosses, tactics, stands, plans, steps, codes, haveRows, truncated).result());
      const unknown = {};
      if (haveRows) {
        tactics.forEach((row) => {
          if (row.map !== mapId || String(row.boss) in bosses) return;
          (unknown[row.boss] = unknown[row.boss] || []).push({
            key: tacticKey(row), ident: tacticIdent(row), why: "boss " + row.boss + " is not in the table",
          });
        });
        stands.forEach((stand) => {
          if (stand.map !== mapId || String(stand.boss) in bosses) return;
          (unknown[stand.boss] = unknown[stand.boss] || []).push({
            key: "stand:" + stand.windowSpell, ident: "stand " + stand.windowSpell, why: "boss " + stand.boss + " is not in the table",
          });
        });
      }
      const counts = {};
      COUNT_KEYS.forEach((key) => { counts[key] = 0; });
      bossResults.forEach((boss) => { COUNT_KEYS.forEach((key) => { counts[key] += boss.counts[key]; }); });
      const unknownBosses = Object.keys(unknown).map(Number).sort((a, b) => a - b).map((entry) => ({
        entry, extra: dedupeExtras(unknown[entry]),
      }));
      out.push({
        key: raid.key,
        mapId,
        name: raid.name,
        counts,
        bosses: bossResults,
        unknownBosses,
      });
    });
    return {
      raids: out,
      source: rowsRead ? (rows.source == null ? null : rows.source) : null,
      loadedAt: rowsRead ? (rows.loadedAt == null ? null : rows.loadedAt) : null,
      files: rowsRead && rows.files != null ? rows.files : null,
      truncated,
      truncatedAt: truncated && rows.truncatedAt != null && isFinite(Number(rows.truncatedAt)) ? Number(rows.truncatedAt) : null,
    };
  }

  // The fixture projection (data/boss/testdata/coverage_cases.json): the same shape both implementations produce.
  function project(result) {
    const out = {};
    (result.raids || []).forEach((raid) => {
      (raid.bosses || []).forEach((boss) => {
        const lines = {};
        boss.lines.forEach((line) => { lines[line.key] = { status: line.status, needs: Object.assign({}, line.needs) }; });
        const counts = {};
        COUNT_KEYS.forEach((key) => { counts[key] = boss.counts[key]; });
        out[String(boss.entry)] = {
          lines,
          extra: boss.extra.map((item) => item.key),
          warnings: boss.warnings.length,
          counts,
        };
      });
      (raid.unknownBosses || []).forEach((boss) => {
        const counts = {};
        COUNT_KEYS.forEach((key) => { counts[key] = 0; });
        out[String(boss.entry)] = {
          lines: {},
          extra: boss.extra.map((item) => item.key),
          warnings: 0,
          counts,
        };
      });
    });
    return out;
  }

  // ---- routes ----------------------------------------------------------------------------------------------------------

  function parseHash(hash) {
    const raw = String(hash == null ? "" : hash).replace(/^#\/?/, "");
    const cut = raw.indexOf("?");
    const path = cut < 0 ? raw : raw.slice(0, cut);
    const query = cut < 0 ? "" : raw.slice(cut + 1);
    const segs = path.split("/").filter(Boolean);
    if (!segs.length || segs[0] !== "coverage") return null;
    let raid = null;
    if (segs[1]) {
      try { raid = decodeURIComponent(segs[1]); } catch (e) { raid = segs[1]; }
    }
    const q = new URLSearchParams(query);
    const boss = parseInt(q.get("boss"), 10);
    return { raid, missing: q.get("missing") === "1", boss: isFinite(boss) && boss > 0 ? boss : null };
  }

  function hash(state) {
    const st = state || {};
    let out = "#/coverage";
    if (st.raid) out += "/" + encodeURIComponent(st.raid);
    const q = [];
    if (st.missing) q.push("missing=1");
    if (st.boss != null && st.boss !== "" && isFinite(Number(st.boss)) && Number(st.boss) > 0) q.push("boss=" + Number(st.boss));
    return q.length ? out + "?" + q.join("&") : out;
  }

  // ---- the panel's chips (§3.1) ----------------------------------------------------------------------------------------

  function glyph(status) {
    return GLYPH[status] || "?";
  }

  function titleOf(line) {
    const parts = [];
    const needs = line.needs || {};
    Object.keys(needs).forEach((need) => {
      const level = needs[need];
      let text = "";
      if (level === "ok") text = "ok: " + ((line.okNeeds || {})[need] || line.ok || "not needed");
      else if (level === "unknown") text = "rows not read";
      else if (level === "none") text = need === "classify" ? "the table does not say what it needs" : "no row";
      else {
        const hits = (line.answers || []).filter((answer) => answer.need === need).map((answer) => answer.text);
        text = hits.length ? hits.join("; ") : level;
      }
      parts.push(need + ": " + text);
    });
    if (!parts.length && line.status === "notneeded") parts.push("not needed");
    return parts.join("\n");
  }

  // { [sid]: {status, title} } for one boss's ability lines; an unknown boss gives {}.
  function forBoss(result, entry) {
    const out = {};
    const wanted = Number(entry);
    if (!result || !isFinite(wanted)) return out;
    (result.raids || []).forEach((raid) => {
      (raid.bosses || []).forEach((boss) => {
        if (boss.entry !== wanted) return;
        (boss.lines || []).forEach((line) => {
          if (line.kind !== "ability") return;
          out[line.key] = { status: line.status, title: titleOf(line) };
        });
      });
    });
    return out;
  }

  // ---- the raid view (§3.2) --------------------------------------------------------------------------------------------

  function countsText(counts) {
    const parts = ["answered", "partly", "missing", "notneeded"].map((key) => counts[key] + " " + (key === "notneeded" ? "not needed" : key));
    if (counts.unknown) parts.push(counts.unknown + " unknown");
    return parts.join(", ");
  }

  function countsBar(counts) {
    const segs = ["answered", "partly", "missing", "notneeded", "unknown"].map((key) => {
      if (!counts[key]) return "";
      return '<span class="cov-seg cov-seg-' + key + '" style="flex:' + counts[key] + '"></span>';
    }).join("");
    return '<span class="cov-bar" role="img" aria-label="' + esc(countsText(counts)) + '">' + segs + "</span>";
  }

  function findRaid(result, wanted) {
    const raids = result && result.raids ? result.raids : [];
    if (wanted == null || wanted === "") return raids[0] || null;
    const key = String(wanted);
    return raids.find((raid) => String(raid.key) === key || String(raid.mapId) === key) || null;
  }

  function hhmm(iso) {
    const at = iso ? new Date(iso) : null;
    if (!at || isNaN(at.getTime())) return "?";
    const h = String(at.getHours()), m = String(at.getMinutes());
    return (h.length < 2 ? "0" + h : h) + ":" + (m.length < 2 ? "0" + m : m);
  }

  function sourceText(result) {
    if (!result) return "";
    if (result.source === "world") {
      return "Tactics from the world DB, read " + esc(hhmm(result.loadedAt))
        + " (the module reads them at startup or on <code>.bot run reloadplans</code>)";
    }
    if (result.source === "migrations") return "Tactics from the migrations";
    return "Tactics rows not read";
  }

  function lineHtml(line, e) {
    const needs = Object.keys(line.needs || {}).map((need) => {
      const level = line.needs[need];
      return '<span class="cov-need cov-need-' + esc(level) + '">' + e(need) + " " + (NEED_GLYPH[level] || "?") + "</span>";
    }).join("");
    const okText = Object.keys(line.needs || {})
      .filter((need) => line.needs[need] === "ok")
      .map((need) => "ok: " + ((line.okNeeds || {})[need] || line.ok || "not needed"));
    const answers = (line.answers || []).map((answer) => e(answer.text)).concat(okText.map((text) => e(text))).join("; ");
    const name = line.kind === "add" ? e(line.name || ("add " + line.add)) : e(line.name);
    const sid = line.kind === "add" ? e("add " + line.add) : e(line.sid);
    const phase = line.phase ? '<span class="cov-phase">' + e(line.phase) + "</span>" : "";
    return '<li class="cov-line st-' + e(line.status) + '" data-key="' + e(line.key) + '">'
      + '<span class="cov-st cov-st-' + e(line.status) + '" title="' + e(titleOf(line)) + '" aria-label="coverage: ' + e(line.status) + '">' + glyph(line.status) + "</span>"
      + '<span class="cov-an">' + name + "</span>"
      + '<span class="cov-sid">' + sid + "</span>"
      + phase
      + '<span class="cov-needs">' + needs + "</span>"
      + (answers ? '<span class="cov-answers">' + answers + "</span>" : "")
      + "</li>";
  }

  function extraList(items, e) {
    if (!items || !items.length) return "";
    return '<div class="cov-extra"><b class="cov-lab">Rows without an ability</b><ul class="cov-extras">'
      + items.map((item) => '<li class="cov-extra-row" data-key="' + e(item.key) + '"><span class="cov-xid">' + e(item.ident)
        + '</span> <span class="cov-why">' + e(item.why) + "</span></li>").join("")
      + "</ul></div>";
  }

  function bossHtml(boss, st, e) {
    const wanted = st.boss != null && Number(st.boss) === boss.entry;
    const lines = boss.lines.filter((line) => !st.missing || (line.status !== "answered" && line.status !== "notneeded"));
    if (st.missing && !lines.length) return "";
    const open = wanted || boss.counts.missing > 0;
    const warnings = boss.warnings && boss.warnings.length
      ? '<p class="cov-warns">' + boss.warnings.map((text) => e(text)).join(" · ") + "</p>"
      : "";
    return '<details class="cov-boss" data-entry="' + e(boss.entry) + '"' + (open ? " open" : "") + ">"
      + '<summary class="cov-head"><span class="cov-name">' + e(boss.name) + '</span>'
      + '<span class="cov-entry">' + e(boss.entry) + "</span>" + countsBar(boss.counts)
      + '<span class="cov-counts">' + e(countsText(boss.counts)) + "</span></summary>"
      + '<div class="cov-body"><ul class="cov-lines">' + lines.map((line) => lineHtml(line, e)).join("") + "</ul>"
      + warnings + extraList(boss.extra, e) + "</div></details>";
  }

  // The whole view. state = {raid, missing, boss}; opts.esc may replace the escaper.
  function renderRaid(result, state, opts) {
    const o = opts || {}, e = typeof o.esc === "function" ? o.esc : esc;
    const st = { raid: state ? state.raid : null, missing: !!(state && state.missing), boss: state && state.boss != null ? Number(state.boss) : null };
    const raids = result && result.raids ? result.raids : [];
    const chips = '<div class="cov-chips">' + raids.map((raid) => {
      const cur = findRaid(result, st.raid) === raid;
      return '<a class="cov-chip' + (cur ? " on" : "") + '" href="' + e(hash({ raid: raid.key })) + '">'
        + e(raid.name) + " · " + raid.counts.missing + " missing</a>";
    }).join("") + "</div>";
    if (!raids.length) return '<p class="cov-empty">No raid in the boss table.</p>' + chips;
    const raid = findRaid(result, st.raid);
    if (!raid) {
      return chips + '<p class="cov-notice">No raid <b>' + e(st.raid) + "</b> in the boss table.</p>";
    }
    const banner = result.source == null
      ? '<p class="cov-ban">World database not reachable: answers from tactic and stand rows are unknown; code answers are shown.</p>'
      : "";
    const cutBanner = result.truncated
      ? '<p class="cov-ban">' + e("the server's tactic rows were cut off at "
        + (result.truncatedAt == null ? "its row cap" : fmtNum(result.truncatedAt))
        + " rows: coverage from the database is incomplete") + "</p>"
      : "";
    const summary = '<div class="cov-summary"><span class="cov-cname">' + e(raid.name) + '</span>'
      + countsBar(raid.counts)
      + '<span class="cov-counts">' + e(countsText(raid.counts)) + "</span>"
      + '<label class="cov-toggle"><input type="checkbox" id="cov-missing"' + (st.missing ? " checked" : "") + "> Missing only</label>"
      + '<button type="button" class="cov-reload" id="cov-reload">Reload</button>'
      + '<span class="cov-src">' + sourceText(result) + "</span></div>"
      + '<p class="cov-note">Coverage checks that an answer exists, not that it works: the live runs judge the bots in game.</p>';
    const cards = raid.bosses.map((boss) => bossHtml(boss, st, e)).join("");
    const unknown = (raid.unknownBosses || []).length
      ? '<div class="cov-unknown"><b class="cov-lab">Rows for bosses the table does not list</b>'
        + raid.unknownBosses.map((boss) => '<div class="cov-uboss" data-entry="' + e(boss.entry) + '">'
          + '<span class="cov-xid">' + e(boss.entry) + "</span>"
          + (boss.extra.length ? '<ul class="cov-extras">' + boss.extra.map((item) => '<li class="cov-extra-row" data-key="' + e(item.key)
            + '"><span class="cov-xid">' + e(item.ident) + '</span> <span class="cov-why">' + e(item.why) + "</span></li>").join("") + "</ul>" : "")
          + "</div>").join("")
        + "</div>"
      : "";
    return chips + cutBanner + banner + summary + cards + unknown;
  }

  const api = {
    compute, project, parseHash, hash, forBoss, renderRaid, glyph, titleOf,
    countsText, findRaid, _esc: esc,
    NEED_ORDER, COUNT_KEYS,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.Coverage = api;
})(typeof self !== "undefined" ? self : this);
