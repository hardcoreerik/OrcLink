use orclink_protocol::{AdapterEnvelope, MessageType, ProtocolVersion};
use serde_json::{Value, json};
use std::{
    env,
    process::{Command, ExitCode},
};

const SCRIPT: &str = r#"
$params = @{ Uri=$env:ORCLINK_BRIGHTSIGN_INFO_URL; Method='Get'; TimeoutSec=5; ErrorAction='Stop' }
if ($env:ORCLINK_BRIGHTSIGN_USERNAME) {
  $secure = ConvertTo-SecureString $env:ORCLINK_BRIGHTSIGN_PASSWORD -AsPlainText -Force
  $params.Authentication = 'Digest'
  $params.Credential = [pscredential]::new($env:ORCLINK_BRIGHTSIGN_USERNAME, $secure)
}
Invoke-RestMethod @params | ConvertTo-Json -Compress -Depth 12
"#;

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

fn info_url(endpoint: &str) -> Result<String, String> {
    let target = endpoint
        .strip_prefix("http://")
        .or_else(|| endpoint.strip_prefix("https://"));
    if endpoint.len() > 2048 || endpoint.chars().any(char::is_whitespace) {
        return Err("endpoint must be an HTTP(S) URL without credentials".into());
    }
    let Some(target) = target.map(|value| value.trim_end_matches('/')) else {
        return Err("endpoint must be an HTTP(S) URL without credentials".into());
    };
    if target.is_empty() || target.contains(['/', '@', '?', '#']) {
        return Err("endpoint must be an HTTP(S) URL without credentials".into());
    }
    Ok(format!("{}/api/v1/info/", endpoint.trim_end_matches('/')))
}

fn credentials() -> Result<(Option<String>, Option<String>), String> {
    let username = env::var("ORCLINK_BRIGHTSIGN_USERNAME")
        .ok()
        .filter(|v| !v.is_empty());
    let password = env::var("ORCLINK_BRIGHTSIGN_PASSWORD")
        .ok()
        .filter(|v| !v.is_empty());
    if username.is_some() != password.is_some() {
        return Err("BrightSign username and password must be supplied together".into());
    }
    Ok((username, password))
}

fn get_info(endpoint: &str) -> Result<Value, String> {
    let (username, password) = credentials()?;
    let mut command = Command::new("pwsh.exe");
    command
        .args([
            "-NoLogo",
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            SCRIPT,
        ])
        .env("ORCLINK_BRIGHTSIGN_INFO_URL", info_url(endpoint)?);
    if let (Some(username), Some(password)) = (username, password) {
        command
            .env("ORCLINK_BRIGHTSIGN_USERNAME", username)
            .env("ORCLINK_BRIGHTSIGN_PASSWORD", password);
    } else {
        command
            .env_remove("ORCLINK_BRIGHTSIGN_USERNAME")
            .env_remove("ORCLINK_BRIGHTSIGN_PASSWORD");
    }
    let output = command.output().map_err(|error| error.to_string())?;
    if !output.status.success() {
        return Err(String::from_utf8_lossy(&output.stderr).trim().into());
    }
    serde_json::from_slice(&output.stdout).map_err(|error| error.to_string())
}

fn required_string<'a>(info: &'a Value, field: &str) -> Result<&'a str, String> {
    info.get(field)
        .and_then(Value::as_str)
        .ok_or_else(|| format!("BrightSign response is missing {field}"))
}

fn safe_snapshot(response: &Value, endpoint: &str) -> Result<Value, String> {
    let info = response
        .pointer("/data/result")
        .or_else(|| response.get("result"))
        .unwrap_or(response);
    Ok(json!({
        "node_id":format!("brightsign.{}", required_string(info, "serial")?),
        "connection_state":"online",
        "endpoint":endpoint,
        "serial":required_string(info, "serial")?,
        "model":required_string(info, "model")?,
        "firmware_version":required_string(info, "FWVersion")?,
        "uptime_seconds":info.get("upTimeSeconds").and_then(Value::as_u64),
        "connection_type":info.get("connectionType").and_then(Value::as_str),
        "capabilities":["brightsign.status.read"]
    }))
}

fn run() -> Result<(), String> {
    let mut args = env::args().skip(1);
    let endpoint = args
        .next()
        .ok_or("usage: orclink-adapter-brightsign <http(s)://player>")?;
    if args.next().is_some() {
        return Err("usage: orclink-adapter-brightsign <http(s)://player>".into());
    }
    let records = [
        envelope(
            MessageType::Hello,
            "brightsign_hello_01",
            json!({
                "adapter":{"id":"brightsign.readonly","version":env!("CARGO_PKG_VERSION")},
                "features":["telemetry"],
                "capability_namespaces":["brightsign"],
                "config_schema_version":1
            }),
        ),
        envelope(
            MessageType::NodeSnapshot,
            "brightsign_snapshot_01",
            safe_snapshot(&get_info(&endpoint)?, &endpoint)?,
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
    use std::{
        io::{Read, Write},
        net::TcpListener,
        thread,
    };

    #[test]
    fn reads_and_sanitizes_local_info() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut request = [0; 1024];
            let read = stream.read(&mut request).unwrap();
            assert!(String::from_utf8_lossy(&request[..read]).starts_with("GET /api/v1/info/ "));
            let body = r#"{"data":{"result":{"serial":"ABC123","model":"XD5","FWVersion":"9.1","upTimeSeconds":42,"networking":{"secret":"omitted"}}}}"#;
            write!(stream, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}", body.len()).unwrap();
        });
        let endpoint = format!("http://{address}");
        let snapshot =
            super::safe_snapshot(&super::get_info(&endpoint).unwrap(), &endpoint).unwrap();
        server.join().unwrap();
        assert_eq!(snapshot["node_id"], "brightsign.ABC123");
        assert_eq!(snapshot["uptime_seconds"], 42);
        assert!(snapshot.get("networking").is_none());
        assert_eq!(
            snapshot["capabilities"],
            serde_json::json!(["brightsign.status.read"])
        );
        assert!(super::info_url("http://user:password@player").is_err());
    }
}
