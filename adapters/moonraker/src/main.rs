use orclink_protocol::{AdapterEnvelope, MessageType, ProtocolVersion};
use serde_json::{Map, Value, json};
use std::{
    env,
    process::ExitCode,
    time::{SystemTime, UNIX_EPOCH},
};

fn get(base_url: &str, path: &str) -> Result<Value, String> {
    ureq::get(format!("{}{path}", base_url.trim_end_matches('/')))
        .call()
        .map_err(|error| error.to_string())?
        .body_mut()
        .read_json()
        .map_err(|error| error.to_string())
}

fn query(base_url: &str, objects: Map<String, Value>) -> Result<Value, String> {
    ureq::post(format!(
        "{}/printer/objects/query",
        base_url.trim_end_matches('/')
    ))
    .send_json(json!({"objects":objects}))
    .map_err(|error| error.to_string())?
    .body_mut()
    .read_json()
    .map_err(|error| error.to_string())
}

fn result(value: &Value) -> &Value {
    value.get("result").unwrap_or(value)
}

fn selected_objects(list: &Value) -> Map<String, Value> {
    let loaded = result(list)["objects"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(Value::as_str)
        .collect::<Vec<_>>();
    ["extruder", "heater_bed", "print_stats", "toolhead"]
        .into_iter()
        .filter(|name| loaded.contains(name))
        .map(|name| (name.into(), Value::Null))
        .collect()
}

fn envelope(message_type: MessageType, message_id: &str, payload: Value) -> AdapterEnvelope {
    AdapterEnvelope {
        message_type,
        message_id: message_id.into(),
        protocol_version: ProtocolVersion { major: 1, minor: 0 },
        payload: payload.as_object().expect("payload is an object").clone(),
        in_reply_to: None,
        adapter_instance_id: None,
    }
}

fn snapshot(base_url: &str) -> Result<Vec<AdapterEnvelope>, String> {
    let run_id = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|error| error.to_string())?
        .as_millis();
    let server = get(base_url, "/server/info")?;
    let printer = get(base_url, "/printer/info")?;
    let objects = selected_objects(&get(base_url, "/printer/objects/list")?);
    let status = if objects.is_empty() {
        json!({})
    } else {
        result(&query(base_url, objects)?)["status"].clone()
    };
    Ok(vec![
        envelope(
            MessageType::Hello,
            &format!("moonraker_hello_{run_id}"),
            json!({"adapter":{"id":"moonraker.readonly","version":env!("CARGO_PKG_VERSION")},"features":["telemetry"],"capability_namespaces":["klipper"],"config_schema_version":1}),
        ),
        envelope(
            MessageType::NodeSnapshot,
            &format!("moonraker_snapshot_{run_id}"),
            json!({
                "node_id":"klipper.local",
                "connection_state": if result(&server)["klippy_connected"] == true { "online" } else { "degraded" },
                "klippy_state":result(&server)["klippy_state"],
                "printer_state":result(&printer)["state"],
                "status":status,
                "capabilities":["klipper.status.read","klipper.temperature.read"]
            }),
        ),
    ])
}

fn run() -> Result<(), String> {
    let base_url = env::args()
        .nth(1)
        .unwrap_or_else(|| "http://127.0.0.1:7125".into());
    for record in snapshot(&base_url)? {
        println!(
            "{}",
            serde_json::to_string(&record).map_err(|error| error.to_string())?
        );
    }
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    #[test]
    fn selects_only_supported_loaded_objects() {
        let selected = super::selected_objects(&json!({
            "result":{"objects":["webhooks","extruder","heater_bed","fan"]}
        }));
        assert_eq!(selected.len(), 2);
        assert!(selected.contains_key("extruder"));
        assert!(selected.contains_key("heater_bed"));
        assert!(!selected.contains_key("fan"));
    }
}
