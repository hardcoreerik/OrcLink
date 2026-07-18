use orclink_protocol::AdapterEnvelope;
use std::{
    env,
    io::{self, BufRead, BufReader, Read},
    process::ExitCode,
    time::Duration,
};

fn forward(reader: impl BufRead, mut output: impl io::Write) -> Result<usize, String> {
    let mut count = 0;
    for (index, line) in reader.lines().enumerate() {
        let line = line.map_err(|error| format!("line {}: {error}", index + 1))?;
        if !line.starts_with('{') {
            continue;
        }
        let envelope: AdapterEnvelope = serde_json::from_str(&line)
            .map_err(|error| format!("line {}: invalid envelope: {error}", index + 1))?;
        if envelope.protocol_version.major != 1 {
            return Err(format!(
                "line {}: unsupported protocol major {}",
                index + 1,
                envelope.protocol_version.major
            ));
        }
        serde_json::to_writer(&mut output, &envelope).map_err(|error| error.to_string())?;
        writeln!(output).map_err(|error| error.to_string())?;
        count += 1;
    }
    Ok(count)
}

fn run() -> Result<(), String> {
    let argument = env::args().nth(1).unwrap_or_else(|| "COM17".into());
    if argument == "--stdin" {
        forward(io::stdin().lock(), io::stdout().lock())?;
        return Ok(());
    }

    let port = serialport::new(&argument, 115_200)
        .timeout(Duration::from_secs(5))
        .open()
        .map_err(|error| format!("open {argument}: {error}"))?;
    let reader: Box<dyn Read> = port;
    forward(BufReader::new(reader), io::stdout().lock())?;
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
    use std::io::Cursor;

    #[test]
    fn ignores_boot_logs_and_forwards_v1_envelopes() {
        let input = concat!(
            "ESP-ROM:esp32p4\n",
            "{\"type\":\"heartbeat\",\"message_id\":\"h1\",\"protocol_version\":{\"major\":1,\"minor\":0},\"payload\":{\"sequence\":1}}\n"
        );
        let mut output = Vec::new();
        assert_eq!(super::forward(Cursor::new(input), &mut output).unwrap(), 1);
        assert!(
            String::from_utf8(output)
                .unwrap()
                .contains("\"message_id\":\"h1\"")
        );
    }

    #[test]
    fn rejects_unknown_protocol_major() {
        let input = "{\"type\":\"heartbeat\",\"message_id\":\"h1\",\"protocol_version\":{\"major\":2,\"minor\":0},\"payload\":{}}\n";
        assert!(super::forward(Cursor::new(input), Vec::new()).is_err());
    }
}
