use rusqlite::{Connection, OptionalExtension, params};
use std::{collections::HashMap, path::Path};

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
