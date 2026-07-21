use orclink_protocol::{AdapterEnvelope, ProposalReceipt, ProposalState, Risk, TheOrcProposal};
use rusqlite::{Connection, OptionalExtension, params};
use serde_json::{Map, Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    io::BufRead,
    path::Path,
    time::{SystemTime, UNIX_EPOCH},
};

pub const API_VERSION: &str = "1.0";
pub const MAX_REQUEST_BYTES: usize = 64 * 1024;
const DEFAULT_TIMELINE_LIMIT: usize = 200;
const MAX_TIMELINE_LIMIT: usize = 1_000;

type CapabilityTargets = (BTreeSet<String>, BTreeSet<String>);
type CapabilityMap = BTreeMap<String, CapabilityTargets>;

pub struct ApiResponse {
    pub status: u16,
    pub body: Value,
}

impl ApiResponse {
    fn json(status: u16, body: Value) -> Self {
        Self { status, body }
    }

    fn error(status: u16, code: &str, message: impl Into<String>) -> Self {
        Self::json(
            status,
            json!({"error":{"code":code,"message":message.into()}}),
        )
    }
}

pub struct Store {
    database: Connection,
    started_at: u64,
}

impl Store {
    pub fn open(path: impl AsRef<Path>) -> Result<Self, String> {
        let database = Connection::open(path).map_err(|error| error.to_string())?;
        database
            .execute_batch(
                "CREATE TABLE IF NOT EXISTS adapter_records (
                    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                    source TEXT NOT NULL,
                    message_id TEXT NOT NULL,
                    message_type TEXT NOT NULL,
                    received_at INTEGER NOT NULL,
                    record_json TEXT NOT NULL,
                    UNIQUE(source, message_id)
                );
                CREATE TABLE IF NOT EXISTS command_proposals (
                    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                    proposal_id TEXT NOT NULL UNIQUE,
                    idempotency_key TEXT NOT NULL UNIQUE,
                    requester_id TEXT NOT NULL,
                    target_node TEXT NOT NULL,
                    capability TEXT NOT NULL,
                    risk TEXT NOT NULL,
                    approval_required INTEGER NOT NULL,
                    state TEXT NOT NULL,
                    reason TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    proposal_json TEXT
                );",
            )
            .map_err(|error| error.to_string())?;
        Ok(Self {
            database,
            started_at: unix_seconds()?,
        })
    }

    pub fn ingest(&mut self, source: &str, reader: impl BufRead) -> Result<usize, String> {
        let transaction = self
            .database
            .transaction()
            .map_err(|error| error.to_string())?;
        let mut inserted = 0;
        for (index, line) in reader.lines().enumerate() {
            let line = line.map_err(|error| format!("line {}: {error}", index + 1))?;
            let envelope: AdapterEnvelope = serde_json::from_str(&line)
                .map_err(|error| format!("line {}: {error}", index + 1))?;
            if envelope.protocol_version.major != 1 {
                return Err(format!("line {}: unsupported protocol major", index + 1));
            }
            let record = serde_json::to_value(&envelope).map_err(|error| error.to_string())?;
            let message_type = record["type"]
                .as_str()
                .ok_or_else(|| format!("line {}: missing message type", index + 1))?;
            inserted += transaction
                .execute(
                    "INSERT OR IGNORE INTO adapter_records (source, message_id, message_type, received_at, record_json) VALUES (?1, ?2, ?3, ?4, ?5)",
                    params![source, envelope.message_id, message_type, unix_seconds()? as i64, record.to_string()],
                )
                .map_err(|error| error.to_string())?;
        }
        transaction.commit().map_err(|error| error.to_string())?;
        Ok(inserted)
    }

    pub fn response(&self, path: &str) -> Result<Value, String> {
        match path {
            "/api/health" => Ok(json!({"status":"ok"})),
            "/api/snapshot" => self.records(Some("node_snapshot"), false, 0, usize::MAX),
            "/api/operations" => self.records(Some("command_update"), false, 0, usize::MAX),
            "/api/timeline" => self.records(None, true, 0, usize::MAX),
            _ => Err("not found".into()),
        }
    }

    pub fn handle(&mut self, method: &str, target: &str, body: &[u8]) -> ApiResponse {
        if method == "OPTIONS" {
            return ApiResponse::json(204, Value::Null);
        }
        let (path, query) = target.split_once('?').unwrap_or((target, ""));
        let result = match (method, path) {
            ("GET", "/api/health") => Ok(json!({"status":"ok"})),
            ("GET", "/api/snapshot") => self.records(Some("node_snapshot"), false, 0, usize::MAX),
            ("GET", "/api/operations") => {
                self.records(Some("command_update"), false, 0, usize::MAX)
            }
            ("GET", "/api/timeline") => self.records(None, true, 0, usize::MAX),
            ("GET", "/api/v1/status") => self.status(),
            ("GET", "/api/v1/nodes") => self.latest_nodes(),
            ("GET", "/api/v1/capabilities") => self.capabilities(),
            ("GET", "/api/v1/operations") => {
                self.records(Some("command_update"), false, 0, usize::MAX)
            }
            ("GET", "/api/v1/timeline") => {
                let after = query_usize(query, "after").unwrap_or(0);
                let limit = query_usize(query, "limit")
                    .unwrap_or(DEFAULT_TIMELINE_LIMIT)
                    .min(MAX_TIMELINE_LIMIT);
                self.records(None, true, after, limit)
            }
            ("GET", "/api/v1/openapi.json") => Ok(openapi()),
            ("POST", "/api/v1/proposals") => return self.propose(body),
            _ if method == "GET" && path.starts_with("/api/v1/nodes/") => {
                return match self.node(path.trim_start_matches("/api/v1/nodes/")) {
                    Ok(value) => ApiResponse::json(200, value),
                    Err(_) => ApiResponse::error(404, "UNKNOWN_NODE", "node not found"),
                };
            }
            _ if method == "GET" && path.starts_with("/api/v1/proposals/") => {
                return match self.proposal(path.trim_start_matches("/api/v1/proposals/")) {
                    Ok(value) => ApiResponse::json(200, value),
                    Err(_) => ApiResponse::error(404, "NOT_FOUND", "proposal not found"),
                };
            }
            _ => return ApiResponse::error(404, "NOT_FOUND", "API route not found"),
        };
        match result {
            Ok(value) => ApiResponse::json(200, value),
            Err(error) => ApiResponse::error(500, "INTERNAL", error),
        }
    }

    fn status(&self) -> Result<Value, String> {
        let records = count(&self.database, "adapter_records")?;
        let proposals = count(&self.database, "command_proposals")?;
        let nodes = self.latest_node_map()?.len();
        let capabilities = self.capability_map()?.len();
        Ok(json!({
            "status":"ok",
            "api_version":API_VERSION,
            "daemon_version":env!("CARGO_PKG_VERSION"),
            "protocol":{"major":1,"minor":0},
            "started_at":self.started_at,
            "uptime_seconds":unix_seconds()?.saturating_sub(self.started_at),
            "counts":{"nodes":nodes,"capabilities":capabilities,"records":records,"proposals":proposals},
            "authority":"orclinkd"
        }))
    }

    fn records(
        &self,
        message_type: Option<&str>,
        timeline: bool,
        after: usize,
        limit: usize,
    ) -> Result<Value, String> {
        let (sql, parameter) = if message_type.is_some() {
            (
                "SELECT sequence, source, received_at, record_json FROM adapter_records WHERE message_type = ?1 ORDER BY sequence",
                message_type,
            )
        } else {
            (
                "SELECT sequence, source, received_at, record_json FROM adapter_records ORDER BY sequence",
                None,
            )
        };
        let mut statement = self
            .database
            .prepare(sql)
            .map_err(|error| error.to_string())?;
        let mut rows = match parameter {
            Some(value) => statement.query([value]),
            None => statement.query([]),
        }
        .map_err(|error| error.to_string())?;
        let mut records = Vec::new();
        while let Some(row) = rows.next().map_err(|error| error.to_string())? {
            let sequence = row.get::<_, i64>(0).map_err(|error| error.to_string())?;
            if sequence <= after as i64 {
                continue;
            }
            let record: String = row.get(3).map_err(|error| error.to_string())?;
            let record: Value = serde_json::from_str(&record).map_err(|error| error.to_string())?;
            records.push(if timeline {
                json!({
                    "sequence":sequence,
                    "source":row.get::<_,String>(1).map_err(|error| error.to_string())?,
                    "received_at":row.get::<_,i64>(2).map_err(|error| error.to_string())?,
                    "record":record
                })
            } else {
                record
            });
            if records.len() >= limit {
                break;
            }
        }
        Ok(Value::Array(records))
    }

    fn latest_node_map(&self) -> Result<BTreeMap<String, Value>, String> {
        let records = self.records(Some("node_snapshot"), false, 0, usize::MAX)?;
        let mut nodes = BTreeMap::new();
        for record in records.as_array().into_iter().flatten() {
            if let Some(node_id) = record["payload"]["node_id"].as_str() {
                nodes.insert(node_id.to_owned(), record.clone());
            }
        }
        Ok(nodes)
    }

    fn latest_nodes(&self) -> Result<Value, String> {
        Ok(Value::Array(
            self.latest_node_map()?.into_values().collect(),
        ))
    }

    fn node(&self, node_id: &str) -> Result<Value, String> {
        self.latest_node_map()?
            .remove(node_id)
            .ok_or_else(|| "node not found".into())
    }

    fn capability_map(&self) -> Result<CapabilityMap, String> {
        let mut capabilities = CapabilityMap::new();
        for (node_id, envelope) in self.latest_node_map()? {
            let online = envelope["payload"]["connection_state"] == "online";
            for capability in envelope["payload"]["capabilities"]
                .as_array()
                .into_iter()
                .flatten()
                .filter_map(Value::as_str)
            {
                let targets = capabilities.entry(capability.to_owned()).or_default();
                targets.0.insert(node_id.clone());
                if online {
                    targets.1.insert(node_id.clone());
                }
            }
        }
        Ok(capabilities)
    }

    fn capabilities(&self) -> Result<Value, String> {
        Ok(Value::Array(
            self.capability_map()?
                .into_iter()
                .map(|(name, (nodes, online_nodes))| {
                    let floor = capability_floor(&name);
                    json!({
                        "name":name,
                        "schema_version":1,
                        "risk_floor":floor,
                        "invokable":floor.is_some() && !online_nodes.is_empty(),
                        "nodes":nodes.into_iter().collect::<Vec<_>>(),
                        "online_nodes":online_nodes.into_iter().collect::<Vec<_>>()
                    })
                })
                .collect(),
        ))
    }

    fn propose(&mut self, body: &[u8]) -> ApiResponse {
        if body.len() > MAX_REQUEST_BYTES {
            return ApiResponse::error(413, "REQUEST_TOO_LARGE", "proposal exceeds 64 KiB");
        }
        let proposal: TheOrcProposal = match serde_json::from_slice(body) {
            Ok(proposal) => proposal,
            Err(error) => {
                return ApiResponse::error(400, "INVALID_MESSAGE", error.to_string());
            }
        };
        if let Ok(Some(mut receipt)) = self.receipt_by_idempotency(&proposal.idempotency_key) {
            receipt.duplicate = true;
            return ApiResponse::json(200, json!(receipt));
        }

        let decision = self.evaluate(&proposal);
        let created_at = match unix_seconds() {
            Ok(value) => value,
            Err(error) => return ApiResponse::error(500, "INTERNAL", error),
        };
        let persisted = if decision.state == ProposalState::Rejected
            && contains_sensitive(&proposal.arguments)
        {
            None
        } else {
            match serde_json::to_string(&proposal) {
                Ok(value) => Some(value),
                Err(error) => return ApiResponse::error(500, "INTERNAL", error.to_string()),
            }
        };
        let risk = risk_name(proposal.risk);
        let transaction = match self.database.transaction() {
            Ok(transaction) => transaction,
            Err(error) => return ApiResponse::error(500, "INTERNAL", error.to_string()),
        };
        if let Err(error) = transaction.execute(
            "INSERT INTO command_proposals (proposal_id,idempotency_key,requester_id,target_node,capability,risk,approval_required,state,reason,created_at,proposal_json) VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)",
            params![
                proposal.proposal_id,
                proposal.idempotency_key,
                proposal.requester_id,
                proposal.target.node_id,
                proposal.capability.name,
                risk,
                i64::from(decision.approval_required),
                proposal_state_name(decision.state),
                decision.reason,
                created_at as i64,
                persisted,
            ],
        ) {
            return ApiResponse::error(409, "CONFLICT", error.to_string());
        }
        let sequence = transaction.last_insert_rowid() as u64;
        let receipt = ProposalReceipt {
            proposal_id: proposal.proposal_id.clone(),
            state: decision.state,
            reason: decision.reason.to_owned(),
            risk: proposal.risk,
            approval_required: decision.approval_required,
            duplicate: false,
            sequence,
            created_at,
        };
        let record = json!({
            "type":"command_update",
            "message_id":format!("proposal_{}", receipt.sequence),
            "protocol_version":{"major":1,"minor":0},
            "payload":{
                "command_id":proposal.proposal_id,
                "requester_id":proposal.requester_id,
                "node_id":proposal.target.node_id,
                "capability":proposal.capability,
                "risk":proposal.risk,
                "state":receipt.state,
                "outcome":"not_executed",
                "reason":receipt.reason,
                "approval_required":receipt.approval_required,
                "proposal_sequence":receipt.sequence
            },
            "in_reply_to":null,
            "adapter_instance_id":null
        });
        if let Err(error) = transaction.execute(
            "INSERT INTO adapter_records (source,message_id,message_type,received_at,record_json) VALUES (?1,?2,'command_update',?3,?4)",
            params!["orclinkd.proposal", format!("proposal_{}", receipt.sequence), receipt.created_at as i64, record.to_string()],
        ) {
            return ApiResponse::error(500, "INTERNAL", error.to_string());
        }
        if let Err(error) = transaction.commit() {
            return ApiResponse::error(500, "INTERNAL", error.to_string());
        }
        ApiResponse::json(
            if receipt.state == ProposalState::Rejected {
                422
            } else {
                201
            },
            json!(receipt),
        )
    }

    fn evaluate(&self, proposal: &TheOrcProposal) -> Decision {
        let rejected = |reason: &'static str| Decision {
            state: ProposalState::Rejected,
            reason,
            approval_required: false,
        };
        if proposal.schema_version != 1 {
            return rejected("unsupported_schema_version");
        }
        if !valid_id(&proposal.proposal_id)
            || !valid_id(&proposal.requester_id)
            || !valid_id(&proposal.target.node_id)
            || !valid_id(&proposal.idempotency_key)
            || !valid_capability(&proposal.capability.name)
            || proposal.capability.schema_version != 1
        {
            return rejected("invalid_identifier_or_schema");
        }
        if contains_sensitive(&proposal.arguments) {
            return rejected("secret_values_require_protected_references");
        }
        let Ok(nodes) = self.latest_node_map() else {
            return rejected("registry_unavailable");
        };
        let Some(node) = nodes.get(&proposal.target.node_id) else {
            return rejected("unknown_target_node");
        };
        if node["payload"]["connection_state"] != "online" {
            return rejected("target_unavailable");
        }
        let observed = node["payload"]["capabilities"]
            .as_array()
            .into_iter()
            .flatten()
            .filter_map(Value::as_str)
            .any(|name| name == proposal.capability.name);
        if !observed {
            return rejected("capability_not_declared_by_target");
        }
        let Some(floor) = capability_floor(&proposal.capability.name) else {
            return rejected("capability_has_no_fixed_risk_floor");
        };
        if risk_rank(proposal.risk) < risk_rank(floor) {
            return rejected("risk_below_capability_floor");
        }
        let approval_required = proposal.approval_required || proposal.risk != Risk::R0;
        Decision {
            state: if approval_required {
                ProposalState::ApprovalRequired
            } else {
                ProposalState::Proposed
            },
            reason: if approval_required {
                "human_approval_required"
            } else {
                "validated_for_policy_evaluation"
            },
            approval_required,
        }
    }

    fn receipt_by_idempotency(&self, key: &str) -> Result<Option<ProposalReceipt>, String> {
        self.receipt(
            "SELECT sequence,proposal_id,state,reason,risk,approval_required,created_at FROM command_proposals WHERE idempotency_key=?1",
            key,
        )
    }

    fn proposal(&self, id: &str) -> Result<Value, String> {
        self.receipt(
            "SELECT sequence,proposal_id,state,reason,risk,approval_required,created_at FROM command_proposals WHERE proposal_id=?1",
            id,
        )?
        .map(|receipt| json!(receipt))
        .ok_or_else(|| "proposal not found".into())
    }

    fn receipt(&self, sql: &str, value: &str) -> Result<Option<ProposalReceipt>, String> {
        self.database
            .query_row(sql, [value], |row| {
                let state: String = row.get(2)?;
                let risk: String = row.get(4)?;
                Ok(ProposalReceipt {
                    sequence: row.get::<_, i64>(0)? as u64,
                    proposal_id: row.get(1)?,
                    state: parse_proposal_state(&state),
                    reason: row.get(3)?,
                    risk: parse_risk(&risk),
                    approval_required: row.get::<_, i64>(5)? != 0,
                    duplicate: false,
                    created_at: row.get::<_, i64>(6)? as u64,
                })
            })
            .optional()
            .map_err(|error| error.to_string())
    }
}

