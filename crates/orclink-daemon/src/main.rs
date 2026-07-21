use orclink_daemon::{ApiResponse, MAX_REQUEST_BYTES, Store};
use std::{
    env,
    io::{Cursor, Read},
    path::{Path, PathBuf},
    process::{Command, ExitCode},
};
use tiny_http::{Header, Request, Response, Server, StatusCode};

fn collect(store: &mut Store, source: &str, executable: &Path) -> Result<usize, String> {
    let output = Command::new(executable)
        .output()
        .map_err(|error| format!("{}: {error}", executable.display()))?;
    if !output.status.success() {
        return Err(format!(
            "{}: {}",
            executable.display(),
            String::from_utf8_lossy(&output.stderr).trim()
        ));
    }
    store.ingest(source, Cursor::new(output.stdout))
}

fn collect_available(store: &mut Store, source: &str, executable: &Path) {
    if !executable.exists() {
        eprintln!("orclinkd adapter unavailable: {}", executable.display());
        return;
    }
    match collect(store, source, executable) {
        Ok(count) => eprintln!("orclinkd admitted {count} records from {source}"),
        Err(error) => eprintln!("orclinkd adapter degraded: {source}: {error}"),
    }
}

fn allowed_origin(origin: &str) -> bool {
    origin == "tauri://localhost"
        || origin == "https://tauri.localhost"
        || origin == "http://tauri.localhost"
        || origin == "http://127.0.0.1"
        || origin.starts_with("http://127.0.0.1:")
        || origin == "http://localhost"
        || origin.starts_with("http://localhost:")
}

fn header(name: &str, value: &str) -> Header {
    Header::from_bytes(name, value).expect("static HTTP header is valid")
}

fn respond(request: Request, result: ApiResponse, origin: Option<&str>) -> Result<(), String> {
    let mut response = Response::from_string(if result.status == 204 {
        String::new()
    } else {
        result.body.to_string()
    })
    .with_status_code(StatusCode(result.status))
    .with_header(header("Content-Type", "application/json; charset=utf-8"))
    .with_header(header("Cache-Control", "no-store"))
    .with_header(header("X-Content-Type-Options", "nosniff"));
    if let Some(origin) = origin {
        response.add_header(header("Access-Control-Allow-Origin", origin));
        response.add_header(header("Vary", "Origin"));
        response.add_header(header("Access-Control-Allow-Methods", "GET, POST, OPTIONS"));
        response.add_header(header(
            "Access-Control-Allow-Headers",
            "Content-Type, X-OrcLink-Client",
        ));
    }
    request.respond(response).map_err(|error| error.to_string())
}

fn serve(mut store: Store, address: &str, maximum_requests: Option<usize>) -> Result<(), String> {
    let server = Server::http(address).map_err(|error| error.to_string())?;
    eprintln!("orclinkd listening on http://{address}");
    for mut request in server
        .incoming_requests()
        .take(maximum_requests.unwrap_or(usize::MAX))
    {
        let origin = request
            .headers()
            .iter()
            .find(|header| header.field.equiv("Origin"))
            .map(|header| header.value.as_str().to_owned());
        if origin
            .as_deref()
            .is_some_and(|origin| !allowed_origin(origin))
        {
            respond(
                request,
                ApiResponse {
                    status: 403,
                    body: serde_json::json!({"error":{"code":"ORIGIN_DENIED","message":"browser origin is not allowed"}}),
                },
                None,
            )?;
            continue;
        }
        let method = request.method().as_str().to_owned();
        let target = request.url().to_owned();
        let mut body = Vec::new();
        request
            .as_reader()
            .take((MAX_REQUEST_BYTES + 1) as u64)
            .read_to_end(&mut body)
            .map_err(|error| error.to_string())?;
        let result = if body.len() > MAX_REQUEST_BYTES {
            ApiResponse {
                status: 413,
                body: serde_json::json!({"error":{"code":"REQUEST_TOO_LARGE","message":"request exceeds 64 KiB"}}),
            }
        } else {
            store.handle(&method, &target, &body)
        };
        respond(request, result, origin.as_deref())?;
    }
    Ok(())
}

fn run() -> Result<(), String> {
    let executable = env::current_exe().map_err(|error| error.to_string())?;
    let directory = executable
        .parent()
        .ok_or("orclinkd has no executable directory")?;
    let suffix = env::consts::EXE_SUFFIX;
    let host = directory.join(format!("orclink-adapter-host-windows{suffix}"));
    let ollama = directory.join(format!("orclink-adapter-ollama{suffix}"));
    let rtl_sdr = directory.join(format!("orclink-adapter-rtl-sdr{suffix}"));
    let database = env::args()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| "orclink.sqlite".into());
    let address = env::args()
        .nth(2)
        .unwrap_or_else(|| "127.0.0.1:4017".into());
    let maximum_requests = env::args()
        .nth(3)
        .map(|value| {
            value
                .parse()
                .map_err(|_| "maximum requests must be an integer")
        })
        .transpose()?;

    let mut store = Store::open(database)?;
    collect_available(&mut store, "host.windows", &host);
    collect_available(&mut store, "ollama.local", &ollama);
    collect_available(&mut store, "rtl_sdr.windows", &rtl_sdr);
    serve(store, &address, maximum_requests)
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
    fn restricts_browser_origins_to_local_control_surfaces() {
        assert!(super::allowed_origin("http://127.0.0.1:4173"));
        assert!(super::allowed_origin("http://localhost:3000"));
        assert!(super::allowed_origin("tauri://localhost"));
        assert!(!super::allowed_origin("https://evil.example"));
        assert!(!super::allowed_origin("http://127.0.0.1.evil.example"));
    }
}
