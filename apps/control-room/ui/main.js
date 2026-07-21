const API = "http://127.0.0.1:4017/api";

const ui = {
  connection: document.querySelector("#connection"),
  connectionDot: document.querySelector("#connection-dot"),
  railStatus: document.querySelector("#rail-status"),
  railStatusDot: document.querySelector("#rail-status-dot"),
  modePill: document.querySelector("#mode-pill"),
  offlineBanner: document.querySelector("#offline-banner"),
  refresh: document.querySelector("#refresh"),
  lastUpdated: document.querySelector("#last-updated"),
  demoButton: document.querySelector("#demo-button"),
  topology: document.querySelector("#topology"),
  telemetry: document.querySelector("#telemetry"),
  telemetryAge: document.querySelector("#telemetry-age"),
  systemScore: document.querySelector("#system-score"),
  postureSummary: document.querySelector("#posture-summary"),
  approvals: document.querySelector("#approvals"),
  approvalCount: document.querySelector("#approval-count"),
  recentEvents: document.querySelector("#recent-events"),
  networkSummary: document.querySelector("#network-summary"),
  networkNodes: document.querySelector("#network-nodes"),
  operations: document.querySelector("#operations"),
  timeline: document.querySelector("#timeline"),
  chronicleSearch: document.querySelector("#chronicle-search"),
  nodeDialog: document.querySelector("#node-dialog"),
  nodeDialogContent: document.querySelector("#node-dialog-content"),
  connectionDialog: document.querySelector("#connection-dialog"),
  proposalResult: document.querySelector("#proposal-result"),
  proposalTarget: document.querySelector("#proposal-target"),
  proposalCapability: document.querySelector("#proposal-capability"),
};

const state = {
  mode: "offline",
  nodes: [],
  operations: [],
  timeline: [],
  catalog: [],
  operationFilter: "all",
  lastRefresh: null,
};

const viewTitles = {
  command: "Command Center",
  network: "Network Topology",
  operations: "Operations",
  chronicle: "The Chronicle",
};

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = String(text);
  return node;
}

function valueOr(value, fallback = "—") {
  return value === undefined || value === null || value === "" ? fallback : value;
}