struct Decision {
    state: ProposalState,
    reason: &'static str,
    approval_required: bool,
}

fn unix_seconds() -> Result<u64, String> {
    Ok(SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|error| error.to_string())?
        .as_secs())
}

fn count(database: &Connection, table: &str) -> Result<u64, String> {
    database
        .query_row(&format!("SELECT COUNT(*) FROM {table}"), [], |row| {
            row.get::<_, i64>(0)
        })
        .map(|value| value as u64)
        .map_err(|error| error.to_string())
}

fn query_usize(query: &str, name: &str) -> Option<usize> {
    query.split('&').find_map(|part| {
        let (key, value) = part.split_once('=')?;
        (key == name).then(|| value.parse().ok()).flatten()
    })
}

fn valid_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 128
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"._:-".contains(&byte))
}

fn valid_capability(value: &str) -> bool {
    valid_id(value) && value.contains('.')
}

fn contains_sensitive(arguments: &Map<String, Value>) -> bool {
    fn sensitive_key(key: &str) -> bool {
        let key = key.to_ascii_lowercase();
        key.contains("password")
            || key.contains("passwd")
            || key.contains("secret")
            || key.contains("token")
            || key.contains("credential")
            || key == "psk"
            || key.ends_with("_key")
    }

    fn visit(value: &Value) -> bool {
        match value {
            Value::Object(object) => object
                .iter()
                .any(|(key, value)| sensitive_key(key) || visit(value)),
            Value::Array(array) => array.iter().any(visit),
            _ => false,
        }
    }
    arguments
        .iter()
        .any(|(key, value)| sensitive_key(key) || visit(value))
}

