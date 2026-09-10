// Tortoise WoW Observability Dashboard — Telemetry & Bot Management
(function() {
  'use strict';

  // Fallback zone dictionary (loaded dynamically from /data/zone_maps.json)
  let ZONE_CONFIG = {
    12: { name: 'Elwynn Forest', map: 0, file: 'elwynn.webp' },
    14: { name: 'Durotar', map: 1, file: 'durotar.webp' },
    17: { name: 'The Barrens', map: 1, file: 'barrens.webp' },
    3277: { name: 'Warsong Gulch', map: 489, file: 'warsonggulch.webp' }
  };

  const state = {
    activeTab: 'dashboard',
    currentZoneId: 12,
    bots: [],
    anomalies: [],
    server: {
      online: false,
      uptime: 0,
      diff: 0,
      humans: 0,
      bots: 0,
      states: { combat: 0, moving: 0, resting: 0, dead: 0, idle: 0 }
    },
    history: {
      timestamps: [],
      bots: [],
      humans: []
    },
    showTrails: true,
    roleFilter: 'all',
    searchQuery: '',
    selectedBotGuid: null,
    anomalyTypeFilter: 'all',
    anomalySeverityFilter: 'all',
    wsConnected: false
  };

  // DOM Elements
  const el = {
    statusPill: document.getElementById('status-pill'),
    statusDot: document.getElementById('status-dot'),
    topBotsVal: document.getElementById('top-bots-val'),
    subHumansVal: document.getElementById('sub-humans-val'),
    subBotsVal: document.getElementById('sub-bots-val'),
    metricBotsOnline: document.getElementById('metric-bots-online'),
    metricHumansOnline: document.getElementById('metric-humans-online'),
    metricUptime: document.getElementById('metric-uptime'),
    gaugeTickVal: document.getElementById('gauge-tick-val'),
    gaugeTickBar: document.getElementById('gauge-tick-bar'),
    gaugeLoadVal: document.getElementById('gauge-load-val'),
    gaugeLoadBar: document.getElementById('gauge-load-bar'),
    
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
    toggleTrails: document.getElementById('toggle-trails'),
    botDrawer: document.getElementById('bot-drawer'),
    drawerContent: document.getElementById('drawer-content'),
    closeDrawer: document.getElementById('close-drawer'),

    // Roster
    roleFilter: document.getElementById('role-filter'),
    botSearch: document.getElementById('bot-search'),
    rosterTable: document.getElementById('roster-table-body'),

    // Incidents
    anomaliesTable: document.getElementById('anomalies-table-body'),
    anomaliesCount: document.getElementById('anomalies-count'),
    typeFilter: document.getElementById('anomaly-type-filter'),
    severityFilter: document.getElementById('anomaly-severity-filter'),
    clearAnomalies: document.getElementById('clear-anomalies'),

    // Macro states
    stateCombat: document.getElementById('state-combat'),
    stateCombatTxt: document.getElementById('state-combat-txt'),
    stateMoving: document.getElementById('state-moving'),
    stateMovingTxt: document.getElementById('state-moving-txt'),
    stateResting: document.getElementById('state-resting'),
    stateRestingTxt: document.getElementById('state-resting-txt'),
    stateIdle: document.getElementById('state-idle'),
    stateIdleTxt: document.getElementById('state-idle-txt'),
    stateDead: document.getElementById('state-dead'),
    stateDeadTxt: document.getElementById('state-dead-txt'),

    // Chart & Console
    activityChart: document.getElementById('activity-chart'),
    consoleBody: document.getElementById('console-body'),
    consoleRate: document.getElementById('console-rate'),
    refreshBtn: document.getElementById('refresh-btn')
  };

  // Sidebar Tab Navigation
  el.menuItems.forEach(item => {
    item.addEventListener('click', (e) => {
      e.preventDefault();
      const tab = item.dataset.tab;
      if (!tab) return;
      state.activeTab = tab;
      el.menuItems.forEach(m => m.classList.toggle('active', m.dataset.tab === tab));
      el.tabViews.forEach(v => {
        v.style.display = v.id === `tab-${tab}` ? 'block' : 'none';
      });
      if (tab === 'map') renderMap();
      if (tab === 'roster') renderRoster();
      if (tab === 'dashboard') renderActivityChart();
    });
  });

  if (el.refreshBtn) {
    el.refreshBtn.addEventListener('click', () => {
      fetchBots();
      fetchAnomalies();
    });
  }

  // Format uptime
  function formatUptime(seconds) {
    if (!seconds) return '0m';
    const m = Math.floor(seconds / 60);
    const h = Math.floor(m / 60);
    const remM = m % 60;
    if (h > 0) return `${h}h ${remM}m`;
    return `${m}m`;
  }

  // Look up human-readable zone name from ZONE_CONFIG
  function getZoneName(zoneId) {
    if (!zoneId && zoneId !== 0) return '-';
    const zone = ZONE_CONFIG[zoneId];
    return zone ? zone.name : `Zone ${zoneId}`;
  }

  // Log to Console Card
  function appendConsoleLog(time, tag, text, level = 'info') {
    if (!el.consoleBody) return;
    const line = document.createElement('div');
    line.className = 'log-line';
    const tagClass = level === 'warn' ? 'warn' : level === 'error' ? 'error' : '';
    line.innerHTML = `<span class="log-time">[${time}]</span> <span class="log-tag ${tagClass}">[${tag}]</span> ${text}`;
    el.consoleBody.appendChild(line);
    while (el.consoleBody.children.length > 200) {
      el.consoleBody.removeChild(el.consoleBody.firstChild);
    }
    el.consoleBody.scrollTop = el.consoleBody.scrollHeight;
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
      el.zoneFilterInput.addEventListener('input', (e) => {
        populateZoneSelect(e.target.value);
      });
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

  // Activity Timeline Sparkline
  function renderActivityChart() {
    const canvas = el.activityChart;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const w = canvas.parentElement.clientWidth;
    const h = canvas.parentElement.clientHeight;
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }

    ctx.clearRect(0, 0, w, h);

    // Draw subtle grid lines
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
    ctx.lineWidth = 1;
    for (let y = 0; y <= h; y += h / 4) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
    }

    const data = state.history.bots;
    if (data.length < 2) {
      ctx.fillStyle = '#484f58';
      ctx.font = '12px Inter';
      ctx.fillText('Waiting for activity telemetry...', 20, h / 2);
      return;
    }

    const maxVal = Math.max(10, Math.max(...data, ...state.history.humans) * 1.15);
    const stepX = w / (data.length - 1);

    // Draw Bots filled area
    ctx.beginPath();
    ctx.moveTo(0, h);
    data.forEach((val, idx) => {
      const x = idx * stepX;
      const y = h - (val / maxVal) * (h - 20) - 10;
      if (idx === 0) ctx.lineTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.lineTo(w, h);
    ctx.closePath();
    const grad = ctx.createLinearGradient(0, 0, 0, h);
    grad.addColorStop(0, 'rgba(88, 166, 255, 0.25)');
    grad.addColorStop(1, 'rgba(88, 166, 255, 0.0)');
    ctx.fillStyle = grad;
    ctx.fill();

    // Draw Bots stroke
    ctx.beginPath();
    data.forEach((val, idx) => {
      const x = idx * stepX;
      const y = h - (val / maxVal) * (h - 20) - 10;
      if (idx === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.strokeStyle = '#58a6ff';
    ctx.lineWidth = 2;
    ctx.stroke();

    // Draw Players stroke
    ctx.beginPath();
    state.history.humans.forEach((val, idx) => {
      const x = idx * stepX;
      const y = h - (val / maxVal) * (h - 20) - 10;
      if (idx === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.strokeStyle = '#2ea043';
    ctx.lineWidth = 2;
    ctx.stroke();
  }

  // Update Top Stats & Gauge Rings
  function updateDashboardMetrics() {
    const s = state.server;
    const botCount = state.bots.length > 0 ? state.bots.length : s.bots;

    if (el.topBotsVal) el.topBotsVal.textContent = botCount;
    if (el.subBotsVal) el.subBotsVal.textContent = `${botCount} Bots`;
    if (el.subHumansVal) el.subHumansVal.textContent = `${s.humans} Players`;
    if (el.metricBotsOnline) el.metricBotsOnline.textContent = botCount;
    if (el.metricHumansOnline) el.metricHumansOnline.textContent = s.humans;
    if (el.metricUptime) el.metricUptime.textContent = formatUptime(s.uptime);

    // Tick ms gauge
    const diff = s.diff || 50;
    if (el.gaugeTickVal) el.gaugeTickVal.textContent = `${diff}ms`;
    if (el.gaugeTickBar) {
      const maxDiff = 200;
      const pct = Math.min(1, diff / maxDiff);
      const circumference = 188.5;
      el.gaugeTickBar.style.strokeDashoffset = circumference - (circumference * pct);
      el.gaugeTickBar.style.stroke = diff > 100 ? '#f85149' : diff > 70 ? '#d29922' : '#2ea043';
    }

    // Load gauge
    const load = (diff / 100).toFixed(2);
    if (el.gaugeLoadVal) el.gaugeLoadVal.textContent = load;
    if (el.gaugeLoadBar) {
      const circumference = 188.5;
      const pct = Math.min(1, parseFloat(load));
      el.gaugeLoadBar.style.strokeDashoffset = circumference - (circumference * pct);
    }

    // Update activity history
    const now = new Date().toLocaleTimeString();
    state.history.timestamps.push(now);
    state.history.bots.push(botCount);
    state.history.humans.push(s.humans);
    if (state.history.bots.length > 30) {
      state.history.timestamps.shift();
      state.history.bots.shift();
      state.history.humans.shift();
    }
    if (state.activeTab === 'dashboard') renderActivityChart();
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
      if (b.zone !== state.currentZoneId) return false;
      if (state.roleFilter !== 'all' && b.role !== state.roleFilter) return false;
      return true;
    });

    // Draw trails
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
        ctx.strokeStyle = b.role === 'tank' ? 'rgba(56, 139, 253, 0.4)' : b.role === 'healer' ? 'rgba(46, 160, 67, 0.4)' : 'rgba(248, 81, 73, 0.4)';
        ctx.lineWidth = 2;
        ctx.stroke();
      });
    }

    // Draw bot markers (directional triangles)
    zoneBots.forEach(b => {
      const dot = document.createElement('div');
      dot.className = 'bot-dot';
      dot.style.left = `${b.pct_x}%`;
      dot.style.top = `${b.pct_y}%`;
      const roleColor = b.role === 'tank' ? 'var(--color-tank)' : b.role === 'healer' ? 'var(--color-healer)' : 'var(--color-dps)';
      dot.style.backgroundColor = roleColor;
      // Rotate triangle to face the bot's orientation
      const deg = (b.o || 0) * (180 / Math.PI);
      dot.style.transform = `translate(-50%, -50%) rotate(${-deg}deg)`;

      dot.addEventListener('mouseenter', (e) => {
        if (!el.mapTooltip) return;
        el.mapTooltip.style.display = 'block';
        el.mapTooltip.style.left = `${e.clientX + 12}px`;
        el.mapTooltip.style.top = `${e.clientY + 12}px`;
        el.mapTooltip.innerHTML = `
          <strong style="color: #fff;">${b.name}</strong> (${b.class || 'Unknown'} Lvl ${b.level})<br>
          <span style="color: var(--text-muted);">Role:</span> ${b.role.toUpperCase()}<br>
          <span style="color: var(--text-muted);">Status:</span> ${b.state || 'idle'}<br>
          <span style="color: var(--text-muted);">Zone:</span> ${getZoneName(b.zone)}<br>
          <span style="color: var(--text-muted);">Target:</span> ${b.target || 'None'}
        `;
      });

      dot.addEventListener('mouseleave', () => {
        if (el.mapTooltip) el.mapTooltip.style.display = 'none';
      });

      dot.addEventListener('click', () => selectBot(b.guid));
      el.mapOverlay.appendChild(dot);
    });
  }

  function selectBot(guid) {
    state.selectedBotGuid = guid;
    const b = state.bots.find(x => x.guid === guid);
    if (!b || !el.botDrawer || !el.drawerContent) return;

    el.botDrawer.style.display = 'block';
    const hpPct = b.max_hp ? Math.round((b.hp / b.max_hp) * 100) : 100;
    const powerPct = b.max_power ? Math.round((b.power / b.max_power) * 100) : 0;

    el.drawerContent.innerHTML = `
      <div style="font-size: 1.15rem; font-weight: 700; color: #fff; margin-bottom: 4px;">${b.name}</div>
      <div style="font-size: 0.8rem; color: var(--text-muted); margin-bottom: 16px;">
        Level ${b.level} ${b.class} · <span class="badge badge-info">${b.role.toUpperCase()}</span>
      </div>

      <div style="margin-bottom: 14px;">
        <div style="display: flex; justify-content: space-between; font-size: 0.75rem; margin-bottom: 4px;">
          <span>Health</span><strong>${b.hp} / ${b.max_hp} (${hpPct}%)</strong>
        </div>
        <div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${hpPct}%; background: var(--accent-green-bright);"></div></div>
      </div>

      <div style="margin-bottom: 16px;">
        <div style="display: flex; justify-content: space-between; font-size: 0.75rem; margin-bottom: 4px;">
          <span>Mana / Energy</span><strong>${b.power} / ${b.max_power}</strong>
        </div>
        <div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${powerPct}%; background: var(--accent-blue-bright);"></div></div>
      </div>

      <div style="background: rgba(255, 255, 255, 0.03); border: 1px solid var(--border-color); border-radius: 6px; padding: 10px; font-size: 0.8rem; display: flex; flex-direction: column; gap: 6px;">
        <div><span style="color: var(--text-muted);">Status:</span> <strong>${b.state}</strong></div>
        <div><span style="color: var(--text-muted);">Target:</span> <strong style="color: #f85149;">${b.target || 'None'}</strong></div>
        <div><span style="color: var(--text-muted);">Strategy:</span> <span class="mono" style="font-size: 0.75rem;">${b.strategy || 'default'}</span></div>
      </div>
    `;
  }

  if (el.closeDrawer) {
    el.closeDrawer.addEventListener('click', () => {
      if (el.botDrawer) el.botDrawer.style.display = 'none';
      state.selectedBotGuid = null;
    });
  }

  // Roster Table
  function renderRoster() {
    if (!el.rosterTable) return;
    const query = (el.botSearch ? el.botSearch.value : '').toLowerCase().trim();
    const filtered = state.bots.filter(b => {
      if (state.roleFilter !== 'all' && b.role !== state.roleFilter) return false;
      if (query && !b.name.toLowerCase().includes(query) && !b.class.toLowerCase().includes(query)) return false;
      return true;
    });

    if (filtered.length === 0) {
      el.rosterTable.innerHTML = `<tr><td colspan="8" style="text-align: center; color: var(--text-muted); padding: 24px;">No active bots found.</td></tr>`;
      return;
    }

    el.rosterTable.innerHTML = '';
    filtered.forEach(b => {
      const tr = document.createElement('tr');
      const hpPct = b.max_hp ? Math.round((b.hp / b.max_hp) * 100) : 100;
      const roleBadge = b.role === 'tank' ? 'badge-info' : b.role === 'healer' ? 'badge-success' : 'badge-error';

      tr.innerHTML = `
        <td style="font-weight: 600; cursor: pointer; color: #58a6ff;" onclick="window.dashboardSelectBot(${b.guid})">${b.name}</td>
        <td>${b.class}</td>
        <td><span class="badge ${roleBadge}">${b.role.toUpperCase()}</span></td>
        <td>${b.level}</td>
        <td style="width: 140px;">
          <div style="font-size: 0.7rem; margin-bottom: 2px;">${b.hp}/${b.max_hp} (${hpPct}%)</div>
          <div class="progress-bar-bg"><div class="progress-bar-fill" style="width: ${hpPct}%; background: var(--accent-green-bright);"></div></div>
        </td>
        <td><span class="badge ${b.state === 'combat' ? 'badge-error' : b.state === 'dead' ? 'badge-warn' : 'badge-info'}">${b.state || 'idle'}</span></td>
        <td style="color: #f85149;">${b.target || '-'}</td>
        <td class="mono" style="font-size: 0.8rem;">${getZoneName(b.zone)}</td>
      `;
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

  // Incidents
  function fetchAnomalies() {
    fetch('/api/v1/anomalies')
      .then(r => r.json())
      .then(data => {
        state.anomalies = data || [];
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

    el.anomaliesTable.innerHTML = '';
    filtered.slice().reverse().forEach(a => {
      const tr = document.createElement('tr');
      const timeStr = a.time_str || (a.ts ? new Date(a.ts * 1000).toLocaleTimeString() : '-');
      const sev = (a.severity || 'info').toLowerCase();
      const sevBadge = sev === 'error' ? 'badge-error' : sev === 'warn' ? 'badge-warn' : 'badge-info';

      tr.innerHTML = `
        <td class="mono" style="font-size: 0.75rem; color: var(--text-muted);">${timeStr}</td>
        <td><span class="badge ${sevBadge}">${sev.toUpperCase()}</span></td>
        <td class="mono" style="font-size: 0.8rem; font-weight: 600;">${a.type}</td>
        <td style="color: #fff; font-weight: 600;">${a.bot || '-'}</td>
        <td class="mono" style="font-size: 0.75rem;">${getZoneName(a.zone)}</td>
        <td style="color: var(--text-muted);">${a.details || a.last_action || '-'}</td>
        <td style="color: #f85149;">${a.target || '-'}</td>
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
      state.anomalies = [];
      renderAnomalies();
    });
  }

  function fetchBots() {
    fetch('/api/v1/bots')
      .then(r => r.json())
      .then(data => {
        if (Array.isArray(data)) {
          state.bots = data;
          updateDashboardMetrics();
          if (state.activeTab === 'map') renderMap();
          if (state.activeTab === 'roster') renderRoster();
        }
      })
      .catch(() => {});
  }

  // WebSocket Live Streaming
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
        const msg = JSON.parse(event.data);
        handleStreamMessage(msg);
      } catch (e) {}
    };

    ws.onclose = () => {
      state.wsConnected = false;
      if (el.consoleRate) el.consoleRate.innerHTML = '<span style="color: #f85149;">● disconnected</span>';
      setTimeout(initWebSocket, 3000);
    };
  }

  function handleStreamMessage(msg) {
    const time = new Date().toLocaleTimeString();

    if (msg.event === 'heartbeat') {
      const d = msg.data;
      if (!d) return;
      state.server.online = true;
      state.server.uptime = d.uptime || 0;
      state.server.diff = d.diff || 0;
      state.server.humans = d.humans || 0;
      state.server.bots = d.bots || 0;
      updateDashboardMetrics();
      // State ratios are embedded in the heartbeat payload
      if (d.states) {
        updateMacroRatios(d.states);
      }
      appendConsoleLog(time, 'pulse', `Heartbeat diff=${d.diff}ms, Players=${d.humans}, Bots=${d.bots}`);
    } else if (msg.event === 'bots') {
      const bots = msg.data;
      if (!Array.isArray(bots)) return;
      // Merge/upsert incoming bot snapshots by GUID
      bots.forEach(bot => {
        const idx = state.bots.findIndex(b => b.guid === bot.guid);
        if (idx >= 0) {
          state.bots[idx] = bot;
        } else {
          state.bots.push(bot);
        }
      });
      updateDashboardMetrics();
      if (state.activeTab === 'map') renderMap();
      if (state.activeTab === 'roster') renderRoster();
    } else if (msg.event === 'anomaly') {
      const a = msg.data;
      if (a) {
        state.anomalies.push(a);
        renderAnomalies();
        const sev = (a.severity || 'info').toLowerCase();
        appendConsoleLog(time, a.type, `<span class="log-bot">${a.bot || 'Bot'}</span>: ${a.details || a.last_action}`, sev);
      }
    }
  }

  function updateMacroRatios(r) {
    const toPct = (val) => `${Math.round((val || 0) * 100)}%`;
    if (el.stateCombat) el.stateCombat.style.width = toPct(r.combat);
    if (el.stateCombatTxt) el.stateCombatTxt.textContent = toPct(r.combat);
    if (el.stateMoving) el.stateMoving.style.width = toPct(r.moving);
    if (el.stateMovingTxt) el.stateMovingTxt.textContent = toPct(r.moving);
    if (el.stateResting) el.stateResting.style.width = toPct(r.resting);
    if (el.stateRestingTxt) el.stateRestingTxt.textContent = toPct(r.resting);
    if (el.stateIdle) el.stateIdle.style.width = toPct(r.idle);
    if (el.stateIdleTxt) el.stateIdleTxt.textContent = toPct(r.idle);
    if (el.stateDead) el.stateDead.style.width = toPct(r.dead);
    if (el.stateDeadTxt) el.stateDeadTxt.textContent = toPct(r.dead);
  }

  window.dashboardSelectBot = selectBot;

  // Init
  initZoneSelector();
  fetchBots();
  fetchAnomalies();
  initWebSocket();

  // Periodic full-refresh to keep bot list authoritative and remove stale entries
  setInterval(() => {
    fetchBots();
    fetchAnomalies();
  }, 5000);

})();
