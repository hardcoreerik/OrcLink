use hmac::{Hmac, Mac};
use orclink_protocol::AdapterEnvelope;
use serialport::ClearBuffer;
use sha2::Sha256;
use std::{
    env,
    fs::{self, OpenOptions},
    io::{self, BufRead, BufReader, ErrorKind, Write},
    path::{Path, PathBuf},
    process::ExitCode,
    thread,
    time::{Duration, Instant},
};

type HmacSha256 = Hmac<Sha256>;

struct TimeoutTolerantWriter<W>(W);

impl<W: Write> Write for TimeoutTolerantWriter<W> {
    fn write(&mut self, buffer: &[u8]) -> io::Result<usize> {
        match self.0.write(buffer) {
            Ok(written) => Ok(written),
            Err(error) if is_timeout(&error) => Ok(buffer.len()),
            Err(error) => Err(error),
        }
    }

    fn flush(&mut self) -> io::Result<()> {
        match self.0.flush() {
            Ok(()) => Ok(()),
            Err(error) if is_timeout(&error) => Ok(()),
            Err(error) => Err(error),
        }
    }
}

fn timeout_message(message: &str) -> bool {
    let message = message.to_ascii_lowercase();
    message.contains("timed out")
        || message.contains("semaphore timeout")
        || message.contains("does not recognize the command")
}

fn is_timeout(error: &io::Error) -> bool {
    matches!(error.kind(), ErrorKind::TimedOut | ErrorKind::WouldBlock)
        || error.raw_os_error() == Some(121)
        || error.raw_os_error() == Some(22)
        || timeout_message(&error.to_string())
}

fn flush_serial(writer: &mut impl Write) -> Result<(), String> {
    match writer.flush() {
        Ok(()) => Ok(()),
        Err(error) if is_timeout(&error) => Ok(()),
        Err(error) => Err(error.to_string()),
    }
}

fn encode_hex(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut output = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        output.push(HEX[(byte >> 4) as usize] as char);
        output.push(HEX[(byte & 0xf) as usize] as char);
    }
    output
}

fn decode_hex(value: &str) -> Result<Vec<u8>, String> {
    if !value.len().is_multiple_of(2) {
        return Err("hex value has odd length".into());
    }
    value
        .as_bytes()
        .chunks_exact(2)
        .map(|pair| {
            let digit = |value: u8| match value {
                b'0'..=b'9' => Ok(value - b'0'),
                b'a'..=b'f' => Ok(value - b'a' + 10),
                b'A'..=b'F' => Ok(value - b'A' + 10),
                _ => Err("invalid hex digit".to_string()),
            };
            Ok((digit(pair[0])? << 4) | digit(pair[1])?)
        })
        .collect()
}

fn load_or_create_key(path: &Path) -> Result<[u8; 32], String> {
    match fs::read_to_string(path) {
        Ok(value) => return decode_key(value.trim()),
        Err(error) if error.kind() == ErrorKind::NotFound => {}
        Err(error) => return Err(format!("read {}: {error}", path.display())),
    }
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|error| error.to_string())?;
    }
    let mut key = [0; 32];
    getrandom::fill(&mut key).map_err(|error| error.to_string())?;
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|error| format!("create {}: {error}", path.display()))?;
    file.write_all(encode_hex(&key).as_bytes())
        .map_err(|error| error.to_string())?;
    Ok(key)
}

fn replace_key(path: &Path, key: &[u8; 32]) -> Result<(), String> {
    let temporary = path.with_extension("key.new");
    let mut file = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(&temporary)
        .map_err(|error| format!("create {}: {error}", temporary.display()))?;
    file.write_all(encode_hex(key).as_bytes())
        .and_then(|()| file.sync_all())
        .map_err(|error| format!("write {}: {error}", temporary.display()))?;
    fs::rename(&temporary, path).map_err(|error| format!("replace {}: {error}", path.display()))
}

fn hmac_hex(key: &[u8; 32], value: &[u8]) -> Result<String, String> {
    let mut signature = HmacSha256::new_from_slice(key).map_err(|error| error.to_string())?;
    signature.update(value);
    Ok(encode_hex(&signature.finalize().into_bytes()))
}

fn decode_key(value: &str) -> Result<[u8; 32], String> {
    decode_hex(value)?
        .try_into()
        .map_err(|_| "credential must be exactly 32 bytes".into())
}

fn read_control(reader: &mut impl BufRead, expected: &str) -> Result<String, String> {
    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        let mut line = String::new();
        match reader.read_line(&mut line) {
            Ok(0) => return Err("device disconnected during authentication".into()),
            Ok(_) if line.trim_start().starts_with(expected) => return Ok(line.trim().into()),
            Ok(_) if !line.trim().is_empty() => eprint!("{line}"),
            Ok(_) => {}
            Err(error) if is_timeout(&error) && Instant::now() < deadline => {}
            Err(error) => return Err(error.to_string()),
        }
        if Instant::now() >= deadline {
            return Err(format!("timed out waiting for {expected}"));
        }
    }
}