function humanBytes(value) {
  const bytes = Number(value);
  if (!Number.isFinite(bytes)) return "—";
  if (bytes < 1024) return `${bytes} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let amount = bytes;
  let unit = -1;
  do {
    amount /= 1024;
    unit += 1;
  } while (amount >= 1024 && unit < units.length - 1);
  return `${amount >= 100 ? amount.toFixed(0) : amount.toFixed(1)} ${units[unit]}`;
}

function sentence(value) {
  return String(valueOr(value, "unknown"))
    .replaceAll("_", " ")
    .replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function timeLabel(epochSeconds) {
  const date = new Date(Number(epochSeconds) * 1000);
  if (Number.isNaN(date.getTime())) return "—";
  return new Intl.DateTimeFormat(undefined, {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  }).format(date);
}

function relativeTime(epochSeconds) {
  const seconds = Math.max(0, Math.round(Date.now() / 1000 - Number(epochSeconds)));
  if (!Number.isFinite(seconds)) return "unknown age";
  if (seconds < 5) return "just now";
  if (seconds < 60) return `${seconds}s ago`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
  return `${Math.floor(seconds / 3600)}h ago`;
}

function nodeId(payload = {}) {
  return payload.node_id || payload.id || payload.host?.hostname || "unknown-node";
}

function capabilities(payload = {}) {
  return (payload.capabilities || payload.installed_models || []).map((capability) =>
    typeof capability === "string" ? capability : capability.name || "unnamed",
  );
}

function nodeKind(payload = {}) {
  if (payload.hardware?.chip) return payload.hardware.chip;
  if (payload.host?.os) return payload.host.os;
  if (payload.server_version) return `Ollama ${payload.server_version}`;
  return payload.kind || "OrcLink node";
}

function nodeAddress(payload = {}) {
  const network = payload.hardware?.network || {};
  return network.ethernet?.ip || network.wifi?.ip || payload.network_ip || payload.host?.hostname || "No address reported";
}

function latestHeartbeat(timeline, id) {
  return [...timeline]
    .reverse()
    .find(({ record }) => record?.type === "heartbeat" && record.payload?.node_id === id);
}

function normalizeNodes(snapshots, timeline) {
  const byId = new Map();
  for (const envelope of snapshots) {
    const payload = envelope.payload || {};
    byId.set(nodeId(payload), { envelope, payload });
  }

  return [...byId.entries()].map(([id, entry]) => {
    const heartbeat = latestHeartbeat(timeline, id);
    const live = heartbeat?.record?.payload || {};
    const payload = entry.payload;
    const snapshotNetwork = payload.hardware?.network || {};
    const liveNetwork = {
      ethernet: {
        link: live.network_link ?? snapshotNetwork.ethernet?.link,
        ip: live.network_ip || snapshotNetwork.ethernet?.ip,
      },
      wifi: {
        ...snapshotNetwork.wifi,
        station_ready: live.wifi_station_ready ?? snapshotNetwork.wifi?.station_ready,
        configured: live.wifi_configured ?? snapshotNetwork.wifi?.configured,
        connected: live.wifi_connected ?? snapshotNetwork.wifi?.connected,
        ip: live.wifi_ip || snapshotNetwork.wifi?.ip,
      },
    };

    return {
      id,
      payload,
      heartbeat: live,
      heartbeatAt: heartbeat?.received_at,
      name: payload.display_name || id,
      kind: nodeKind(payload),
      address: liveNetwork.ethernet.ip || liveNetwork.wifi.ip || nodeAddress(payload),
      status: payload.connection_state || (liveNetwork.ethernet.link || liveNetwork.wifi.connected ? "online" : "degraded"),
      network: liveNetwork,
      capabilities: capabilities(payload),
      hardware: payload.hardware || {},
      isDemo: Boolean(payload.__demo),
    };
  });
}

function normalizeOperations(envelopes) {
  const byId = new Map();
  for (const envelope of envelopes) {
    const payload = envelope.payload || {};
    const id = payload.command_id || payload.id || envelope.in_reply_to || envelope.message_id;
    byId.set(id, {
      id,
      messageId: envelope.message_id,
      capability: payload.capability?.name || payload.capability || payload.action || "Command update",
      target: payload.target?.node_id || payload.node_id || payload.target || "—",
      risk: String(payload.risk || "R0").toUpperCase(),
      state: payload.state || "Unknown",
      outcome: payload.outcome || "Pending",
      isDemo: Boolean(payload.__demo),
    });
  }
  return [...byId.values()].reverse();
}

async function read(path) {
  const response = await fetch(`${API}/${path}`, { cache: "no-store" });
  if (!response.ok) throw new Error(`${path}: HTTP ${response.status}`);
  return response.json();
}

function renderProposalCapabilities() {
  const target = ui.proposalTarget.value;
  const available = state.catalog.filter((capability) => capability.nodes?.includes(target));
  ui.proposalCapability.replaceChildren();
  if (!available.length) {
    ui.proposalCapability.append(new Option("No cataloged capabilities", ""));
    ui.proposalCapability.disabled = true;
    return;
  }
  for (const capability of available) {
    const risk = capability.risk_floor || "NO RISK FLOOR";
    const option = new Option(`${capability.name} · ${risk}`, capability.name);
    option.disabled = !capability.invokable;
    option.dataset.risk = capability.risk_floor || "";
    ui.proposalCapability.append(option);
  }
  ui.proposalCapability.disabled = !available.some((capability) => capability.invokable);
}

function renderProposalTargets() {
  const prior = ui.proposalTarget.value;
  ui.proposalTarget.replaceChildren();
  for (const node of state.nodes) {
    ui.proposalTarget.append(new Option(`${node.name} · ${node.status}`, node.id));
  }
  if (!state.nodes.length) {
    ui.proposalTarget.append(new Option("No authoritative nodes", ""));
    ui.proposalTarget.disabled = true;
  } else {
    ui.proposalTarget.disabled = false;
    if (state.nodes.some((node) => node.id === prior)) ui.proposalTarget.value = prior;
  }
  renderProposalCapabilities();
}

function empty(container, title, detail) {
  container.replaceChildren();
  const box = el("div", "empty-state");
  box.append(el("strong", "", title), el("span", "", detail));
  container.append(box);
}

function setConnection(mode, detail = "") {
  state.mode = mode;
  const copy = {
    live: ["Daemon online", "Connected", "Live telemetry"],
    demo: ["Interface preview · simulated data", "Preview", "Preview data"],
    offline: [`Daemon unavailable${detail ? ` · ${detail}` : ""}`, "Offline", "Daemon offline"],
  }[mode];

  ui.connection.textContent = copy[0];
  ui.railStatus.textContent = copy[1];
  ui.modePill.lastChild.textContent = ` ${copy[2]}`;
  ui.connectionDot.className = `status-dot ${mode}`;
  ui.railStatusDot.className = `status-dot ${mode}`;
  ui.modePill.className = `mode-pill ${mode === "live" ? "" : mode}`.trim();
  ui.offlineBanner.hidden = mode === "live";
  ui.demoButton.textContent = mode === "demo" ? "Return to live state" : "Explore interface preview";
}

function statePill(status, demo = false) {
  const pill = el("span", `state-pill ${demo ? "demo" : String(status).toLowerCase()}`, demo ? "Preview" : sentence(status));
  return pill;
}

function nodeGlyph(node) {
  if (/esp|waveshare|m5/i.test(`${node.kind} ${node.name}`)) return "▣";
  if (/ollama|model/i.test(node.kind)) return "◈";
  return "⬡";
}

function renderPosture() {
  const total = state.nodes.length;
  const online = state.nodes.filter((node) => node.status === "online").length;
  const degraded = state.nodes.filter((node) => node.status !== "online").length;
  const score = total ? Math.round((online / total) * 100) : null;
  const scoreValue = ui.systemScore.querySelector("strong");
  const scoreTitle = ui.systemScore.querySelector("p b");
  const scoreDetail = ui.systemScore.querySelector("p small");

  scoreValue.textContent = score === null ? "—" : `${score}`;
  scoreTitle.textContent = state.mode === "demo" ? "Preview" : score === null ? "No evidence" : degraded ? "Attention" : "Nominal";
  scoreDetail.textContent = state.mode === "demo" ? "simulated posture" : total ? `${online}/${total} nodes online` : "awaiting snapshots";

  if (state.mode === "demo") {
    ui.postureSummary.textContent = "A clearly marked preview of TheOrc coordinating through OrcLink. No device state or authority is implied.";
  } else if (!total) {
    ui.postureSummary.textContent = "No authoritative node snapshot is available yet. Start orclinkd and an authenticated adapter to establish current state.";
  } else if (!degraded) {
    ui.postureSummary.textContent = `${total} ${total === 1 ? "node is" : "nodes are"} reporting online through the local OrcLink control plane.`;
  } else {
    ui.postureSummary.textContent = `${degraded} of ${total} connected ${total === 1 ? "node needs" : "nodes need"} operator attention.`;
  }
}

function renderTopology() {
  if (!state.nodes.length) {
    empty(ui.topology, "No connected systems", "Authenticated node snapshots will appear here. Browser state is never treated as device truth.");
    return;
  }

  ui.topology.replaceChildren();
  const hub = el("div", "topology-hub");
  hub.append(el("span", "hub-mark", "⬡"), el("strong", "", "orclinkd"), el("small", "", state.mode === "demo" ? "preview authority" : "policy authority"));
  const nodes = el("div", "topology-nodes");

  for (const node of state.nodes) {
    const button = el("button", "topology-node");
    button.type = "button";
    button.dataset.nodeId = node.id;
    const icon = el("span", "node-icon", nodeGlyph(node));
    const copy = el("span");
    copy.append(el("strong", "", node.name), el("small", "", `${node.kind} · ${node.address}`));
    button.append(icon, copy, statePill(node.status, node.isDemo));
    button.addEventListener("click", () => openNode(node.id));
    nodes.append(button);
  }
  ui.topology.append(hub, nodes);
}

function telemetryMetrics() {
  const edge = state.nodes.find((node) => /esp|waveshare|m5/i.test(`${node.kind} ${node.name}`)) || state.nodes[0];
  if (!edge) return [];
  const heartbeat = edge.heartbeat || {};
  const ethernet = edge.network?.ethernet || {};
  const wifi = edge.network?.wifi || {};
  return [
    ["Primary link", ethernet.link ? "Ethernet" : wifi.connected ? "Wi-Fi" : "Offline", ethernet.ip || wifi.ip || "no IP reported"],
    ["Free heap", humanBytes(heartbeat.free_heap_bytes), edge.kind],
    ["Display + touch", heartbeat.display_ready === false || heartbeat.touch_ready === false ? "Attention" : "Ready", "ILI9341 / XPT2046"],
    ["Wi-Fi sidecar", wifi.connected ? "Connected" : wifi.station_ready ? "Ready" : "Unavailable", wifi.connected ? wifi.ip : wifi.configured ? "configured" : `${valueOr(wifi.scan_result, 0)} networks seen`],
  ];
}

function renderTelemetry() {
  const metrics = telemetryMetrics();
  if (!metrics.length) {
    empty(ui.telemetry, "No edge samples", "Heartbeat telemetry will be summarized without guessing missing values.");
    ui.telemetryAge.textContent = "Awaiting samples";
    return;
  }
  ui.telemetry.replaceChildren();
  for (const [label, value, detail] of metrics) {
    const card = el("div", "metric-card");
    card.append(el("span", "", label), el("strong", "", value), el("small", "", detail));
    ui.telemetry.append(card);
  }
  const newest = Math.max(...state.nodes.map((node) => Number(node.heartbeatAt || 0)));
  ui.telemetryAge.textContent = newest ? `Latest heartbeat ${relativeTime(newest)}` : "Snapshot data";
}

function pendingOperations() {
  return state.operations.filter(({ state: operationState }) => ["Proposed", "Validated", "Authorized"].includes(operationState));
}

function renderApprovals() {
  const pending = pendingOperations();
  ui.approvalCount.textContent = pending.length;
  if (!pending.length) {
    empty(ui.approvals, "No approvals waiting", "Approval controls appear only when daemon-owned command state requires a human gate.");
    return;
  }
  ui.approvals.replaceChildren();
  for (const operation of pending.slice(0, 3)) {
    const card = el("div", "approval-card");
    card.append(el("strong", "", operation.capability), el("p", "", `${operation.target} · ${operation.risk} · ${sentence(operation.state)}`));
    ui.approvals.append(card);
  }
}

function eventTitle(item) {
  const record = item.record || {};
  const type = sentence(record.type || "record");
  const subject = record.payload?.node_id || record.payload?.command_id || record.message_id;
  return [type, subject].filter(Boolean).join(" · ");
}

function renderRecent() {
  if (!state.timeline.length) {
    empty(ui.recentEvents, "Chronicle is empty", "Accepted adapter records will appear in append-only sequence order.");
    return;
  }
  ui.recentEvents.replaceChildren();
  for (const item of [...state.timeline].reverse().slice(0, 4)) {
    const row = el("div", "event-item");
    const copy = el("div");
    copy.append(el("strong", "", eventTitle(item)), el("small", "", `${item.source} · sequence ${item.sequence}`));
    const time = el("time", "", timeLabel(item.received_at));
    row.append(el("span", "event-dot"), copy, time);
    ui.recentEvents.append(row);
  }
}

function renderNetwork() {
  const online = state.nodes.filter((node) => node.status === "online").length;
  const ethernet = state.nodes.filter((node) => node.network?.ethernet?.link).length;
  const wifi = state.nodes.filter((node) => node.network?.wifi?.connected).length;
  const declared = state.nodes.reduce((total, node) => total + node.capabilities.length, 0);
  const metrics = [
    ["Registered nodes", state.nodes.length, `${online} online`],
    ["Ethernet links", ethernet, "reported up"],
    ["Wi-Fi links", wifi, "reported connected"],
    ["Capabilities", declared, "declared surfaces"],
  ];
  ui.networkSummary.replaceChildren();
  for (const [label, value, detail] of metrics) {
    const card = el("div", "metric-card");
    card.append(el("span", "", label), el("strong", "", value), el("small", "", detail));
    ui.networkSummary.append(card);
  }

  if (!state.nodes.length) {
    empty(ui.networkNodes, "No node registry projection", "Connect the daemon and an authenticated adapter to populate the topology.");
    return;
  }
  ui.networkNodes.replaceChildren();
  for (const node of state.nodes) {
    const row = el("button", "node-row");
    row.type = "button";
    const icon = el("span", "node-icon", nodeGlyph(node));
    const identity = el("span");
    identity.append(el("strong", "", node.name), el("small", "", node.id));
    row.append(
      icon,
      identity,
      el("span", "", node.kind),
      el("span", "capability-summary", node.capabilities.join(" · ") || "No capabilities declared"),
      statePill(node.status, node.isDemo),
    );
    row.addEventListener("click", () => openNode(node.id));
    ui.networkNodes.append(row);
  }
}

function operationMatches(operation) {
  if (state.operationFilter === "all") return true;
  if (state.operationFilter === "active") return ["Authorized", "Queued", "Executing", "Verifying", "Cancelling"].includes(operation.state);
  return ["Failed", "TimedOut", "Rejected", "Uncertain"].includes(operation.state) || operation.risk === "R3";
}

function renderOperations() {
  const operations = state.operations.filter(operationMatches);
  if (!operations.length) {
    empty(ui.operations, "No matching operations", state.operations.length ? "Try another lifecycle filter." : "Command lifecycle updates will appear here when recorded by orclinkd.");
    return;
  }
  ui.operations.replaceChildren();
  for (const operation of operations) {
    const row = el("div", "operation-row");
    const identity = el("div");
    identity.append(el("strong", "", operation.capability), el("small", "", operation.id));
    const risk = el("span", `risk-pill ${operation.risk.toLowerCase()}`, operation.risk);
    row.append(identity, el("span", "", operation.target), risk, statePill(operation.state, operation.isDemo), el("span", "", sentence(operation.outcome)));
    ui.operations.append(row);
  }
}

function renderTimeline() {
  const query = ui.chronicleSearch.value.trim().toLowerCase();
  const records = [...state.timeline].reverse().filter((item) => {
    if (!query) return true;
    return `${item.source} ${item.sequence} ${item.record?.type} ${item.record?.message_id} ${JSON.stringify(item.record?.payload || {})}`.toLowerCase().includes(query);
  });
  if (!records.length) {
    empty(ui.timeline, query ? "No matching Chronicle facts" : "Chronicle is empty", query ? "Change the filter to inspect other accepted records." : "No records have been projected by orclinkd.");
    return;
  }
  ui.timeline.replaceChildren();
  for (const item of records) {
    const row = el("div", "chronicle-item");
    const identity = el("div");
    identity.append(el("strong", "", eventTitle(item)), el("small", "", `${item.source} · #${item.sequence}`));
    const payload = JSON.stringify(item.record?.payload || {});
    row.append(el("time", "", timeLabel(item.received_at)), el("span", "event-dot"), identity, el("code", "event-data", payload));
    ui.timeline.append(row);
  }
}

