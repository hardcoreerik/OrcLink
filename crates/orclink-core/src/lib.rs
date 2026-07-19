use rusqlite::{Connection, OptionalExtension, params};
use std::{collections::HashMap, path::Path};

#[derive(Clone, Copy)]
pub enum StepCondition {
    Always,
    PreviousSucceeded,
}

pub struct WorkflowStep<'a> {
    pub name: &'a str,
    pub condition: StepCondition,
    pub approval_required: bool,
    pub max_attempts: u8,
    pub cleanup: Option<&'a str>,
}

#[derive(Debug, PartialEq, Eq)]
pub struct StepResult {
    pub name: String,
    pub attempts: u8,
    pub state: String,
    pub cleanup_state: Option<String>,
}

pub fn run_workflow(
    steps: &[WorkflowStep<'_>],
    approval_granted: bool,
    mut invoke: impl FnMut(&str, u8) -> Result<(), String>,
) -> Vec<StepResult> {
    let mut previous_succeeded = true;
    steps
        .iter()
        .map(|step| {
            if matches!(step.condition, StepCondition::PreviousSucceeded) && !previous_succeeded {
                return StepResult {
                    name: step.name.into(),
                    attempts: 0,
                    state: "Skipped".into(),
                    cleanup_state: None,
                };
            }
            if step.approval_required && !approval_granted {
                previous_succeeded = false;
                return StepResult {
                    name: step.name.into(),
                    attempts: 0,
                    state: "ApprovalRequired".into(),
                    cleanup_state: None,
                };
            }
            let mut attempts = 0;
            let mut succeeded = false;
            for attempt in 1..=step.max_attempts {
                attempts = attempt;
                if invoke(step.name, attempt).is_ok() {
                    succeeded = true;
                    break;
                }
            }
            previous_succeeded = succeeded;
            let cleanup_state = if !succeeded {
                step.cleanup.map(|cleanup| match invoke(cleanup, 1) {
                    Ok(()) => "Succeeded".into(),
                    Err(_) => "Failed".into(),
                })
            } else {
                None
            };
            StepResult {
                name: step.name.into(),
                attempts,
                state: if succeeded { "Succeeded" } else { "Failed" }.into(),
                cleanup_state,
            }
        })
        .collect()
}

#[derive(Clone, Copy)]
pub enum NodeBehavior {
    Online,
    Offline,
    Slow,
    Failing,
}

struct Node {
    capability: String,
    behavior: NodeBehavior,
}

pub struct Command<'a> {
    pub id: &'a str,
    pub target_node: &'a str,
    pub capability: &'a str,
    pub idempotency_key: &'a str,
    pub deadline_ms: u64,
    pub policy_allows: bool,
    pub cancel_requested: bool,
}

pub struct Simulator {
    database: Connection,
    nodes: HashMap<String, Node>,
}

