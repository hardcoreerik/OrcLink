use orclink_protocol::load_and_admit;
use std::{env, fs::File, io::BufReader, process::ExitCode};

fn main() -> ExitCode {
    let path = env::args()
        .nth(1)
        .unwrap_or_else(|| "schemas/examples/simulator-session.jsonl".into());
    match File::open(&path)
        .map_err(|error| format!("{path}: {error}"))
        .and_then(|file| load_and_admit(BufReader::new(file)))
    {
        Ok(summary) => {
            println!("{summary}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