function detail(label, value) {
  const row = el("div", "detail-row");
  row.append(el("span", "", label), el("strong", "", valueOr(value)));
  return row;
}

function openNode(id) {
  const node = state.nodes.find((candidate) => candidate.id === id);
  if (!node) return;
  ui.nodeDialogContent.replaceChildren();
  const header = el("div", "node-dialog-header");
  const copy = el("div");
  copy.append(el("h2", "", node.name), el("p", "", `${node.kind} · ${node.id}`));
  header.append(el("span", "node-icon", nodeGlyph(node)), copy, statePill(node.status, node.isDemo));
  const details = el("div", "detail-grid");
  const network = node.network || {};
  details.append(
    detail("Address", node.address),
    detail("Ethernet", network.ethernet?.link ? "Link up" : "Not reported up"),
    detail("Wi-Fi", network.wifi?.connected ? "Connected" : network.wifi?.station_ready ? "Station ready" : "Unavailable"),
    detail("Free heap", humanBytes(node.heartbeat?.free_heap_bytes)),
    detail("Flash", humanBytes(node.hardware?.flash_bytes)),
    detail("PSRAM", humanBytes(node.hardware?.psram_bytes)),
  );
  const list = el("div", "capability-list");
  for (const capability of node.capabilities) list.append(el("span", "", capability));
  if (!node.capabilities.length) list.append(el("span", "", "No capabilities declared"));
  ui.nodeDialogContent.append(header, details, list);
  ui.nodeDialog.showModal();
}

