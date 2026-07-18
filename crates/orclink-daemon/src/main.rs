use orclink_daemon::Store;
use std::{
    env,
    io::Cursor,
    path::{Path, PathBuf},
    process::{Command, ExitCode},
};
use tiny_http::{Header, Response, Server, StatusCode};

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

fn serve(store: Store, address: &str, maximum_requests: Option<usize>) -> Result<(), String> {
    let server = Server::http(address).map_err(|error| error.to_string())?;
    eprintln!("orclinkd listening on http://{address}");
    for request in server
        .incoming_requests()
        .take(maximum_requests.unwrap_or(usize::MAX))
    {
        let result = store.response(request.url());
        let (body, status) = match result {
            Ok(value) => (value.to_string(), StatusCode(200)),
            Err(error) if error == "not found" => {
                ("{\"error\":\"not found\"}".into(), StatusCode(404))
            }
            Err(error) => (
                serde_json::json!({"error":error}).to_string(),
                StatusCode(500),
            ),
        };
        let response = Response::from_string(body)
            .with_status_code(status)
            .with_header(Header::from_bytes("Content-Type", "application/json").unwrap())
            .with_header(Header::from_bytes("Access-Control-Allow-Origin", "*").unwrap());
        request
            .respond(response)
            .map_err(|error| error.to_string())?;
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
    collect(&mut store, "host.windows", &host)?;
    collect(&mut store, "ollama.local", &ollama)?;
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
