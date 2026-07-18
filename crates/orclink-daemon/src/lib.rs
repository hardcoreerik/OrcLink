use orclink_protocol::AdapterEnvelope;
use rusqlite::{Connection, params};
use serde_json::{Value, json};
use std::{
    io::BufRead,
    path::Path,
    time::{SystemTime, UNIX_EPOCH},
};

pub struct Store {
    database: Connection,
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
                );",
            )
            .map_err(|error| error.to_string())?;
        Ok(Self { database })
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
            let received_at: i64 = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map_err(|error| error.to_string())?
                .as_secs()
                .try_into()
                .map_err(|_| "system time exceeds SQLite integer range")?;
            inserted += transaction
                .execute(
                    "INSERT OR IGNORE INTO adapter_records (source, message_id, message_type, received_at, record_json) VALUES (?1, ?2, ?3, ?4, ?5)",
                    params![source, envelope.message_id, message_type, received_at, record.to_string()],
                )
                .map_err(|error| error.to_string())?;
        }
        transaction.commit().map_err(|error| error.to_string())?;
        Ok(inserted)
    }

    pub fn response(&self, path: &str) -> Result<Value, String> {
        match path {
            "/api/health" => Ok(json!({"status":"ok"})),
            "/api/snapshot" => self.records(Some("node_snapshot"), false),
            "/api/operations" => self.records(Some("command_update"), false),
            "/api/timeline" => self.records(None, true),
            _ => Err("not found".into()),
        }
    }

    fn records(&self, message_type: Option<&str>, timeline: bool) -> Result<Value, String> {
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
            let record: String = row.get(3).map_err(|error| error.to_string())?;
            let record: Value = serde_json::from_str(&record).map_err(|error| error.to_string())?;
            records.push(if timeline {
                json!({"sequence":row.get::<_,i64>(0).map_err(|error| error.to_string())?,"source":row.get::<_,String>(1).map_err(|error| error.to_string())?,"received_at":row.get::<_,i64>(2).map_err(|error| error.to_string())?,"record":record})
            } else {
                record
            });
        }
        Ok(Value::Array(records))
    }
}