function render() {
  renderPosture();
  renderTopology();
  renderTelemetry();
  renderApprovals();
  renderRecent();
  renderNetwork();
  renderOperations();
  renderTimeline();
  renderProposalTargets();
}

function demoRecords() {
  const now = Math.floor(Date.now() / 1000);
  const snapshots = [
    {
      type: "node_snapshot",
      message_id: "preview_waveshare_snapshot",
      protocol_version: { major: 1, minor: 0 },
      payload: {
        __demo: true,
        node_id: "esp32p4-80f1b2d1e0d5",
        display_name: "Waveshare P4 Edge",
        connection_state: "online",
        hardware: {
          chip: "ESP32-P4 rev 1.3",
          flash_bytes: 16777216,
          psram_bytes: 33554432,
          display: { controller: "ILI9341", width: 320, height: 240 },
          network: {
            ethernet: { link: true, ip: "192.168.1.108" },
            wifi: { transport: "esp_hosted_sdio", station_ready: true, configured: false, scan_result: 10, connected: false, ip: "" },
          },
        },
        capabilities: ["waveshare_p4.device.info", "waveshare_p4.health.read", "display.status.show", "display.touch.read"],
      },
    },
    {
      type: "node_snapshot",
      message_id: "preview_orc_host_snapshot",
      protocol_version: { major: 1, minor: 0 },
      payload: {
        __demo: true,
        node_id: "theorc-workstation",
        display_name: "TheOrc Workstation",
        connection_state: "online",
        host: { os: "Windows", hostname: "newcorepc", process_count: 184 },
        capabilities: ["host.inventory.read", "theorc.proposal.create"],
      },
    },
  ];
  const timeline = [
    { sequence: 101, source: "waveshare-p4.serial", received_at: now - 22, record: snapshots[0] },
    { sequence: 102, source: "host.local", received_at: now - 18, record: snapshots[1] },
    {
      sequence: 103,
      source: "waveshare-p4.serial",
      received_at: now - 4,
      record: {
        type: "heartbeat",
        message_id: "preview_heartbeat_42",
        payload: {
          node_id: "esp32p4-80f1b2d1e0d5",
          sequence: 42,
          uptime_ms: 143200,
          free_heap_bytes: 2784912,
          display_ready: true,
          touch_ready: true,
          network_link: true,
          network_ip: "192.168.1.108",
          wifi_station_ready: true,
          wifi_configured: false,
          wifi_connected: false,
        },
      },
    },
    {
      sequence: 104,
      source: "orclinkd.policy",
      received_at: now - 2,
      record: { type: "event", message_id: "preview_policy_event", payload: { node_id: "esp32p4-80f1b2d1e0d5", event: "snapshot_admitted", policy: "local-authority" } },
    },
  ];
  const operations = [
    { type: "command_update", message_id: "preview_command_1", payload: { __demo: true, command_id: "cmd_display_status", capability: "display.status.show", node_id: "esp32p4-80f1b2d1e0d5", risk: "R0", state: "Succeeded", outcome: "executed_verified" } },
    { type: "command_update", message_id: "preview_command_2", payload: { __demo: true, command_id: "cmd_wifi_provision", capability: "network.wifi.provision", node_id: "esp32p4-80f1b2d1e0d5", risk: "R2", state: "Proposed", outcome: "not_executed" } },
  ];
  return { snapshots, operations, timeline };
}