fn authenticate(
    reader: &mut impl BufRead,
    writer: &mut impl Write,
    key: &[u8; 32],
) -> Result<(), String> {
    writeln!(writer, "PAIR {}", encode_hex(key)).map_err(|error| error.to_string())?;
    flush_serial(writer)?;
    read_control(reader, "PAIR_OK")?;

    let mut nonce = [0; 16];
    getrandom::fill(&mut nonce).map_err(|error| error.to_string())?;
    let mut proof = HmacSha256::new_from_slice(key).map_err(|error| error.to_string())?;
    proof.update(b"host");
    proof.update(&nonce);
    writeln!(
        writer,
        "AUTH {} {}",
        encode_hex(&nonce),
        encode_hex(&proof.finalize().into_bytes())
    )
    .map_err(|error| error.to_string())?;
    flush_serial(writer)?;
    let response = read_control(reader, "AUTH_OK ")?;
    let signature = decode_hex(response.trim_start_matches("AUTH_OK "))?;
    let mut verifier = HmacSha256::new_from_slice(key).map_err(|error| error.to_string())?;
    verifier.update(b"device");
    verifier.update(&nonce);
    verifier
        .verify_slice(&signature)
        .map_err(|_| "device authentication failed".to_string())
}

fn write_envelope(line: &str, mut output: impl Write) -> Result<(AdapterEnvelope, usize), String> {
    let envelope: AdapterEnvelope =
        serde_json::from_str(line).map_err(|error| format!("invalid envelope: {error}"))?;
    if envelope.protocol_version.major != 1 {
        return Err(format!(
            "unsupported protocol major {}",
            envelope.protocol_version.major
        ));
    }
    serde_json::to_writer(&mut output, &envelope).map_err(|error| error.to_string())?;
    writeln!(output).map_err(|error| error.to_string())?;
    output.flush().map_err(|error| error.to_string())?;
    Ok((envelope, 1))
}

fn forward(reader: impl BufRead, mut output: impl Write) -> Result<usize, String> {
    let mut count = 0;
    for (index, line) in reader.lines().enumerate() {
        let line = line.map_err(|error| format!("line {}: {error}", index + 1))?;
        if !line.starts_with('{') {
            continue;
        }
        count += write_envelope(&line, &mut output)
            .map_err(|error| format!("line {}: {error}", index + 1))?
            .1;
    }
    Ok(count)
}

