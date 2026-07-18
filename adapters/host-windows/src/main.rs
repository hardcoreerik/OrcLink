use orclink_protocol::{AdapterEnvelope, MessageType, ProtocolVersion};
use serde_json::{Value, json};
use std::{process::Command, process::ExitCode};

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

fn run() -> Result<(), String> {
    let records = [
        envelope(
            MessageType::Hello,
            "host_hello_01",
            json!({"adapter":{"id":"host.windows","version":env!("CARGO_PKG_VERSION")},"features":["telemetry"],"capability_namespaces":["host"],"config_schema_version":1}),
        ),
        envelope(
            MessageType::NodeSnapshot,
            "host_snapshot_01",
            json!({"node_id":"host.windows.local","host":snapshot()?}),
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
}