function useDemo() {
  const data = demoRecords();
  state.timeline = data.timeline;
  state.nodes = normalizeNodes(data.snapshots, data.timeline);
  state.operations = normalizeOperations(data.operations);
  state.catalog = [];
  state.lastRefresh = new Date();
  setConnection("demo");
  ui.lastUpdated.textContent = "Preview data · not device state";
  render();
}

async function refresh() {
  ui.refresh.classList.add("busy");
  ui.refresh.disabled = true;
  ui.connection.textContent = "Connecting to orclinkd";
  ui.connectionDot.className = "status-dot";
  try {
    const [snapshots, operations, timeline, catalog] = await Promise.all([
      read("snapshot"),
      read("operations"),
      read("timeline"),
      read("v1/capabilities").catch(() => []),
    ]);
    state.timeline = timeline;
    state.nodes = normalizeNodes(snapshots, timeline);
    state.operations = normalizeOperations(operations);
    state.catalog = catalog;
    state.lastRefresh = new Date();
    setConnection("live");
    ui.lastUpdated.textContent = `Refreshed ${state.lastRefresh.toLocaleTimeString()}`;
  } catch (error) {
    state.nodes = [];
    state.operations = [];
    state.timeline = [];
    state.catalog = [];
    state.lastRefresh = null;
    setConnection("offline", error.message);
    ui.lastUpdated.textContent = "No authoritative snapshot";
  } finally {
    ui.refresh.classList.remove("busy");
    ui.refresh.disabled = false;
    render();
  }
}