fn risk_rank(risk: Risk) -> u8 {
    match risk {
        Risk::R0 => 0,
        Risk::R1 => 1,
        Risk::R2 => 2,
        Risk::R3 => 3,
    }
}

fn risk_name(risk: Risk) -> &'static str {
    match risk {
        Risk::R0 => "R0",
        Risk::R1 => "R1",
        Risk::R2 => "R2",
        Risk::R3 => "R3",
    }
}

fn parse_risk(value: &str) -> Risk {
    match value {
        "R1" => Risk::R1,
        "R2" => Risk::R2,
        "R3" => Risk::R3,
        _ => Risk::R0,
    }
}

fn proposal_state_name(state: ProposalState) -> &'static str {
    match state {
        ProposalState::Proposed => "Proposed",
        ProposalState::ApprovalRequired => "ApprovalRequired",
        ProposalState::Rejected => "Rejected",
    }
}

fn parse_proposal_state(value: &str) -> ProposalState {
    match value {
        "ApprovalRequired" => ProposalState::ApprovalRequired,
        "Rejected" => ProposalState::Rejected,
        _ => ProposalState::Proposed,
    }
}

fn capability_floor(name: &str) -> Option<Risk> {
    match name {
        "host.inventory.read"
        | "host.service.status"
        | "ollama.model.list"
        | "klipper.status.read"
        | "klipper.temperature.read"
        | "m5tab5.device.info"
        | "m5tab5.health.read"
        | "m5tab5.input.touch"
        | "m5tab5.event.journal.sync"
        | "m5tab5.network.inventory"
        | "m5tab5.power.read"
        | "waveshare_p4.device.info"
        | "waveshare_p4.health.read"
        | "display.touch.read" => Some(Risk::R0),
        "ollama.generate" | "m5tab5.display.status" | "display.status.show" | "rtl_sdr.capture" => {
            Some(Risk::R1)
        }
        "host.service.start"
        | "host.service.stop"
        | "host.service.restart"
        | "m5tab5.network.configure"
        | "rtl_sdr.decode" => Some(Risk::R2),
        _ => None,
    }
}

fn openapi() -> Value {
    json!({
        "openapi":"3.1.0",
        "info":{"title":"OrcLink Local API","version":API_VERSION,"description":"Loopback-first API. orclinkd remains the policy authority."},
        "servers":[{"url":"http://127.0.0.1:4017"}],
        "paths":{
            "/api/v1/status":{"get":{"summary":"Daemon and registry status"}},
            "/api/v1/nodes":{"get":{"summary":"Latest node projections"}},
            "/api/v1/nodes/{node_id}":{"get":{"summary":"Latest projection for one node"}},
            "/api/v1/capabilities":{"get":{"summary":"Observed capability catalog and fixed risk floors"}},
            "/api/v1/operations":{"get":{"summary":"Command lifecycle records"}},
            "/api/v1/timeline":{"get":{"summary":"Bounded Chronicle facts","parameters":[{"name":"after","in":"query"},{"name":"limit","in":"query"}]}},
            "/api/v1/proposals":{"post":{"summary":"Submit typed intent for validation and policy evaluation"}},
            "/api/v1/proposals/{proposal_id}":{"get":{"summary":"Read one proposal receipt"}}
        }
    })
}
