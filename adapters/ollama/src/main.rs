use orclink_protocol::{AdapterEnvelope, MessageType, ProtocolVersion};
use serde_json::{Value, json};
use std::{env, process::ExitCode};

fn get(base_url: &str, path: &str) -> Result<Value, String> {
    ureq::get(format!("{}{path}", base_url.trim_end_matches('/')))
        .call()
        .map_err(|error| error.to_string())?
        .body_mut()
        .read_json()
        .map_err(|error| error.to_string())
}

fn model_names(response: &Value) -> Vec<String> {
    let mut names = response["models"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|model| model["name"].as_str().map(str::to_owned))
        .collect::<Vec<_>>();
    names.sort();
    names
}

fn envelope(message_type: MessageType, message_id: &str, payload: Value) -> AdapterEnvelope {
    AdapterEnvelope {
        message_type,
        message_id: message_id.into(),
        protocol_version: ProtocolVersion { major: 1, minor: 0 },
        payload: payload
            .as_object()
            .expect("adapter payload is an object")
            .clone(),
        in_reply_to: None,
        adapter_instance_id: None,
    }
}

fn run(base_url: &str) -> Result<(), String> {
    let version = get(base_url, "/api/version")?;
    let installed = model_names(&get(base_url, "/api/tags")?);
    let running = model_names(&get(base_url, "/api/ps")?);
    let records = [
        envelope(
            MessageType::Hello,
            "ollama_hello_01",
            json!({"adapter":{"id":"ollama.local","version":env!("CARGO_PKG_VERSION")},"features":["telemetry"],"capability_namespaces":["ollama"],"config_schema_version":1}),
        ),
        envelope(
            MessageType::NodeSnapshot,
            "ollama_snapshot_01",
            json!({"node_id":"ollama.local","server_version":version["version"],"installed_models":installed,"running_models":running}),
        ),
    ];
    for record in records {
        println!(
            "{}",
            serde_json::to_string(&record).map_err(|error| error.to_string())?
        );
    }
    Ok(())
}

fn main() -> ExitCode {
    let base_url = env::args()
        .nth(1)
        .unwrap_or_else(|| "http://127.0.0.1:11434".into());
    match run(&base_url) {
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
    fn returns_sorted_model_names() {
        assert_eq!(
            super::model_names(&json!({"models":[{"name":"z"},{"name":"a"}]})),
            ["a", "z"]
        );
    }
}
