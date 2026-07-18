use orclink_protocol::{AdapterEnvelope, MessageType, ProtocolVersion};
use serde_json::{Value, json};
use std::{
    env,
    io::{BufRead, BufReader},
    process::ExitCode,
    time::{Duration, Instant},
};

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

fn print_record(record: &AdapterEnvelope) -> Result<(), String> {
    println!(
        "{}",
        serde_json::to_string(record).map_err(|error| error.to_string())?
    );
    Ok(())
}

fn validate_generation(model: &str, prompt: &str, maximum_tokens: u64) -> Result<(), String> {
    if model.is_empty() || model.len() > 256 {
        return Err("model must be 1-256 characters".into());
    }
    if prompt.is_empty() || prompt.len() > 4_096 {
        return Err("prompt must be 1-4096 characters".into());
    }
    if !(1..=128).contains(&maximum_tokens) {
        return Err("maximum_tokens must be 1-128".into());
    }
    Ok(())
}

fn generate(
    base_url: &str,
    model: &str,
    prompt: &str,
    maximum_tokens: u64,
    cancel_after_ms: Option<u64>,
) -> Result<Value, String> {
    validate_generation(model, prompt, maximum_tokens)?;
    if !model_names(&get(base_url, "/api/tags")?)
        .iter()
        .any(|name| name == model)
    {
        return Err(format!("model {model} is not installed"));
    }

    let mut response = ureq::post(format!("{}/api/generate", base_url.trim_end_matches('/')))
        .send_json(json!({"model":model,"prompt":prompt,"stream":true,"options":{"num_predict":maximum_tokens}}))
        .map_err(|error| error.to_string())?;
    let started = Instant::now();
    if cancel_after_ms == Some(0) {
        return Ok(json!({"state":"Cancelled","outcome":"uncertain"}));
    }

    let mut reader = BufReader::new(response.body_mut().as_reader());
    let mut line = String::new();
    let mut generated = String::new();
    loop {
        line.clear();
        if reader
            .read_line(&mut line)
            .map_err(|error| error.to_string())?
            == 0
        {
            return Err("generation stream ended without a terminal record".into());
        }
        let chunk: Value = serde_json::from_str(&line).map_err(|error| error.to_string())?;
        generated.push_str(chunk["response"].as_str().unwrap_or_default());
        if chunk["done"] == true {
            return Ok(
                json!({"state":"Succeeded","outcome":"executed_verified","response":generated,"eval_count":chunk["eval_count"]}),
            );
        }
        if cancel_after_ms.is_some_and(|limit| started.elapsed() >= Duration::from_millis(limit)) {
            return Ok(json!({"state":"Cancelled","outcome":"executed_unverified"}));
        }
    }
}

fn run_snapshot(base_url: &str) -> Result<(), String> {
    let version = get(base_url, "/api/version")?;
    let installed = model_names(&get(base_url, "/api/tags")?);
    let running = model_names(&get(base_url, "/api/ps")?);
    for record in [
        envelope(
            MessageType::Hello,
            "ollama_hello_01",
            json!({"adapter":{"id":"ollama.local","version":env!("CARGO_PKG_VERSION")},"features":["telemetry","commands","cancellation"],"capability_namespaces":["ollama"],"config_schema_version":1}),
        ),
        envelope(
            MessageType::NodeSnapshot,
            "ollama_snapshot_01",
            json!({"node_id":"ollama.local","server_version":version["version"],"installed_models":installed,"running_models":running}),
        ),
    ] {
        print_record(&record)?;
    }
    Ok(())
}

fn run() -> Result<(), String> {
    let arguments = env::args().skip(1).collect::<Vec<_>>();
    let default_url = "http://127.0.0.1:11434";
    match arguments.as_slice() {
        [] => run_snapshot(default_url),
        [command, base_url] if command == "snapshot" => run_snapshot(base_url),
        [command, model, prompt, maximum_tokens] if command == "generate" => {
            let maximum_tokens = maximum_tokens.parse().map_err(|_| "maximum_tokens must be an integer")?;
            print_record(&envelope(
                MessageType::CommandUpdate,
                "ollama_command_01",
                generate(default_url, model, prompt, maximum_tokens, None)?,
            ))
        }
        [command, model, prompt, maximum_tokens, cancel_after_ms] if command == "generate" => {
            let maximum_tokens = maximum_tokens.parse().map_err(|_| "maximum_tokens must be an integer")?;
            let cancel_after_ms = cancel_after_ms.parse().map_err(|_| "cancel_after_ms must be an integer")?;
            print_record(&envelope(
                MessageType::CommandUpdate,
                "ollama_command_01",
                generate(default_url, model, prompt, maximum_tokens, Some(cancel_after_ms))?,
            ))
        }
        _ => Err("usage: orclink-adapter-ollama [snapshot URL | generate MODEL PROMPT MAXIMUM_TOKENS [CANCEL_AFTER_MS]]".into()),
    }
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
    fn sorts_models_and_bounds_generation() {
        assert_eq!(
            super::model_names(&json!({"models":[{"name":"z"},{"name":"a"}]})),
            ["a", "z"]
        );
        assert!(super::validate_generation("model", "prompt", 128).is_ok());
        assert!(super::validate_generation("model", "prompt", 129).is_err());
        assert!(super::validate_generation("model", "", 1).is_err());
    }
}