impl Simulator {
    pub fn open(path: impl AsRef<Path>) -> Result<Self, String> {
        let database = Connection::open(path).map_err(|error| error.to_string())?;
        database
            .execute_batch(
                "CREATE TABLE IF NOT EXISTS commands (
                    id TEXT PRIMARY KEY,
                    target_node TEXT NOT NULL,
                    capability TEXT NOT NULL,
                    idempotency_key TEXT NOT NULL UNIQUE,
                    state TEXT NOT NULL
                );
                CREATE TABLE IF NOT EXISTS timeline (
                    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                    command_id TEXT NOT NULL,
                    state TEXT NOT NULL
                );",
            )
            .map_err(|error| error.to_string())?;
        Ok(Self {
            database,
            nodes: HashMap::new(),
        })
    }

    pub fn register_node(
        &mut self,
        id: impl Into<String>,
        capability: impl Into<String>,
        behavior: NodeBehavior,
    ) {
        self.nodes.insert(
            id.into(),
            Node {
                capability: capability.into(),
                behavior,
            },
        );
    }

    pub fn run(&mut self, command: &Command<'_>) -> Result<String, String> {
        let state = self.start(command)?;
        if state != "Executing" {
            return Ok(state);
        }

        if command.cancel_requested {
            self.transition(command.id, "Cancelling")?;
            self.transition(command.id, "Cancelled")?;
            return Ok("Cancelled".into());
        }

        let behavior = self
            .nodes
            .get(command.target_node)
            .ok_or_else(|| format!("unknown node {}", command.target_node))?
            .behavior;
        match behavior {
            NodeBehavior::Online | NodeBehavior::Slow => {
                // ponytail: deterministic virtual time; use a clock only when real adapters need scheduling.
                let execution_ms = match behavior {
                    NodeBehavior::Online => 10,
                    NodeBehavior::Slow => 2_000,
                    _ => unreachable!(),
                };
                if command.deadline_ms < execution_ms {
                    self.transition(command.id, "TimedOut")?;
                    return Ok("TimedOut".into());
                }
                self.transition(command.id, "Verifying")?;
                self.transition(command.id, "Succeeded")?;
                Ok("Succeeded".into())
            }
            NodeBehavior::Failing => {
                self.transition(command.id, "Failed")?;
                Ok("Failed".into())
            }
            NodeBehavior::Offline => unreachable!("offline nodes are rejected before execution"),
        }
    }

    pub fn start(&mut self, command: &Command<'_>) -> Result<String, String> {
        if let Some((id, target, capability, state)) = self
            .database
            .query_row(
                "SELECT id, target_node, capability, state FROM commands WHERE idempotency_key = ?1",
                [command.idempotency_key],
                |row| Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?, row.get::<_, String>(2)?, row.get::<_, String>(3)?)),
            )
            .optional()
            .map_err(|error| error.to_string())?
        {
            if id == command.id
                && target == command.target_node
                && capability == command.capability
            {
                return Ok(state);
            }
            return Err(format!(
                "idempotency key {} is already bound to command {id}",
                command.idempotency_key
            ));
        }

        let transaction = self
            .database
            .transaction()
            .map_err(|error| error.to_string())?;
        transaction
            .execute(
                "INSERT INTO commands (id, target_node, capability, idempotency_key, state) VALUES (?1, ?2, ?3, ?4, 'Proposed')",
                params![command.id, command.target_node, command.capability, command.idempotency_key],
            )
            .map_err(|error| error.to_string())?;
        transaction
            .execute(
                "INSERT INTO timeline (command_id, state) VALUES (?1, 'Proposed')",
                [command.id],
            )
            .map_err(|error| error.to_string())?;
        transaction.commit().map_err(|error| error.to_string())?;

        let Some(node) = self.nodes.get(command.target_node) else {
            self.transition(command.id, "Rejected")?;
            return Ok("Rejected".into());
        };
        if node.capability != command.capability
            || matches!(node.behavior, NodeBehavior::Offline)
            || !command.policy_allows
        {
            self.transition(command.id, "Rejected")?;
            return Ok("Rejected".into());
        }

        for state in ["Validated", "Authorized", "Queued", "Executing"] {
            self.transition(command.id, state)?;
        }
        Ok("Executing".into())
    }

    pub fn reconcile_after_restart(&mut self) -> Result<usize, String> {
        let mut statement = self
            .database
            .prepare(
                "SELECT id FROM commands WHERE state NOT IN ('Succeeded', 'Failed', 'TimedOut', 'Cancelled', 'Rejected', 'Uncertain') ORDER BY id",
            )
            .map_err(|error| error.to_string())?;
        let ids = statement
            .query_map([], |row| row.get::<_, String>(0))
            .map_err(|error| error.to_string())?
            .collect::<Result<Vec<_>, _>>()
            .map_err(|error| error.to_string())?;
        drop(statement);
        for id in &ids {
            self.transition(id, "Uncertain")?;
        }
        Ok(ids.len())
    }

    pub fn timeline(&self, command_id: &str) -> Result<Vec<String>, String> {
        let mut statement = self
            .database
            .prepare("SELECT state FROM timeline WHERE command_id = ?1 ORDER BY sequence")
            .map_err(|error| error.to_string())?;
        statement
            .query_map([command_id], |row| row.get(0))
            .map_err(|error| error.to_string())?
            .collect::<Result<Vec<_>, _>>()
            .map_err(|error| error.to_string())
    }

    fn transition(&mut self, command_id: &str, state: &str) -> Result<(), String> {
        let transaction = self
            .database
            .transaction()
            .map_err(|error| error.to_string())?;
        transaction
            .execute(
                "UPDATE commands SET state = ?2 WHERE id = ?1",
                params![command_id, state],
            )
            .map_err(|error| error.to_string())?;
        transaction
            .execute(
                "INSERT INTO timeline (command_id, state) VALUES (?1, ?2)",
                params![command_id, state],
            )
            .map_err(|error| error.to_string())?;
        transaction.commit().map_err(|error| error.to_string())
    }
}
