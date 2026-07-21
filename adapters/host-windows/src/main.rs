use orclink_protocol::{AdapterEnvelope, MessageType, ProtocolVersion};
use serde_json::{Value, json};
use std::{
    collections::HashSet,
    env,
    process::{Command, ExitCode},
    time::{SystemTime, UNIX_EPOCH},
};

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

fn snapshot() -> Result<Value, String> {
    let script = r#"$os=Get-CimInstance Win32_OperatingSystem;$groups=Get-Service|Group-Object Status|Sort-Object Name;[ordered]@{hostname=[Environment]::MachineName;os=$os.Caption;version=$os.Version;total_memory_bytes=[uint64]$os.TotalVisibleMemorySize*1024;free_memory_bytes=[uint64]$os.FreePhysicalMemory*1024;process_count=@(Get-Process).Count;services=@($groups|ForEach-Object{[ordered]@{status=$_.Name;count=$_.Count}})}|ConvertTo-Json -Compress -Depth 3"#;
    let output = Command::new("powershell.exe")
        .args(["-NoProfile", "-NonInteractive", "-Command", script])
        .output()
        .map_err(|error| error.to_string())?;
    if !output.status.success() {
        return Err(String::from_utf8_lossy(&output.stderr).trim().into());
    }
    serde_json::from_slice(&output.stdout).map_err(|error| error.to_string())
}

fn service_command(action: &str, name: &str, allowed: &HashSet<String>) -> Value {
    if name.is_empty()
        || name.len() > 128
        || !name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"._-".contains(&byte))
    {
        return json!({"state":"Rejected","outcome":"not_executed","error":"invalid service name"});
    }
    if action != "status" && !allowed.contains(name) {
        return json!({"state":"Rejected","outcome":"not_executed","error":"service is not allowlisted"});
    }

    let script = match action {
        "status" => {
            "$s=Get-Service -Name $env:ORCLINK_SERVICE_NAME -ErrorAction Stop;[ordered]@{name=$s.Name;display_name=$s.DisplayName;status=[string]$s.Status}|ConvertTo-Json -Compress"
        }
        "start" => {
            "Start-Service -Name $env:ORCLINK_SERVICE_NAME -ErrorAction Stop;$s=Get-Service -Name $env:ORCLINK_SERVICE_NAME;$s.WaitForStatus('Running','00:00:10');[ordered]@{name=$s.Name;status='Running'}|ConvertTo-Json -Compress"
        }
        "stop" => {
            "Stop-Service -Name $env:ORCLINK_SERVICE_NAME -ErrorAction Stop;$s=Get-Service -Name $env:ORCLINK_SERVICE_NAME;$s.WaitForStatus('Stopped','00:00:10');[ordered]@{name=$s.Name;status='Stopped'}|ConvertTo-Json -Compress"
        }
        "restart" => {
            "Restart-Service -Name $env:ORCLINK_SERVICE_NAME -ErrorAction Stop;$s=Get-Service -Name $env:ORCLINK_SERVICE_NAME;$s.WaitForStatus('Running','00:00:10');[ordered]@{name=$s.Name;status='Running'}|ConvertTo-Json -Compress"
        }
        _ => {
            return json!({"state":"Rejected","outcome":"not_executed","error":"unknown service action"});
        }
    };
    let output = Command::new("powershell.exe")
        .args(["-NoProfile", "-NonInteractive", "-Command", script])
        .env("ORCLINK_SERVICE_NAME", name)
        .output();
    match output {
        Ok(output) if output.status.success() => {
            match serde_json::from_slice::<Value>(&output.stdout) {
                Ok(service) => {
                    json!({"state":"Succeeded","outcome":"executed_verified","service":service})
                }
                Err(error) => {
                    json!({"state":"Failed","outcome":"executed_unverified","error":error.to_string()})
                }
            }
        }
        Ok(output) => {
            json!({"state":"Failed","outcome":"not_executed","error":String::from_utf8_lossy(&output.stderr).trim()})
        }
        Err(error) => json!({"state":"Failed","outcome":"not_executed","error":error.to_string()}),
    }
}

fn print_record(record: &AdapterEnvelope) -> Result<(), String> {
    println!(
        "{}",
        serde_json::to_string(record).map_err(|error| error.to_string())?
    );
    Ok(())
}

fn run_snapshot() -> Result<(), String> {
    let run_id = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|error| error.to_string())?
        .as_millis();
    for record in [
        envelope(
            MessageType::Hello,
            &format!("host_hello_{run_id}"),
            json!({"adapter":{"id":"host.windows","version":env!("CARGO_PKG_VERSION")},"features":["telemetry","commands"],"capability_namespaces":["host"],"config_schema_version":1}),
        ),
        envelope(
            MessageType::NodeSnapshot,
            &format!("host_snapshot_{run_id}"),
            json!({
                "node_id":"host.windows.local",
                "display_name":"Windows Host",
                "connection_state":"online",
                "host":snapshot()?,
                "capabilities":[
                    "host.inventory.read",
                    "host.service.status",
                    "host.service.start",
                    "host.service.stop",
                    "host.service.restart"
                ]
            }),
        ),
    ] {
        print_record(&record)?;
    }
    Ok(())
}

fn run() -> Result<(), String> {
    let arguments = env::args().skip(1).collect::<Vec<_>>();
    if arguments.is_empty() {
        return run_snapshot();
    }
    let [command, name] = arguments.as_slice() else {
        return Err("usage: orclink-adapter-host-windows [service-status|service-start|service-stop|service-restart NAME]".into());
    };
    let action = command.strip_prefix("service-").unwrap_or("");
    let allowed = env::var("ORCLINK_HOST_SERVICE_ALLOWLIST")
        .unwrap_or_default()
        .split(',')
        .filter(|name| !name.is_empty())
        .map(str::to_owned)
        .collect();
    print_record(&envelope(
        MessageType::CommandUpdate,
        "host_command_01",
        service_command(action, name, &allowed),
    ))
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
    #[test]
    fn reads_live_windows_snapshot() {
        let snapshot = super::snapshot().unwrap();
        assert!(snapshot["os"].as_str().unwrap().contains("Windows"));
        assert!(snapshot["process_count"].as_u64().unwrap() > 0);
        assert!(snapshot["services"].as_array().unwrap().len() >= 2);
    }

    #[test]
    fn denies_service_mutation_without_allowlist() {
        let result = super::service_command("restart", "Spooler", &Default::default());
        assert_eq!(result["state"], "Rejected");
        assert_eq!(result["outcome"], "not_executed");
    }
}