fn run_device(
    port_name: &str,
    credential: &Path,
    once: bool,
    prepare_flash: Option<&str>,
    install_status: Option<(u32, u16)>,
    rotate_key: bool,
    test_pressure: bool,
) -> Result<(), String> {
    let open_deadline = Instant::now() + Duration::from_secs(8);
    let port = loop {
        match serialport::new(port_name, 115_200)
            .timeout(Duration::from_millis(250))
            .dtr_on_open(false)
            .open()
        {
            Ok(port) => break port,
            Err(error) if timeout_message(&error.to_string()) && Instant::now() < open_deadline => {
                thread::sleep(Duration::from_millis(250));
            }
            Err(error) => return Err(format!("open {port_name}: {error}")),
        }
    };
    port.clear(ClearBuffer::Input)
        .map_err(|error| format!("clear {port_name}: {error}"))?;
    let mut writer = TimeoutTolerantWriter(port.try_clone().map_err(|error| error.to_string())?);
    let mut reader = BufReader::new(port);
    let mut key = load_or_create_key(credential)?;
    authenticate(&mut reader, &mut writer, &key)?;

    if let Some(digest) = prepare_flash {
        if decode_hex(digest)?.len() != 32 {
            return Err("flash digest must be exactly 32 bytes".into());
        }
        writeln!(writer, "PREPARE_FLASH {digest}").map_err(|error| error.to_string())?;
        flush_serial(&mut writer)?;
        let response = read_control(&mut reader, "FLASH_READY ")?;
        if !response
            .trim_start_matches("FLASH_READY ")
            .eq_ignore_ascii_case(digest)
        {
            return Err("device confirmed a different flash digest".into());
        }
    }

    if let Some((revision, max_runs)) = install_status {
        let value = format!("workflow|{revision}|{max_runs}");
        writeln!(
            writer,
            "INSTALL_STATUS {revision} {max_runs} {}",
            hmac_hex(&key, value.as_bytes())?
        )
        .map_err(|error| error.to_string())?;
        flush_serial(&mut writer)?;
        read_control(&mut reader, &format!("WORKFLOW_OK {revision}"))?;
    }

    if test_pressure {
        writeln!(writer, "TEST_PRESSURE").map_err(|error| error.to_string())?;
        flush_serial(&mut writer)?;
        let response = read_control(&mut reader, "PRESSURE_OK ")?;
        let dropped = response
            .trim_start_matches("PRESSURE_OK ")
            .parse::<u32>()
            .map_err(|_| "device returned an invalid pressure count".to_string())?;
        if dropped == 0 {
            return Err("journal pressure test did not drop any records".into());
        }
    }

    if rotate_key {
        let mut replacement = [0; 32];
        getrandom::fill(&mut replacement).map_err(|error| error.to_string())?;
        let replacement_hex = encode_hex(&replacement);
        let value = format!("rotate|{replacement_hex}");
        writeln!(
            writer,
            "ROTATE_KEY {replacement_hex} {}",
            hmac_hex(&key, value.as_bytes())?
        )
        .map_err(|error| error.to_string())?;
        flush_serial(&mut writer)?;
        read_control(&mut reader, "KEY_ROTATED")?;
        replace_key(credential, &replacement)?;
        key = replacement;
        authenticate(&mut reader, &mut writer, &key)?;
    }

    writeln!(writer, "PING").map_err(|error| error.to_string())?;
    flush_serial(&mut writer)?;
    let mut last_ping = Instant::now();
    let mut snapshot_seen = false;
    let once_deadline = Instant::now() + Duration::from_secs(15);
    loop {
        let mut line = String::new();
        match reader.read_line(&mut line) {
            Ok(0) => return Err("device disconnected".into()),
            Ok(_) if line.starts_with('{') => {
                let (envelope, _) = write_envelope(line.trim(), io::stdout().lock())?;
                snapshot_seen |= envelope.message_id.starts_with("m5tab5_snapshot_");
                if let Some(sequence) = envelope
                    .payload
                    .get("journal_sequence")
                    .and_then(|value| value.as_u64())
                {
                    writeln!(writer, "ACK {sequence}").map_err(|error| error.to_string())?;
                    flush_serial(&mut writer)?;
                }
                if envelope
                    .payload
                    .get("journal")
                    .and_then(|journal| journal.get("dropped"))
                    .and_then(|value| value.as_u64())
                    .is_some_and(|value| value > 0)
                {
                    writeln!(writer, "PRESSURE_ACK").map_err(|error| error.to_string())?;
                    flush_serial(&mut writer)?;
                }
                if once
                    && snapshot_seen
                    && envelope
                        .payload
                        .get("journal_pending")
                        .and_then(|value| value.as_u64())
                        == Some(0)
                {
                    return Ok(());
                }
            }
            Ok(_) => {}
            Err(error) if is_timeout(&error) => {}
            Err(error) => return Err(error.to_string()),
        }
        if last_ping.elapsed() >= Duration::from_secs(1) {
            writeln!(writer, "PING").map_err(|error| error.to_string())?;
            flush_serial(&mut writer)?;
            last_ping = Instant::now();
        }
        if once && Instant::now() >= once_deadline {
            return Err("timed out waiting for a settled device snapshot".into());
        }
    }
}

fn run() -> Result<(), String> {
    let argument = env::args().nth(1).unwrap_or_else(|| "COM17".into());
    if argument == "--stdin" {
        forward(io::stdin().lock(), io::stdout().lock())?;
        return Ok(());
    }
    let credential = env::args()
        .nth(2)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(".orclink").join(format!("m5tab5-{argument}.key")));
    let options = env::args().skip(3).collect::<Vec<_>>();
    let once = options.iter().any(|value| value == "--once");
    let prepare_flash = options
        .iter()
        .position(|value| value == "--prepare-flash")
        .and_then(|index| options.get(index + 1))
        .map(String::as_str);
    let install_status = options
        .iter()
        .position(|value| value == "--install-status")
        .map(|index| {
            let revision = options
                .get(index + 1)
                .ok_or("--install-status requires REVISION MAX_RUNS")?
                .parse::<u32>()
                .map_err(|_| "workflow revision must be an integer")?;
            let max_runs = options
                .get(index + 2)
                .ok_or("--install-status requires REVISION MAX_RUNS")?
                .parse::<u16>()
                .map_err(|_| "workflow max runs must be 1..65535")?;
            if revision == 0 || max_runs == 0 {
                return Err("workflow revision and max runs must be nonzero");
            }
            Ok((revision, max_runs))
        })
        .transpose()?;
    let rotate_key = options.iter().any(|value| value == "--rotate-key");
    let test_pressure = options.iter().any(|value| value == "--test-pressure");
    run_device(
        &argument,
        &credential,
        once,
        prepare_flash,
        install_status,
        rotate_key,
        test_pressure,
    )
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

    #[test]
    fn hex_and_hmac_are_strict() {
        let key = [7; 32];
        assert_eq!(super::decode_key(&super::encode_hex(&key)).unwrap(), key);
        assert!(super::decode_key("00").is_err());
        assert!(super::decode_hex("xy").is_err());
        assert!(super::is_timeout(&std::io::Error::other(
            "The semaphore timeout period has expired."
        )));
        assert!(super::timeout_message(
            "The semaphore timeout period has expired."
        ));
    }
}
