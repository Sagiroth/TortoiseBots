// Tortoise WoW Observability Dashboard
// State model: the WebSocket "snapshot" event is authoritative for the roster.
// Heartbeats update server gauges, "status" reports online/stale transitions,
// and a local watchdog marks the stream offline if pulses stop.
(function() {
  'use strict';

  // Fallback zone dictionary (loaded dynamically from /data/zone_maps.json)
  let ZONE_CONFIG = {
    12: { name: 'Elwynn Forest', map: 0, file: 'elwynn.webp' },
    14: { name: 'Durotar', map: 1, file: 'durotar.webp' },
    17: { name: 'The Barrens', map: 1, file: 'barrens.webp' },
    3277: { name: 'Warsong Gulch', map: 489, file: 'warsonggulch.webp' }
  };

  const OFFLINE_AFTER_MS = 10000;

  const state = {
    activeTab: 'dashboard',
    currentZoneId: 12,
    bots: [],
    anomalies: [],
    server: {
      online: false,
      stale: true,
      uptime: 0,
      diff: 0,
      humans: 0,
      bots: 0,
      states: { combat: 0, moving: 0, resting: 0, dead: 0, idle: 0 }
    },
    history: {
      t: [],
      bots: [],
      humans: [],
      diff: []
    },
    counts: [],
    showTrails: true,
    roleFilter: 'all',
    rosterClassFilter: 'all',
    rosterStatusFilter: 'all',
    rosterIssueOnly: false,
    rosterSort: { key: 'name', dir: 1 },
    selectedBotGuid: null,
    anomalyTypeFilter: 'all',
    anomalySeverityFilter: 'all',
    wsConnected: false,
    snapshotSeq: 0,
    lastHeartbeatAt: 0,
    lastSnapshotAt: 0,
    issues: { active: [], resolved: [], counts_by_type: {} },
    issueTypeFilter: 'all',
    issueDurationFilter: 0,
    issueHistory: { t: [], series: {} }
  };

  const ISSUE_TYPES = ['STUCK', 'DEAD_LONG', 'ACTION_LOOP', 'UNREACHABLE_TARGET'];
  const ISSUE_LABELS = { STUCK: 'Stuck', DEAD_LONG: 'Dead long', ACTION_LOOP: 'Action loop', UNREACHABLE_TARGET: 'Unreachable' };
  const ISSUE_COLORS = { STUCK: '#d29922', DEAD_LONG: '#8b949e', ACTION_LOOP: '#f85149', UNREACHABLE_TARGET: '#a371f7' };
  const ISSUE_HISTORY_MAX = 300;

  const HIST_MAX = 300; // 2s samples -> 10 minutes

  // WoW class colors (https://wowpedia.fandom.com/wiki/Class_colors)
  const CLASS_COLORS = {
    warrior: '#c79c6e', paladin: '#f58cba', hunter: '#abd473', rogue: '#fff569',
    priest: '#ffffff', shaman: '#0070de', mage: '#69ccf0', warlock: '#9482c9',
    druid: '#ff7d0a', unknown: '#8b949e'
  };

  function classColor(cls) {
    const key = String(cls || '').toLowerCase().trim();
    return CLASS_COLORS[key] || CLASS_COLORS.unknown;
  }

  // Power bar label follows the bot's resource: rage, energy, focus, mana.
  function powerLabel(b) {
    const t = (b && b.power_type) || 'power';
    return t.charAt(0).toUpperCase() + t.slice(1);
  }

  function hexToRgba(hex, alpha) {
    const h = hex.replace('#', '');
    const full = h.length === 3 ? h.split('').map(c => c + c).join('') : h;
    const n = parseInt(full, 16);
    return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${alpha})`;
  }

  // DOM Elements
  const el = {
    statusPill: document.getElementById('status-pill'),
    statusLabel: document.getElementById('status-label'),
    topBotsVal: document.getElementById('top-bots-val'),
    playersVal: document.getElementById('players-val'),
    metricBotsOnline: document.getElementById('metric-bots-online'),
    metricHumansOnline: document.getElementById('metric-humans-online'),
    metricUptime: document.getElementById('metric-uptime'),
    snapshotAgeVal: document.getElementById('snapshot-age-val'),
    gaugeTickVal: document.getElementById('gauge-tick-val'),
    gaugeTickBar: document.getElementById('gauge-tick-bar'),

    // Tabs & Navigation
    menuItems: document.querySelectorAll('.sidebar-menu .menu-item[data-tab]'),
    tabViews: document.querySelectorAll('.tab-view'),

    // Map
    zoneFilterInput: document.getElementById('zone-filter-input'),
    zoneFilterCount: document.getElementById('zone-filter-count'),
    zoneSelect: document.getElementById('zone-select'),
    mapImg: document.getElementById('map-img'),
    mapOverlay: document.getElementById('map-overlay'),
    mapCanvas: document.getElementById('map-canvas'),
    mapTooltip: document.getElementById('map-tooltip'),
    mapLegend: document.getElementById('map-legend'),
    toggleTrails: document.getElementById('toggle-trails'),
    botDrawer: document.getElementById('bot-drawer'),
    drawerContent: document.getElementById('drawer-content'),
    closeDrawer: document.getElementById('close-drawer'),

    // Roster
    roleFilter: document.getElementById('role-filter'),
    classFilter: document.getElementById('class-filter'),
    statusFilter: document.getElementById('status-filter'),
    issueOnlyFilter: document.getElementById('issue-only-filter'),
    botSearch: document.getElementById('bot-search'),
    rosterTable: document.getElementById('roster-table-body'),
    rosterCount: document.getElementById('roster-count'),

    // Incidents
    anomaliesTable: document.getElementById('anomalies-table-body'),
    anomaliesCount: document.getElementById('anomalies-count'),
    typeFilter: document.getElementById('anomaly-type-filter'),
    severityFilter: document.getElementById('anomaly-severity-filter'),
    clearAnomalies: document.getElementById('clear-anomalies'),

    // Issues
    issueActive: document.getElementById('issue-active'),
    issuePersistent: document.getElementById('issue-persistent'),
    issueWatch: document.getElementById('issue-watch'),
    issueResolved: document.getElementById('issue-resolved'),
    issueDeaths: document.getElementById('issue-deaths'),
    issuesTable: document.getElementById('issues-table-body'),
    issueTypeFilter: document.getElementById('issue-type-filter'),
    issueDurationFilter: document.getElementById('issue-duration-filter'),
    issuesResolved: document.getElementById('issues-resolved'),
    issueZones: document.getElementById('issue-zones'),
    issueChart: document.getElementById('issue-chart'),
    issueLegend: document.getElementById('issue-legend'),
    issuesCount: document.getElementById('issues-count'),
    metricIssues: document.getElementById('metric-issues'),
    metricIssuesSub: document.getElementById('metric-issues-sub'),

    // Dashboard composition
    classBreakdown: document.getElementById('class-breakdown'),
    roleTotals: document.getElementById('role-totals'),
    fleetHealth: document.getElementById('fleet-health'),
    zoneList: document.getElementById('zone-list'),

    // Chart & Console
    activityChart: document.getElementById('activity-chart'),
    tickChart: document.getElementById('tick-chart'),
    tickNow: document.getElementById('tick-now'),
    consoleBody: document.getElementById('console-body'),
    consoleRate: document.getElementById('console-rate'),
    refreshBtn: document.getElementById('refresh-btn')
  };

  // All dynamic values rendered via innerHTML must pass through this.
  function esc(value) {
    if (value === null || value === undefined) return '';
    return String(value)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function formatUptime(seconds) {
    if (!seconds) return '0m';
    const m = Math.floor(seconds / 60);
    const h = Math.floor(m / 60);
    const remM = m % 60;
    if (h > 0) return `${h}h ${remM}m`;
    return `${m}m`;
  }

  function getZoneName(zoneId) {
    if (zoneId === null || zoneId === undefined) return '-';
    const zone = ZONE_CONFIG[zoneId];
    return zone ? zone.name : `Zone ${zoneId}`;
  }

  function appendConsoleLog(time, tag, text, level = 'info') {
    if (!el.consoleBody) return;
    const line = document.createElement('div');
    line.className = 'log-line';
    const tagClass = level === 'warn' ? 'warn' : level === 'error' ? 'error' : '';
    // text may contain intentional markup from callers; all telemetry-derived
    // strings are escaped before they reach this point.
    line.innerHTML = `<span class="log-time">[${esc(time)}]</span> <span class="log-tag ${tagClass}">[${esc(tag)}]</span> ${text}`;
    el.consoleBody.appendChild(line);
    while (el.consoleBody.children.length > 200) {
      el.consoleBody.removeChild(el.consoleBody.firstChild);
    }
    el.consoleBody.scrollTop = el.consoleBody.scrollHeight;
  }

  // Sidebar Tab Navigation
  function switchTab(tab) {
    state.activeTab = tab;
    el.menuItems.forEach(m => m.classList.toggle('active', m.dataset.tab === tab));
    el.tabViews.forEach(v => {
      v.style.display = v.id === `tab-${tab}` ? 'block' : 'none';
    });
    if (tab === 'map') renderMap();
    if (tab === 'roster') renderRoster();
    if (tab === 'issues') renderIssues();
    if (tab === 'dashboard') {
      renderDashboardCharts();
      renderComposition();
      renderFleetHealth();
      renderMacroBar(state.server.states);
    }
  }

  el.menuItems.forEach(item => {
    item.addEventListener('click', (e) => {
      e.preventDefault();
      const tab = item.dataset.tab;
      if (tab) switchTab(tab);
    });
  });

  if (el.refreshBtn) {
    el.refreshBtn.addEventListener('click', () => {
      fetchBots(true);
      fetchAnomalies();
      fetchIssues(true);
    });
  }

  // Zone Selector & Filtering
  function populateZoneSelect(filter = '') {
    if (!el.zoneSelect) return;
    const q = filter.trim().toLowerCase();
    el.zoneSelect.innerHTML = '';
    const sorted = Object.entries(ZONE_CONFIG).sort((a, b) => a[1].name.localeCompare(b[1].name));
    const matching = sorted.filter(([id, z]) => !q || z.name.toLowerCase().includes(q));

    matching.forEach(([id, z]) => {
      const opt = document.createElement('option');
      opt.value = id;
      opt.textContent = z.name;
      if (parseInt(id, 10) === state.currentZoneId) opt.selected = true;
      el.zoneSelect.appendChild(opt);
    });

    if (el.zoneFilterCount) {
      el.zoneFilterCount.textContent = q ? `${matching.length}/${sorted.length} zones` : `${sorted.length} zones`;
    }

    if (matching.length > 0 && !matching.some(([id]) => parseInt(id, 10) === state.currentZoneId)) {
      const firstId = parseInt(matching[0][0], 10);
      state.currentZoneId = firstId;
      el.zoneSelect.value = firstId;
      loadZoneMap(firstId);
    }
  }

  function initZoneSelector() {
    fetch('/data/zone_maps.json')
      .then(r => r.json())
      .then(cfg => {
        if (!cfg || Object.keys(cfg).length === 0) return;
        ZONE_CONFIG = cfg;
        populateZoneSelect(el.zoneFilterInput ? el.zoneFilterInput.value : '');
        loadZoneMap(state.currentZoneId);
      })
      .catch(() => {});

    if (el.zoneFilterInput) {
      el.zoneFilterInput.addEventListener('input', (e) => populateZoneSelect(e.target.value));
    }

    if (el.zoneSelect) {
      el.zoneSelect.addEventListener('change', (e) => {
        const zid = parseInt(e.target.value, 10);
        state.currentZoneId = zid;
        loadZoneMap(zid);
      });
    }
  }

  function loadZoneMap(zoneId) {
    const zone = ZONE_CONFIG[zoneId];
    if (zone && el.mapImg) {
      el.mapImg.src = `/maps/${zone.file}`;
      renderMap();
    }
  }

  if (el.toggleTrails) {
    el.toggleTrails.addEventListener('change', (e) => {
      state.showTrails = e.target.checked;
      renderMap();
    });
  }

  // Chart helpers -----------------------------------------------------------

  function prepCanvas(canvas) {
    if (!canvas || !canvas.parentElement) return null;
    const w = canvas.parentElement.clientWidth;
    const h = canvas.parentElement.clientHeight;
    if (w <= 0 || h <= 0) return null;
    const dpr = window.devicePixelRatio || 1;
    if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
      canvas.width = Math.floor(w * dpr);
      canvas.height = Math.floor(h * dpr);
    }
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    return { ctx, w, h };
  }

  function drawGrid(ctx, w, h, padTop, padBottom, maxVal) {
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const y = padTop + (h - padTop - padBottom) * (i / 4);
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
    }
    ctx.fillStyle = '#6e7681';
    ctx.font = '10px "JetBrains Mono", monospace';
    ctx.fillText(String(Math.round(maxVal)), 3, padTop + 9);
    ctx.fillText('0', 3, h - padBottom - 2);
  }

  function drawTimeAxis(ctx, w, h) {
    ctx.fillStyle = '#6e7681';
    ctx.font = '10px "JetBrains Mono", monospace';
    ctx.fillText('10m ago', 3, h - 2);
    ctx.textAlign = 'right';
    ctx.fillText('now', w - 2, h - 2);
    ctx.textAlign = 'left';
  }

  // Population timeline (bots + players), one sample per heartbeat.
  function renderActivityChart() {
    const canvas = el.activityChart;
    const prepared = prepCanvas(canvas);
    if (!prepared) return;
    const { ctx, w, h } = prepared;

    const bots = state.history.bots;
    const humans = state.history.humans;
    const padTop = 12, padBottom = 16;

    if (bots.length === 0) {
      ctx.fillStyle = '#484f58';
      ctx.font = '12px Inter';
      ctx.fillText('Collecting telemetry...', 12, h / 2);
      drawTimeAxis(ctx, w, h);
      return;
    }

    const maxVal = Math.max(10, ...bots, ...humans) * 1.15;
    const yFor = v => padTop + (h - padTop - padBottom) * (1 - v / maxVal);
    const stepX = bots.length > 1 ? w / (bots.length - 1) : 0;
    const xFor = i => bots.length > 1 ? i * stepX : w / 2;

    drawGrid(ctx, w, h, padTop, padBottom, maxVal);

    // Bots: filled area + line
    if (bots.length > 1) {
      ctx.beginPath();
      ctx.moveTo(xFor(0), h - padBottom);
      bots.forEach((v, i) => ctx.lineTo(xFor(i), yFor(v)));
      ctx.lineTo(xFor(bots.length - 1), h - padBottom);
      ctx.closePath();
      const grad = ctx.createLinearGradient(0, 0, 0, h);
      grad.addColorStop(0, 'rgba(88, 166, 255, 0.28)');
      grad.addColorStop(1, 'rgba(88, 166, 255, 0.0)');
      ctx.fillStyle = grad;
      ctx.fill();
    }

    ctx.beginPath();
    bots.forEach((v, i) => {
      if (i === 0) ctx.moveTo(xFor(i), yFor(v));
      else ctx.lineTo(xFor(i), yFor(v));
    });
    ctx.strokeStyle = '#58a6ff';
    ctx.lineWidth = 2;
    ctx.stroke();

    // Players line
    ctx.beginPath();
    humans.forEach((v, i) => {
      if (i === 0) ctx.moveTo(xFor(i), yFor(v));
      else ctx.lineTo(xFor(i), yFor(v));
    });
    ctx.strokeStyle = '#2ea043';
    ctx.lineWidth = 2;
    ctx.stroke();

    if (bots.length === 1) {
      [[bots[0], '#58a6ff'], [humans[0], '#2ea043']].forEach(([v, color]) => {
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(xFor(0), yFor(v || 0), 3, 0, Math.PI * 2);
        ctx.fill();
      });
    }

    // End labels
    const last = bots.length - 1;
    ctx.font = '10px "JetBrains Mono", monospace';
    ctx.fillStyle = '#58a6ff';
    ctx.textAlign = 'right';
    ctx.fillText(String(bots[last]), w - 3, yFor(bots[last]) - 4);
    ctx.fillStyle = '#2ea043';
    ctx.fillText(String(humans[last]), w - 3, yFor(humans[last]) - 4);
    ctx.textAlign = 'left';

    drawTimeAxis(ctx, w, h);
  }

  // World tick timeline with nominal (50ms) and lag (100ms) references.
  function renderTickChart() {
    const canvas = el.tickChart;
    const prepared = prepCanvas(canvas);
    if (!prepared) return;
    const { ctx, w, h } = prepared;

    const series = state.history.diff;
    const padTop = 8, padBottom = 14;

    if (series.length === 0) {
      ctx.fillStyle = '#484f58';
      ctx.font = '11px Inter';
      ctx.fillText('Collecting telemetry...', 12, h / 2);
      return;
    }

    const maxVal = Math.max(100, ...series) * 1.1;
    const yFor = v => padTop + (h - padTop - padBottom) * (1 - v / maxVal);
    const stepX = series.length > 1 ? w / (series.length - 1) : 0;
    const xFor = i => series.length > 1 ? i * stepX : w / 2;

    drawGrid(ctx, w, h, padTop, padBottom, maxVal);

    ctx.setLineDash([4, 4]);
    ctx.strokeStyle = 'rgba(46, 160, 67, 0.5)';
    ctx.beginPath(); ctx.moveTo(0, yFor(50)); ctx.lineTo(w, yFor(50)); ctx.stroke();
    ctx.strokeStyle = 'rgba(248, 81, 73, 0.5)';
    ctx.beginPath(); ctx.moveTo(0, yFor(100)); ctx.lineTo(w, yFor(100)); ctx.stroke();
    ctx.setLineDash([]);

    ctx.beginPath();
    series.forEach((v, i) => {
      if (i === 0) ctx.moveTo(xFor(i), yFor(v));
      else ctx.lineTo(xFor(i), yFor(v));
    });
    ctx.strokeStyle = '#f0883e';
    ctx.lineWidth = 1.5;
    ctx.stroke();

    if (series.length === 1) {
      ctx.fillStyle = '#f0883e';
      ctx.beginPath();
      ctx.arc(xFor(0), yFor(series[0]), 3, 0, Math.PI * 2);
      ctx.fill();
    }

    const last = series[series.length - 1];
    if (el.tickNow) {
      el.tickNow.textContent = `${last} ms (nominal 50, lag >100)`;
      el.tickNow.style.color = last > 100 ? '#f85149' : last > 70 ? '#d29922' : 'var(--text-muted)';
    }
  }

  function renderDashboardCharts() {
    renderActivityChart();
    renderTickChart();
  }

  // Bots by class (heartbeat counts), with role totals.
  function renderComposition() {
    if (!el.classBreakdown) return;
    const counts = state.counts || [];
    if (counts.length === 0) {
      el.classBreakdown.innerHTML = '<div class="empty-hint">Waiting for bot telemetry...</div>';
      if (el.roleTotals) el.roleTotals.innerHTML = '';
      return;
    }

    const byClass = new Map();
    const byRole = { tank: 0, healer: 0, dps: 0 };
    counts.forEach(c => {
      byClass.set(c.class, (byClass.get(c.class) || 0) + c.count);
      byRole[c.role] = (byRole[c.role] || 0) + c.count;
    });

    const entries = [...byClass.entries()].sort((a, b) => b[1] - a[1]);
    const max = entries[0][1] || 1;

    el.classBreakdown.innerHTML = entries.map(([cls, n]) => `
      <div class="comp-row">
        <span class="comp-name">${esc(cls)}</span>
        <span class="comp-bar-bg"><span class="comp-bar" style="width: ${Math.round((n / max) * 100)}%; background: ${classColor(cls)};"></span></span>
        <span class="comp-count">${n}</span>
      </div>`).join('');

    if (el.roleTotals) {
      el.roleTotals.innerHTML = ['tank', 'healer', 'dps'].map(role => {
        const badge = role === 'tank' ? 'badge-info' : role === 'healer' ? 'badge-success' : 'badge-error';
        return `<span class="badge ${badge}">${byRole[role] || 0} ${role.toUpperCase()}</span>`;
      }).join('');
    }
  }

  // Fleet health + zone distribution from the authoritative roster.
  function renderFleetHealth() {
    if (!el.fleetHealth) return;

    const bots = state.bots;
    if (bots.length === 0) {
      el.fleetHealth.innerHTML = '<div class="empty-hint">Waiting for bot roster...</div>';
      if (el.zoneList) el.zoneList.innerHTML = '';
      return;
    }

    let dead = 0, low = 0, inCombat = 0;
    const zones = new Map();
    bots.forEach(b => {
      const pct = b.max_hp ? b.hp / b.max_hp : 1;
      if (b.state === 'dead' || b.hp === 0) dead++;
      else if (pct < 0.35) low++;
      if (b.state === 'combat') inCombat++;
      zones.set(b.zone, (zones.get(b.zone) || 0) + 1);
    });

    el.fleetHealth.innerHTML = `
      <div class="role-row" style="margin-top: 0;">
        <span class="badge badge-error">${inCombat} IN COMBAT</span>
        <span class="badge badge-warn">${low} LOW HP</span>
        <span class="badge badge-info">${dead} DEAD</span>
      </div>`;

    if (el.zoneList) {
      const top = [...zones.entries()].sort((a, b) => b[1] - a[1]).slice(0, 5);
      el.zoneList.innerHTML = top.map(([zid, n]) =>
        `<div class="zone-row" data-zone="${zid}"><span>${esc(getZoneName(zid))}</span><span class="comp-count">${n}</span></div>`
      ).join('');
      el.zoneList.querySelectorAll('.zone-row').forEach(row => {
        row.addEventListener('click', () => openZone(parseInt(row.dataset.zone, 10)));
      });
    }
  }

  function openZone(zoneId) {
    if (ZONE_CONFIG[zoneId]) {
      state.currentZoneId = zoneId;
      if (el.zoneSelect) el.zoneSelect.value = zoneId;
      loadZoneMap(zoneId);
    }
    switchTab('map');
  }

  function renderMacroBar(r) {
    if (!r) return;
    ['combat', 'moving', 'resting', 'idle', 'dead'].forEach(key => {
      const pct = Math.round((r[key] || 0) * 100);
      const bar = document.getElementById(`macro-bar-${key}`);
      const txt = document.getElementById(`macro-pct-${key}`);
      if (bar) bar.style.width = `${pct}%`;
      if (txt) txt.textContent = `${pct}%`;
    });
  }

  function updateSnapshotAge() {
    if (!el.snapshotAgeVal) return;
    if (!state.lastSnapshotAt) {
      el.snapshotAgeVal.textContent = '–';
      el.snapshotAgeVal.className = 'metric-big-num snapshot-offline';
      return;
    }
    const age = (Date.now() - state.lastSnapshotAt) / 1000;
    el.snapshotAgeVal.textContent = age < 10 ? `${age.toFixed(1)}s` : `${Math.round(age)}s`;
    el.snapshotAgeVal.className = 'metric-big-num ' +
      (age < 6 ? 'snapshot-fresh' : age < 10 ? 'snapshot-stale' : 'snapshot-offline');
  }

  function pushHistory() {
    const s = state.server;
    state.history.t.push(Date.now());
    state.history.bots.push(s.online ? s.bots : 0);
    state.history.humans.push(s.online ? s.humans : 0);
    state.history.diff.push(s.diff || 0);
    if (state.history.t.length > HIST_MAX) {
      state.history.t.shift();
      state.history.bots.shift();
      state.history.humans.shift();
      state.history.diff.shift();
    }
  }

  function setStatusPill() {
    if (!el.statusPill) return;
    const s = state.server;
    let cls = 'status-pill offline';
    if (s.online && !s.stale) {
      cls = 'status-pill online';
    } else if (s.online) {
      cls = 'status-pill stale';
    }
    el.statusPill.className = cls;
    if (el.statusLabel) {
      el.statusLabel.innerHTML = `Tortoise · ${esc(s.online ? (s.stale ? 'snapshot stale' : 'running') : 'offline')} · <strong id="top-bots-val">${s.bots}</strong> Bots`;
      el.topBotsVal = document.getElementById('top-bots-val');
    } else if (el.topBotsVal) {
      el.topBotsVal.textContent = s.bots;
    }
  }

  function updateDashboardMetrics() {
    const s = state.server;
    // While the server is offline the last count is history, not live state.
    const botCount = s.online ? s.bots : 0;

    if (el.playersVal) el.playersVal.textContent = s.online ? s.humans : 0;
    if (el.metricBotsOnline) el.metricBotsOnline.textContent = botCount;
    if (el.metricHumansOnline) el.metricHumansOnline.textContent = s.online ? s.humans : 0;
    if (el.metricUptime) el.metricUptime.textContent = s.online ? formatUptime(s.uptime) : '0m';

    const diff = s.diff || 0;
    if (el.gaugeTickVal) el.gaugeTickVal.textContent = s.online ? `${diff}ms` : '–';
    if (el.gaugeTickBar) {
      const pct = Math.min(1, diff / 200);
      const circumference = 188.5;
      el.gaugeTickBar.style.strokeDashoffset = circumference - (circumference * pct);
      el.gaugeTickBar.style.stroke = diff > 100 ? '#f85149' : diff > 70 ? '#d29922' : '#2ea043';
    }

    updateSnapshotAge();
    setStatusPill();
    if (state.activeTab === 'dashboard') renderMacroBar(s.states);
  }

  // 2D Map Rendering
  function renderMap() {
    if (!el.mapOverlay) return;
    el.mapOverlay.innerHTML = '';

    const canvas = el.mapCanvas;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const rect = canvas.getBoundingClientRect();
    if (canvas.width !== rect.width || canvas.height !== rect.height) {
      canvas.width = rect.width;
      canvas.height = rect.height;
    }
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    const zoneBots = state.bots.filter(b => {
      if (b.zone !== state.currentZoneId || !b.projected) return false;
      if (state.roleFilter !== 'all' && b.role !== state.roleFilter) return false;
      return true;
    });

    if (state.showTrails) {
      zoneBots.forEach(b => {
        if (!b.trail || b.trail.length < 2) return;
        ctx.beginPath();
        b.trail.forEach((pt, idx) => {
          const px = (pt.pct_x / 100) * canvas.width;
          const py = (pt.pct_y / 100) * canvas.height;
          if (idx === 0) ctx.moveTo(px, py);
          else ctx.lineTo(px, py);
        });
        ctx.strokeStyle = hexToRgba(classColor(b.class), 0.45);
        ctx.lineWidth = 2;
        ctx.stroke();
      });
    }

    const issuesByGuid = issueSet();
    zoneBots.forEach(b => {
      const issue = issuesByGuid[b.guid];
      const dot = document.createElement('div');
      dot.className = 'bot-dot';
      if (issue) dot.classList.add(issue.severity === 'persistent' ? 'issue-persistent' : 'issue-watch');
      if (b.guid === state.selectedBotGuid) dot.classList.add('selected');
      dot.style.left = `${b.pct_x}%`;
      dot.style.top = `${b.pct_y}%`;
      dot.style.backgroundColor = classColor(b.class);
      const deg = (b.o || 0) * (180 / Math.PI);
      dot.style.transform = `translate(-50%, -50%) rotate(${-deg}deg)`;

      dot.addEventListener('mouseenter', (e) => {
        if (!el.mapTooltip) return;
        const issueLine = issue
          ? `<br><span style="color: var(--accent-red);">Issue:</span> ${esc(ISSUE_LABELS[issue.type] || issue.type)} (${esc(fmtDuration(issue.duration_sec))})`
          : '';
        el.mapTooltip.innerHTML = `
          <strong style="color: #fff;">${esc(b.name)}</strong> (${esc(b.class)} Lvl ${esc(b.level)})<br>
          <span style="color: var(--text-muted);">Role:</span> ${esc((b.role || '').toUpperCase())}<br>
          <span style="color: var(--text-muted);">Status:</span> ${esc(b.state || 'idle')}<br>
          <span style="color: var(--text-muted);">Zone:</span> ${esc(getZoneName(b.zone))}<br>
          <span style="color: var(--text-muted);">Target:</span> ${esc(b.target || 'None')}
          ${issueLine}
        `;
        el.mapTooltip.style.display = 'block';

        // Keep the tooltip inside the viewport instead of clipping at the edge.
        const pad = 14;
        const rect = el.mapTooltip.getBoundingClientRect();
        let left = e.clientX + pad;
        let top = e.clientY + pad;
        if (left + rect.width > window.innerWidth - 6)
          left = e.clientX - rect.width - pad;
        if (top + rect.height > window.innerHeight - 6)
          top = e.clientY - rect.height - pad;
        el.mapTooltip.style.left = `${Math.max(6, left)}px`;
        el.mapTooltip.style.top = `${Math.max(6, top)}px`;
      });

      dot.addEventListener('mouseleave', () => {
        if (el.mapTooltip) el.mapTooltip.style.display = 'none';
      });

      dot.addEventListener('click', () => selectBot(b.guid));
      el.mapOverlay.appendChild(dot);
    });

    renderMapLegend(zoneBots);
  }

  function renderMapLegend(zoneBots) {
    if (!el.mapLegend) return;
    const present = [...new Set(zoneBots.map(b => b.class))].sort();
    if (present.length === 0) {
      el.mapLegend.innerHTML = '<span style="color: var(--text-muted); font-size: 0.75rem;">No bots in this zone</span>';
      return;
    }
    el.mapLegend.innerHTML = present.map(cls =>
      `<span><i style="background: ${classColor(cls)};"></i>${esc(cls.charAt(0).toUpperCase() + cls.slice(1))}</span>`
    ).join('');
  }

  function selectBot(guid) {
    state.selectedBotGuid = guid;
    const b = state.bots.find(x => x.guid === guid);
    if (!b || !el.botDrawer || !el.drawerContent) {
      if (el.botDrawer) el.botDrawer.style.display = 'none';
      return;
    }

    el.botDrawer.style.display = 'block';
    const hpPct = b.max_hp ? Math.round((b.hp / b.max_hp) * 100) : 100;
    const powerPct = b.max_power ? Math.round((b.power / b.max_power) * 100) : 0;

    el.drawerContent.innerHTML = `
      <div style="font-size: 1.15rem; font-weight: 700; color: #fff; margin-bottom: 4px;">${esc(b.name)}</div>
      <div style="font-size: 0.8rem; color: var(--text-muted); margin-bottom: 16px;">
        Level ${esc(b.level)} ${esc(b.class)} · <span class="badge badge-info">${esc((b.role || '').toUpperCase())}</span>
      </div>

      <div style="margin-bottom: 14px;">
        <div style="display: flex; justify-content: space-between; font-size: 0.75rem; margin-bottom: 4px;">
          <span>Health</span><strong>${esc(b.hp)} / ${esc(b.max_hp)} (${hpPct}%)</strong>
        </div>
        <div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${hpPct}%; background: var(--accent-green-bright);"></div></div>
      </div>

      <div style="margin-bottom: 16px;">
        <div style="display: flex; justify-content: space-between; font-size: 0.75rem; margin-bottom: 4px;">
          <span>${esc(powerLabel(b))}</span><strong>${esc(b.power)} / ${esc(b.max_power)}</strong>
        </div>
        <div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${powerPct}%; background: var(--accent-blue-bright);"></div></div>
      </div>

      <div style="background: rgba(255, 255, 255, 0.03); border: 1px solid var(--border-color); border-radius: 6px; padding: 10px; font-size: 0.8rem; display: flex; flex-direction: column; gap: 6px;">
        <div><span style="color: var(--text-muted);">Status:</span> <strong>${esc(b.state)}</strong></div>
        <div><span style="color: var(--text-muted);">Target:</span> <strong style="color: #f85149;">${esc(b.target || 'None')}</strong></div>
        <div><span style="color: var(--text-muted);">Last action:</span> <span class="mono" style="font-size: 0.75rem;">${esc(b.last_action || '-')}</span></div>
        <div><span style="color: var(--text-muted);">Trigger:</span> <span class="mono" style="font-size: 0.75rem;">${esc(b.last_trigger || '-')}</span></div>
        <div><span style="color: var(--text-muted);">Strategy:</span> <span class="mono" style="font-size: 0.75rem;">${esc(b.strategy || 'default')}</span></div>
      </div>
      ${(state.issues.active.filter(i => i.guid === b.guid).length > 0) ? `
      <div style="margin-top: 14px;">
        <div class="section-label" style="margin: 0 0 8px;">ACTIVE ISSUES</div>
        ${state.issues.active.filter(i => i.guid === b.guid).map(i => `
          <div class="issue-chip ${i.severity}">
            <span>${esc(ISSUE_LABELS[i.type] || i.type)}</span>
            <span class="mono">${esc(fmtDuration(i.duration_sec))}</span>
          </div>`).join('')}
      </div>` : ''}
    `;
  }

  if (el.closeDrawer) {
    el.closeDrawer.addEventListener('click', () => {
      if (el.botDrawer) el.botDrawer.style.display = 'none';
      state.selectedBotGuid = null;
    });
  }

  // Roster Table
  function populateClassFilter() {
    if (!el.classFilter) return;
    const classes = [...new Set(state.bots.map(b => b.class))].filter(Boolean).sort();
    const current = state.rosterClassFilter;
    el.classFilter.innerHTML = '<option value="all">All Classes</option>' +
      classes.map(c => `<option value="${esc(c)}">${esc(c.charAt(0).toUpperCase() + c.slice(1))}</option>`).join('');
    el.classFilter.value = classes.includes(current) ? current : 'all';
    state.rosterClassFilter = el.classFilter.value;
  }

  function rosterSortValue(b, key) {
    switch (key) {
      case 'level': return b.level || 0;
      case 'hp': return b.max_hp ? b.hp / b.max_hp : 0;
      case 'power': return b.max_power ? b.power / b.max_power : 0;
      case 'class': return (b.class || '').toLowerCase();
      case 'role': return (b.role || '').toLowerCase();
      case 'state': return (b.state || '').toLowerCase();
      case 'zone': return getZoneName(b.zone).toLowerCase();
      default: return (b.name || '').toLowerCase();
    }
  }

  function updateRosterSortIndicators() {
    document.querySelectorAll('#tab-roster th.sortable').forEach(th => {
      th.classList.toggle('sort-active', th.dataset.sort === state.rosterSort.key);
      const base = th.textContent.replace(/[ ▲▼]+$/, '');
      const arrow = th.dataset.sort === state.rosterSort.key ? (state.rosterSort.dir > 0 ? ' ▲' : ' ▼') : '';
      th.textContent = base + arrow;
    });
  }

  function renderRoster() {
    if (!el.rosterTable) return;
    const query = (el.botSearch ? el.botSearch.value : '').toLowerCase().trim();
    const issuesByGuid = issueSet();

    const filtered = state.bots.filter(b => {
      if (state.roleFilter !== 'all' && b.role !== state.roleFilter) return false;
      if (state.rosterClassFilter !== 'all' && b.class !== state.rosterClassFilter) return false;
      if (state.rosterStatusFilter !== 'all' && b.state !== state.rosterStatusFilter) return false;
      if (state.rosterIssueOnly && !issuesByGuid[b.guid]) return false;
      if (query && !b.name.toLowerCase().includes(query) && !b.class.toLowerCase().includes(query)) return false;
      return true;
    });

    const key = state.rosterSort.key;
    const dir = state.rosterSort.dir;
    filtered.sort((a, b) => {
      const av = rosterSortValue(a, key), bv = rosterSortValue(b, key);
      if (av < bv) return -1 * dir;
      if (av > bv) return 1 * dir;
      return a.name.localeCompare(b.name);
    });

    if (el.rosterCount) el.rosterCount.textContent = `· ${filtered.length}/${state.bots.length}`;
    updateRosterSortIndicators();

    if (filtered.length === 0) {
      el.rosterTable.innerHTML = `<tr><td colspan="9" style="text-align: center; color: var(--text-muted); padding: 24px;">No matching bots.</td></tr>`;
      return;
    }

    el.rosterTable.innerHTML = '';
    filtered.forEach(b => {
      const tr = document.createElement('tr');
      const hpPct = b.max_hp ? Math.round((b.hp / b.max_hp) * 100) : 100;
      const powerPct = b.max_power ? Math.round((b.power / b.max_power) * 100) : 0;
      const roleBadge = b.role === 'tank' ? 'badge-info' : b.role === 'healer' ? 'badge-success' : 'badge-error';
      const issue = issuesByGuid[b.guid];
      const issueBadge = issue
        ? ` <span class="badge ${issue.severity === 'persistent' ? 'badge-error' : 'badge-warn'}" title="${esc(ISSUE_LABELS[issue.type] || issue.type)}">${esc(fmtDuration(issue.duration_sec))}</span>`
        : '';

      tr.innerHTML = `
        <td style="font-weight: 600; cursor: pointer; color: #58a6ff;" data-guid="${esc(b.guid)}">${esc(b.name)}${issueBadge}</td>
        <td>${esc(b.class)}</td>
        <td><span class="badge ${roleBadge}">${esc((b.role || '').toUpperCase())}</span></td>
        <td>${esc(b.level)}</td>
        <td style="width: 130px;">
          <div style="font-size: 0.7rem; margin-bottom: 2px;">${esc(b.hp)}/${esc(b.max_hp)} (${hpPct}%)</div>
          <div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${hpPct}%; background: var(--accent-green-bright);"></div></div>
        </td>
        <td style="width: 140px;">
          <div style="font-size: 0.7rem; margin-bottom: 2px;">${esc(powerLabel(b))} ${esc(b.power)}/${esc(b.max_power)} (${powerPct}%)</div>
          <div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${powerPct}%; background: var(--accent-blue-bright);"></div></div>
        </td>
        <td><span class="badge ${b.state === 'combat' ? 'badge-error' : b.state === 'dead' ? 'badge-warn' : 'badge-info'}">${esc(b.state || 'idle')}</span></td>
        <td style="color: #f85149;">${esc(b.target || '-')}</td>
        <td class="mono" style="font-size: 0.8rem;">${esc(getZoneName(b.zone))}</td>
      `;
      tr.querySelector('td[data-guid]').addEventListener('click', () => selectBot(b.guid));
      el.rosterTable.appendChild(tr);
    });
  }

  if (el.botSearch) {
    el.botSearch.addEventListener('input', () => renderRoster());
  }

  if (el.roleFilter) {
    el.roleFilter.addEventListener('change', (e) => {
      state.roleFilter = e.target.value;
      renderRoster();
      renderMap();
    });
  }

  if (el.classFilter) {
    el.classFilter.addEventListener('change', (e) => {
      state.rosterClassFilter = e.target.value;
      renderRoster();
    });
  }

  if (el.statusFilter) {
    el.statusFilter.addEventListener('change', (e) => {
      state.rosterStatusFilter = e.target.value;
      renderRoster();
    });
  }

  if (el.issueOnlyFilter) {
    el.issueOnlyFilter.addEventListener('change', (e) => {
      state.rosterIssueOnly = e.target.checked;
      renderRoster();
    });
  }

  document.querySelectorAll('#tab-roster th.sortable').forEach(th => {
    th.addEventListener('click', () => {
      const key = th.dataset.sort;
      if (state.rosterSort.key === key) {
        state.rosterSort.dir *= -1;
      } else {
        state.rosterSort.key = key;
        state.rosterSort.dir = 1;
      }
      renderRoster();
    });
  });

  // Incidents
  function fetchAnomalies() {
    fetch('/api/v1/anomalies')
      .then(r => r.json())
      .then(data => {
        state.anomalies = Array.isArray(data) ? data : [];
        renderAnomalies();
      })
      .catch(() => {});
  }

  function renderAnomalies() {
    if (!el.anomaliesTable) return;
    if (el.anomaliesCount) el.anomaliesCount.textContent = state.anomalies.length;

    const filtered = state.anomalies.filter(a => {
      const sev = (a.severity || '').toLowerCase();
      if (state.anomalyTypeFilter !== 'all' && a.type !== state.anomalyTypeFilter) return false;
      if (state.anomalySeverityFilter !== 'all' && sev !== state.anomalySeverityFilter) return false;
      return true;
    });

    if (filtered.length === 0) {
      el.anomaliesTable.innerHTML = `<tr><td colspan="7" style="text-align: center; color: var(--text-muted); padding: 24px;">No incidents recorded.</td></tr>`;
      return;
    }

    // Newest first, independent of the order the server returned them in.
    const ordered = filtered.slice().sort((a, b) => (b.ts || 0) - (a.ts || 0));

    el.anomaliesTable.innerHTML = '';
    ordered.forEach(a => {
      const tr = document.createElement('tr');
      const timeStr = a.time_str || (a.ts ? new Date(a.ts * 1000).toLocaleTimeString() : '-');
      const sev = (a.severity || 'info').toLowerCase();
      const sevBadge = sev === 'error' ? 'badge-error' : sev === 'warn' ? 'badge-warn' : 'badge-info';

      tr.innerHTML = `
        <td class="mono" style="font-size: 0.75rem; color: var(--text-muted);">${esc(timeStr)}</td>
        <td><span class="badge ${sevBadge}">${esc(sev.toUpperCase())}</span></td>
        <td class="mono" style="font-size: 0.8rem; font-weight: 600;">${esc(a.type)}</td>
        <td style="color: #fff; font-weight: 600;">${esc(a.bot || '-')}</td>
        <td class="mono" style="font-size: 0.75rem;">${esc(getZoneName(a.zone))}</td>
        <td style="color: var(--text-muted);">${esc(a.details || a.last_action || '-')}</td>
        <td style="color: #f85149;">${esc(a.target || '-')}</td>
      `;
      el.anomaliesTable.appendChild(tr);
    });
  }

  if (el.typeFilter) {
    el.typeFilter.addEventListener('change', (e) => {
      state.anomalyTypeFilter = e.target.value;
      renderAnomalies();
    });
  }
  if (el.severityFilter) {
    el.severityFilter.addEventListener('change', (e) => {
      state.anomalySeverityFilter = e.target.value;
      renderAnomalies();
    });
  }
  if (el.clearAnomalies) {
    el.clearAnomalies.addEventListener('click', () => {
      fetch('/api/v1/anomalies', { method: 'DELETE' })
        .then(() => {
          state.anomalies = [];
          renderAnomalies();
        })
        .catch(() => {});
    });
  }

  function fetchIssues(force = false) {
    if (state.wsConnected && !force) return;
    fetch('/api/v1/issues')
      .then(r => r.json())
      .then(data => applyIssues(data))
      .catch(() => {});
  }

  // REST fallback used by the Refresh button and while the socket is down.
  function fetchBots(force = false) {
    if (state.wsConnected && !force) return;
    fetch('/api/v1/bots')
      .then(r => r.json())
      .then(data => {
        if (!Array.isArray(data)) return;
        state.bots = data;
        populateClassFilter();
        updateDashboardMetrics();
        if (state.activeTab === 'map') renderMap();
        if (state.activeTab === 'roster') renderRoster();
        if (state.activeTab === 'dashboard') renderFleetHealth();
      })
      .catch(() => {});
  }

  function applyServerStatus(s) {
    if (!s) return;
    state.server.online = !!s.online;
    state.server.stale = !!s.stale;
    state.server.uptime = s.uptime || 0;
    state.server.diff = s.diff || 0;
    state.server.humans = s.humans || 0;
    state.server.bots = s.bots || 0;
    if (s.states) state.server.states = s.states;
  }

  // WebSocket Live Streaming
  let reconnectTimer = null;

  function initWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/api/v1/stream`;
    const ws = new WebSocket(wsUrl);

    ws.onopen = () => {
      state.wsConnected = true;
      if (el.consoleRate) el.consoleRate.innerHTML = '● connected';
      appendConsoleLog(new Date().toLocaleTimeString(), 'ws', 'Connected to live telemetry stream.');
    };

    ws.onmessage = (event) => {
      try {
        handleStreamMessage(JSON.parse(event.data));
      } catch (e) {}
    };

    ws.onclose = () => {
      state.wsConnected = false;
      if (el.consoleRate) el.consoleRate.innerHTML = '<span style="color: #f85149;">● disconnected</span>';
      if (reconnectTimer) clearTimeout(reconnectTimer);
      reconnectTimer = setTimeout(initWebSocket, 3000);
    };
  }

  function handleStreamMessage(msg) {
    const time = new Date().toLocaleTimeString();

    if (msg.event === 'snapshot') {
      const data = msg.data;
      if (!data) return;
      if (typeof data.seq === 'number' && data.seq < state.snapshotSeq) return;
      state.snapshotSeq = data.seq || 0;

      const prevCount = state.bots.length;
      state.bots = Array.isArray(data.bots) ? data.bots : [];
      state.lastSnapshotAt = Date.now();
      applyServerStatus(data.server);
      applyIssues(data.issues);
      updateDashboardMetrics();

      // Keep a selected bot's drawer fresh; close it if the bot departed.
      if (state.selectedBotGuid !== null) selectBot(state.selectedBotGuid);

      if (state.activeTab === 'map') renderMap();
      if (state.activeTab === 'roster') renderRoster();
      if (state.activeTab === 'dashboard') renderFleetHealth();

      if (state.bots.length !== prevCount) {
        populateClassFilter();
        appendConsoleLog(time, 'roster', `Snapshot #${esc(state.snapshotSeq)}: ${state.bots.length} bots active.`);
      }
    } else if (msg.event === 'heartbeat') {
      const d = msg.data;
      if (!d) return;
      state.lastHeartbeatAt = Date.now();
      state.server.online = true;
      state.server.stale = false;
      state.server.uptime = d.uptime || 0;
      state.server.diff = d.diff || 0;
      state.server.humans = d.humans || 0;
      state.server.bots = d.bots || 0;
      if (d.states) state.server.states = d.states;
      if (Array.isArray(d.counts)) state.counts = d.counts;
      pushHistory();
      updateDashboardMetrics();
      if (state.activeTab === 'dashboard') {
        renderDashboardCharts();
        renderComposition();
      }
      // Heartbeats arrive every 2s; log sparsely to keep the console useful.
      if (!handleStreamMessage._lastPulse || Date.now() - handleStreamMessage._lastPulse > 30000) {
        handleStreamMessage._lastPulse = Date.now();
        appendConsoleLog(time, 'pulse', `diff=${esc(d.diff)}ms, players=${esc(d.humans)}, bots=${esc(d.bots)}`);
      }
    } else if (msg.event === 'status') {
      applyServerStatus(msg.data);
      updateDashboardMetrics();
    } else if (msg.event === 'anomaly') {
      const a = msg.data;
      if (a) {
        state.anomalies.push(a);
        renderAnomalies();
        if (state.activeTab === 'issues') renderIssues();
        const sev = (a.severity || 'info').toLowerCase();
        appendConsoleLog(time, a.type, `<span class="log-bot">${esc(a.bot || 'Bot')}</span>: ${esc(a.details || a.last_action || '')}`, sev);
      }
    }
  }

  // ---- Persistent issues --------------------------------------------------

  function fmtDuration(sec) {
    sec = Math.max(0, Math.round(sec || 0));
    if (sec < 60) return `${sec}s`;
    const m = Math.floor(sec / 60);
    if (m < 60) return `${m}m ${String(sec % 60).padStart(2, '0')}s`;
    return `${Math.floor(m / 60)}h ${String(m % 60).padStart(2, '0')}m`;
  }

  function issueSet() {
    const set = {};
    state.issues.active.forEach(i => { set[i.guid] = i; });
    return set;
  }

  function applyIssues(issues) {
    if (!issues) return;
    state.issues = {
      active: Array.isArray(issues.active) ? issues.active : [],
      resolved: Array.isArray(issues.resolved) ? issues.resolved : [],
      counts_by_type: issues.counts_by_type || {}
    };
    pushIssueHistory();
    updateIssueBadges();
    if (state.activeTab === 'issues') renderIssues();
    if (state.activeTab === 'map') renderMap();
    if (state.activeTab === 'roster') renderRoster();
    if (state.activeTab === 'dashboard') updateDashboardMetrics();
  }

  function pushIssueHistory() {
    state.issueHistory.t.push(Date.now());
    ISSUE_TYPES.forEach(t => {
      if (!state.issueHistory.series[t]) state.issueHistory.series[t] = [];
      state.issueHistory.series[t].push(state.issues.counts_by_type[t] || 0);
    });
    if (state.issueHistory.t.length > ISSUE_HISTORY_MAX) {
      state.issueHistory.t.shift();
      ISSUE_TYPES.forEach(t => state.issueHistory.series[t].shift());
    }
  }

  function updateIssueBadges() {
    const active = state.issues.active.length;
    const persistent = state.issues.active.filter(i => i.severity === 'persistent').length;
    if (el.issuesCount) {
      el.issuesCount.textContent = active;
      el.issuesCount.className = 'badge ' + (persistent > 0 ? 'badge-error' : active > 0 ? 'badge-warn' : 'badge-info');
    }
    if (el.metricIssues) {
      el.metricIssues.textContent = active;
      el.metricIssues.style.color = persistent > 0 ? '#f85149' : active > 0 ? '#d29922' : '#fff';
    }
    if (el.metricIssuesSub) el.metricIssuesSub.textContent = `${persistent} persistent`;
  }

  function renderIssues() {
    const active = state.issues.active;
    if (el.issueActive) el.issueActive.textContent = active.length;
    if (el.issuePersistent) el.issuePersistent.textContent = active.filter(i => i.severity === 'persistent').length;
    if (el.issueWatch) el.issueWatch.textContent = active.filter(i => i.severity === 'watch').length;
    if (el.issueResolved) el.issueResolved.textContent = state.issues.resolved.length;
    if (el.issueDeaths) el.issueDeaths.textContent = state.anomalies.filter(a => a.type === 'BOT_DEATH').length;

    renderIssueTable();
    renderIssueZones();
    renderIssueChart();
    renderResolvedList();
  }

  function renderIssueTable() {
    if (!el.issuesTable) return;
    const minDur = state.issueDurationFilter;
    const filtered = state.issues.active
      .filter(i => (state.issueTypeFilter === 'all' || i.type === state.issueTypeFilter) && i.duration_sec >= minDur)
      .sort((a, b) => b.duration_sec - a.duration_sec);

    if (filtered.length === 0) {
      el.issuesTable.innerHTML = `<tr><td colspan="8" style="text-align:center;color:var(--text-muted);padding:24px;">No matching issues.</td></tr>`;
      return;
    }

    el.issuesTable.innerHTML = '';
    filtered.forEach(i => {
      const tr = document.createElement('tr');
      const sevBadge = i.severity === 'persistent' ? 'badge-error' : 'badge-warn';
      tr.innerHTML = `
        <td style="font-weight:600;color:#58a6ff;cursor:pointer;" data-guid="${esc(i.guid)}">${esc(i.bot)}</td>
        <td>${esc(i.class || '-')}</td>
        <td><span class="badge ${sevBadge}">${esc(ISSUE_LABELS[i.type] || i.type)}</span></td>
        <td class="mono" style="color:${i.severity === 'persistent' ? '#f85149' : '#d29922'};">${esc(fmtDuration(i.duration_sec))}</td>
        <td class="mono" style="font-size:0.75rem;">${esc(i.action || '-')}</td>
        <td class="mono" style="font-size:0.75rem;color:var(--text-muted);">${esc(i.trigger || '-')}</td>
        <td class="mono" style="font-size:0.75rem;">${esc(getZoneName(i.zone))}</td>
        <td style="color:#f85149;">${esc(i.target || '-')}</td>`;
      tr.querySelector('td[data-guid]').addEventListener('click', () => focusBot(i.guid));
      el.issuesTable.appendChild(tr);
    });
  }

  function renderIssueZones() {
    if (!el.issueZones) return;
    const zones = new Map();
    state.issues.active.forEach(i => zones.set(i.zone, (zones.get(i.zone) || 0) + 1));
    if (zones.size === 0) {
      el.issueZones.innerHTML = '<div class="empty-hint">No active issues.</div>';
      return;
    }
    const entries = [...zones.entries()].sort((a, b) => b[1] - a[1]).slice(0, 8);
    const max = entries[0][1] || 1;
    el.issueZones.innerHTML = entries.map(([zid, n]) =>
      `<div class="comp-row"><span class="comp-name" style="width:110px;">${esc(getZoneName(zid))}</span><span class="comp-bar-bg"><span class="comp-bar" style="width:${Math.round((n / max) * 100)}%;background:#f85149;"></span></span><span class="comp-count">${n}</span></div>`
    ).join('');
  }

  function renderIssueChart() {
    const prepared = prepCanvas(el.issueChart);
    if (!prepared) return;
    const { ctx, w, h } = prepared;
    const hist = state.issueHistory;
    const len = hist.t.length;

    if (el.issueLegend) {
      el.issueLegend.innerHTML = ISSUE_TYPES.map(t =>
        `<div class="legend-item"><span class="legend-color" style="background:${ISSUE_COLORS[t]};"></span>${esc(ISSUE_LABELS[t])}</div>`
      ).join('');
    }

    if (len === 0) {
      ctx.fillStyle = '#484f58';
      ctx.font = '12px Inter';
      ctx.fillText('Collecting issue history...', 12, h / 2);
      return;
    }

    let maxVal = 1;
    for (let i = 0; i < len; i++) {
      let sum = 0;
      ISSUE_TYPES.forEach(t => { sum += (hist.series[t] || [])[i] || 0; });
      if (sum > maxVal) maxVal = sum;
    }
    maxVal = Math.ceil(maxVal * 1.2);

    const padTop = 10, padBottom = 12;
    const yFor = v => padTop + (h - padTop - padBottom) * (1 - v / maxVal);
    const stepX = len > 1 ? w / (len - 1) : 0;
    const xFor = i => len > 1 ? i * stepX : w / 2;

    drawGrid(ctx, w, h, padTop, padBottom, maxVal);

    let lower = new Array(len).fill(0);
    ISSUE_TYPES.forEach(t => {
      const series = hist.series[t] || [];
      const upper = lower.map((v, i) => v + (series[i] || 0));
      ctx.beginPath();
      for (let i = 0; i < len; i++) {
        const x = xFor(i), y = yFor(upper[i]);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      for (let i = len - 1; i >= 0; i--) ctx.lineTo(xFor(i), yFor(lower[i]));
      ctx.closePath();
      ctx.fillStyle = hexToRgba(ISSUE_COLORS[t], 0.5);
      ctx.fill();

      ctx.beginPath();
      for (let i = 0; i < len; i++) {
        const x = xFor(i), y = yFor(upper[i]);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = ISSUE_COLORS[t];
      ctx.lineWidth = 1.5;
      ctx.stroke();

      lower = upper;
    });
  }

  function renderResolvedList() {
    if (!el.issuesResolved) return;
    const list = state.issues.resolved.slice(0, 12);
    if (list.length === 0) {
      el.issuesResolved.innerHTML = '<div class="empty-hint">No resolved episodes yet.</div>';
      return;
    }
    el.issuesResolved.innerHTML = list.map(i =>
      `<div class="resolved-row"><span class="badge badge-info">${esc(ISSUE_LABELS[i.type] || i.type)}</span><strong style="color:#fff;">${esc(i.bot)}</strong><span style="color:var(--text-muted);">${esc(getZoneName(i.zone))}</span><span class="mono" style="margin-left:auto;color:#2ea043;">${esc(fmtDuration(i.duration_sec))}</span></div>`
    ).join('');
  }

  function focusBot(guid) {
    const bot = state.bots.find(b => b.guid === guid);

    // Open the map on the zone the bot is actually in, otherwise the marker
    // would not be drawn (the map filters to the selected zone).
    if (bot && ZONE_CONFIG[bot.zone]) {
      state.currentZoneId = bot.zone;
      if (el.zoneSelect) el.zoneSelect.value = bot.zone;
      loadZoneMap(bot.zone);
    }

    state.selectedBotGuid = guid;
    switchTab('map');
    selectBot(guid);
  }

  if (el.issueTypeFilter) {
    el.issueTypeFilter.addEventListener('change', (e) => {
      state.issueTypeFilter = e.target.value;
      renderIssueTable();
    });
  }
  if (el.issueDurationFilter) {
    el.issueDurationFilter.addEventListener('change', (e) => {
      state.issueDurationFilter = parseInt(e.target.value, 10) || 0;
      renderIssueTable();
    });
  }

  // Local watchdog: the daemon cannot tell us it died, so the client decides
  // based on pulse age. This is what makes server-online trustworthy.
  setInterval(() => {
    updateSnapshotAge();
    const age = state.lastHeartbeatAt ? Date.now() - state.lastHeartbeatAt : Infinity;
    if (age > OFFLINE_AFTER_MS && state.server.online) {
      state.server.online = false;
      state.server.stale = true;
      if (state.server.states) {
        state.server.states = { combat: 0, moving: 0, resting: 0, dead: 0, idle: 0 };
      }
      updateDashboardMetrics();
      if (state.activeTab === 'dashboard') renderDashboardCharts();
      appendConsoleLog(new Date().toLocaleTimeString(), 'watchdog', 'No heartbeat for 10s: marking server offline.', 'warn');
    }
  }, 3000);

  // Init
  initZoneSelector();
  fetchBots();
  fetchAnomalies();
  fetchIssues();
  initWebSocket();

})();
