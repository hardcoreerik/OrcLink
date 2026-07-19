use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::{
    collections::{HashMap, HashSet},
    io::BufRead,
};
use time::OffsetDateTime;

#[derive(Debug, Deserialize, Serialize)]
#[serde(untagged)]
pub enum Record {
    Node(Node),
    Event(Event),
    Capability(Capability),
    Command(Command),
    AdapterEnvelope(AdapterEnvelope),
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Node {
    pub schema_version: u64,
    pub id: String,
    pub display_name: String,
    pub kind: String,
    pub trust_state: TrustState,
    pub connection_state: ConnectionState,
    pub capabilities: Vec<CapabilityRef>,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Event {
    pub schema_version: u64,
    pub id: String,
    pub source_id: String,
    pub source_sequence: Option<u64>,
    pub source_time: String,
    pub received_at: String,
    pub kind: String,
    pub correlation_id: Option<String>,
    pub payload: Map<String, Value>,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Capability {
    pub schema_version: u64,
    pub name: String,
    pub risk: Risk,
    pub availability: Availability,
    pub arguments_schema: Map<String, Value>,
    pub result_schema: Map<String, Value>,
    pub timeout_ms: u64,
    pub concurrency: Concurrency,
    pub idempotency: Idempotency,
    pub cancellation: Cancellation,
    pub verification: Verification,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Command {
    pub schema_version: u64,
    pub id: String,
    pub requester_id: String,
    pub target: Target,
    pub capability: CapabilityRef,
    pub arguments: Map<String, Value>,
    pub risk: Risk,
    pub state: CommandState,
    pub requested_at: String,
    pub deadline: String,
    pub idempotency_key: Option<String>,
    pub outcome: Option<Outcome>,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct TheOrcProposal {
    pub schema_version: u64,
    pub proposal_id: String,
    pub requester_id: String,
    pub target: Target,
    pub capability: CapabilityRef,
    pub arguments: Map<String, Value>,
    pub risk: Risk,
    pub approval_required: bool,
    pub idempotency_key: String,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct AdapterEnvelope {
    #[serde(rename = "type")]
    pub message_type: MessageType,
    pub message_id: String,
    pub protocol_version: ProtocolVersion,
    pub payload: Map<String, Value>,
    pub in_reply_to: Option<String>,
    pub adapter_instance_id: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct CapabilityRef {
    pub name: String,
    pub schema_version: u64,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Target {
    pub node_id: String,
    pub component_id: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProtocolVersion {
    pub major: u64,
    pub minor: u64,
}

macro_rules! wire_enum {
    ($name:ident { $($variant:ident $(= $wire:literal)?),+ $(,)? }) => {
        #[derive(Debug, Deserialize, Serialize)]
        pub enum $name {
            $($(#[serde(rename = $wire)])? $variant),+
        }
    };
}

wire_enum!(Risk { R0, R1, R2, R3 });
wire_enum!(TrustState { Quarantined = "quarantined", Paired = "paired", Revoked = "revoked" });
wire_enum!(ConnectionState { Online = "online", Degraded = "degraded", Offline = "offline", Resynchronizing = "resynchronizing" });
wire_enum!(Availability { OnlineOnly = "online_only", OnlineOrOffline = "online_or_offline", InstalledOfflineWorkflow = "installed_offline_workflow" });
wire_enum!(Concurrency { Parallel = "parallel", SerialPerNode = "serial_per_node", Singleton = "singleton" });
wire_enum!(Idempotency { Safe = "safe", Conditional = "conditional", Unsafe = "unsafe", None = "none" });
wire_enum!(Cancellation { Supported = "supported", BestEffort = "best_effort", Unsupported = "unsupported" });
wire_enum!(Verification { None = "none", AdapterReported = "adapter_reported", ObservedState = "observed_state" });
wire_enum!(CommandState {
    Proposed,
    Validated,
    Authorized,
    Queued,
    Executing,
    Verifying,
    Cancelling,
    Succeeded,
    Failed,
    TimedOut,
    Cancelled,
    Rejected,
    Uncertain
});
wire_enum!(Outcome { ExecutedVerified = "executed_verified", ExecutedUnverified = "executed_unverified", NotExecuted = "not_executed", Uncertain = "uncertain" });
wire_enum!(MessageType { Hello = "hello", HelloAck = "hello_ack", NodeSnapshot = "node_snapshot", CapabilityDeclare = "capability_declare", Event = "event", CommandInvoke = "command_invoke", CommandUpdate = "command_update", Error = "error", Heartbeat = "heartbeat" });

#[derive(Debug, PartialEq, Eq)]
pub struct Summary {
    pub nodes: usize,
    pub events: usize,
    pub capabilities: usize,
    pub commands: usize,
    pub adapter_envelopes: usize,
}

impl std::fmt::Display for Summary {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "admitted: nodes={} events={} capabilities={} commands={} adapter_envelopes={}",
            self.nodes, self.events, self.capabilities, self.commands, self.adapter_envelopes
        )
    }
}

pub fn load_and_admit(reader: impl BufRead) -> Result<Summary, String> {
    let records = reader
        .lines()
        .enumerate()
        .map(|(index, line)| {
            let line = line.map_err(|error| format!("line {}: {error}", index + 1))?;
            serde_json::from_str(&line).map_err(|error| format!("line {}: {error}", index + 1))
        })
        .collect::<Result<Vec<Record>, String>>()?;
    admit(&records)
}

fn admit(records: &[Record]) -> Result<Summary, String> {
    let mut nodes = HashMap::new();
    let mut declarations = HashSet::new();
    let mut summary = Summary {
        nodes: 0,
        events: 0,
        capabilities: 0,
        commands: 0,
        adapter_envelopes: 0,
    };

    for record in records {
        match record {
            Record::Node(node) => {
                summary.nodes += 1;
                nodes.insert(
                    node.id.as_str(),
                    node.capabilities
                        .iter()
                        .map(|capability| (capability.name.as_str(), capability.schema_version))
                        .collect::<HashSet<_>>(),
                );
            }
            Record::Event(_) => summary.events += 1,
            Record::Capability(capability) => {
                summary.capabilities += 1;
                declarations.insert((capability.name.as_str(), capability.schema_version));
            }
            Record::Command(_) => summary.commands += 1,
            Record::AdapterEnvelope(envelope) => {
                summary.adapter_envelopes += 1;
                if envelope.protocol_version.major != 1 {
                    return Err(format!(
                        "unsupported protocol major {}",
                        envelope.protocol_version.major
                    ));
                }
            }
        }
    }

    for (node_id, capabilities) in &nodes {
        for capability in capabilities {
            if !declarations.contains(capability) {
                return Err(format!(
                    "node {node_id} references missing capability {} schema {}",
                    capability.0, capability.1
                ));
            }
        }
    }

    for record in records {
        let Record::Command(command) = record else {
            continue;
        };
        let node_capabilities = nodes
            .get(command.target.node_id.as_str())
            .ok_or_else(|| format!("unknown target node {}", command.target.node_id))?;
        let capability = (
            command.capability.name.as_str(),
            command.capability.schema_version,
        );
        if !node_capabilities.contains(&capability) {
            return Err(format!(
                "node {} does not declare capability {} schema {}",
                command.target.node_id, capability.0, capability.1
            ));
        }
        if !declarations.contains(&capability) {
            return Err(format!(
                "missing capability declaration {} schema {}",
                capability.0, capability.1
            ));
        }
        let requested_at = OffsetDateTime::parse(
            &command.requested_at,
            &time::format_description::well_known::Rfc3339,
        )
        .map_err(|error| format!("command {} requested_at: {error}", command.id))?;
        let deadline = OffsetDateTime::parse(
            &command.deadline,
            &time::format_description::well_known::Rfc3339,
        )
        .map_err(|error| format!("command {} deadline: {error}", command.id))?;
        if deadline <= requested_at {
            return Err(format!(
                "command {} deadline is not later than requested_at",
                command.id
            ));
        }
    }

    Ok(summary)
}
