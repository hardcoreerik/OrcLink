const API = "http://127.0.0.1:4017/api";
const connection = document.querySelector("#connection");
const connectionDot = document.querySelector("#connection-dot");

function show(containerId, items, describe, emptyText) {
  const container = document.querySelector(containerId);
  container.replaceChildren();
  if (!items.length) {
    const empty = document.createElement("p");
    empty.className = "empty";
    empty.textContent = emptyText;
    container.append(empty);
    return;
  }
  for (const item of items) {
    const row = document.createElement("article");
    const [title, detail] = describe(item);
    const heading = document.createElement("h3");
    const text = document.createElement("p");
    heading.textContent = title;
    text.textContent = detail;
    row.append(heading, text);
    container.append(row);
  }
}

async function read(path) {
  const response = await fetch(`${API}/${path}`);
  if (!response.ok) throw new Error(`${path}: HTTP ${response.status}`);
  return response.json();
}

async function refresh() {
  connection.textContent = "Connecting";
  connectionDot.className = "pending";
  try {
    const [nodes, operations, timeline] = await Promise.all([
      read("snapshot"),
      read("operations"),
      read("timeline"),
    ]);
    show(
      "#nodes",
      nodes,
      ({ payload }) => [
        payload.node_id,
        payload.host
          ? `${payload.host.os} · ${payload.host.process_count} processes`
          : `Ollama ${payload.server_version} · ${payload.installed_models.length} models`,
      ],
      "No node snapshots",
    );
    show(
      "#operations",
      operations,
      ({ message_id, payload }) => [message_id, `${payload.state} · ${payload.outcome}`],
      "No operations recorded",
    );
    show(
      "#timeline",
      timeline,
      ({ sequence, source, record }) => [
        `#${sequence} ${record.type}`,
        `${source} · ${record.message_id}`,
      ],
      "No timeline records",
    );
    connection.textContent = "Daemon online";
    connectionDot.className = "online";
  } catch (error) {
    connection.textContent = `Daemon unavailable · ${error.message}`;
    connectionDot.className = "offline";
  }
}

document.querySelector("#refresh").addEventListener("click", refresh);
refresh();