function showView(name) {
  for (const view of document.querySelectorAll(".view")) view.classList.toggle("active", view.id === `view-${name}`);
  for (const item of document.querySelectorAll(".nav-item")) item.classList.toggle("active", item.dataset.view === name);
  document.querySelector("#view-title").textContent = viewTitles[name];
  document.querySelector("#workspace").focus({ preventScroll: true });
}

for (const item of document.querySelectorAll(".nav-item")) item.addEventListener("click", () => showView(item.dataset.view));
for (const item of document.querySelectorAll("[data-go]")) item.addEventListener("click", () => showView(item.dataset.go));

for (const button of document.querySelectorAll("[data-operation-filter]")) {
  button.addEventListener("click", () => {
    state.operationFilter = button.dataset.operationFilter;
    for (const candidate of document.querySelectorAll("[data-operation-filter]")) candidate.classList.toggle("active", candidate === button);
    renderOperations();
  });
}

for (const button of document.querySelectorAll("[data-prompt]")) {
  button.addEventListener("click", () => {
    document.querySelector("#orc-request").value = button.dataset.prompt;
    document.querySelector("#orc-request").focus();
  });
}

document.querySelector("#orc-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const request = document.querySelector("#orc-request").value.trim();
  ui.proposalResult.hidden = false;
  const target = ui.proposalTarget.value;
  const capabilityName = ui.proposalCapability.value;
  const capability = state.catalog.find((entry) => entry.name === capabilityName && entry.nodes?.includes(target));
  if (!request || !target || !capability?.invokable || !capability.risk_floor) {
    ui.proposalResult.textContent = "Choose an invokable target capability and describe the intended outcome.";
    return;
  }
  const proposalId = `control-room:${crypto.randomUUID()}`;
  const submit = event.currentTarget.querySelector("button[type=submit]");
  submit.disabled = true;
  ui.proposalResult.textContent = "Submitting typed intent to orclinkd…";
  try {
    const response = await fetch(`${API}/v1/proposals`, {
      method: "POST",
      cache: "no-store",
      headers: { "Content-Type": "application/json", "X-OrcLink-Client": "control-room/0.1" },
      body: JSON.stringify({
        schema_version: 1,
        proposal_id: proposalId,
        requester_id: "control-room.local",
        target: { node_id: target, component_id: null },
        capability: { name: capabilityName, schema_version: capability.schema_version },
        arguments: { intent: request },
        risk: capability.risk_floor,
        approval_required: capability.risk_floor !== "R0",
        idempotency_key: proposalId,
      }),
    });
    const receipt = await response.json();
    if (!response.ok && !receipt.state) throw new Error(receipt.error?.message || `HTTP ${response.status}`);
    ui.proposalResult.textContent = `${receipt.proposal_id}: ${sentence(receipt.state)} · ${sentence(receipt.reason)}${receipt.approval_required ? " · human approval required" : ""}`;
    await refresh();
  } catch (error) {
    ui.proposalResult.textContent = `Proposal rejected: ${error.message}`;
  } finally {
    submit.disabled = false;
  }
});

ui.proposalTarget.addEventListener("change", renderProposalCapabilities);

ui.refresh.addEventListener("click", refresh);
ui.demoButton.addEventListener("click", () => (state.mode === "demo" ? refresh() : useDemo()));
ui.chronicleSearch.addEventListener("input", renderTimeline);
document.querySelector("#settings-button").addEventListener("click", () => ui.connectionDialog.showModal());
document.querySelector(".operator").addEventListener("click", () => ui.connectionDialog.showModal());

refresh();
