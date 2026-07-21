use orclink_protocol::{AdapterEnvelope, MessageType, ProtocolVersion};
use serde::{Deserialize, Serialize};
use serde_json::json;
use sha2::{Digest, Sha256};
use std::{
    env,
    ffi::OsStr,
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    os::windows::ffi::OsStrExt,
    path::{Path, PathBuf},
    process::{Child, Command, ExitCode, Stdio},
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};
use windows_sys::Win32::{
    Foundation::{CloseHandle, HANDLE, WAIT_ABANDONED, WAIT_OBJECT_0},
    Storage::FileSystem::GetDiskFreeSpaceExW,
    System::Threading::{CreateMutexW, ReleaseMutex, WaitForSingleObject},
};

const DEVICE_MUTEX: &str = "Local\\OrcLink.RtlSdr.Device0";
const MAX_CAPTURE_BYTES: u64 = 1 << 30;

fn envelope(
    message_type: MessageType,
    message_id: &str,
    payload: serde_json::Value,
) -> AdapterEnvelope {
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

fn snapshot() -> Result<(), String> {
    let run_id = now_ms()?;
    for record in [
        envelope(
            MessageType::Hello,
            &format!("rtl_sdr_hello_{run_id}"),
            json!({
                "adapter":{"id":"rtl_sdr.windows","version":env!("CARGO_PKG_VERSION")},
                "features":["discovery","bounded_capture","provenance","decode"],
                "capability_namespaces":["rtl_sdr"],
                "config_schema_version":1
            }),
        ),
        envelope(
            MessageType::NodeSnapshot,
            &format!("rtl_sdr_snapshot_{run_id}"),
            json!({
                "node_id":"rtl-sdr.device0",
                "display_name":"RTL-SDR Device 0",
                "connection_state":"offline",
                "device_state":"not_probed",
                "capabilities":["rtl_sdr.capture","rtl_sdr.decode"]
            }),
        ),
    ] {
        println!(
            "{}",
            serde_json::to_string(&record).map_err(|error| error.to_string())?
        );
    }
    Ok(())
}

#[derive(Debug, Deserialize, Serialize)]
struct CaptureProvenance {
    artifact: String,
    sha256: String,
    bytes: u64,
    format: String,
    frequency_hz: u64,
    sample_rate_hz: u64,
    requested_start_unix_ms: Option<u64>,
    actual_start_unix_ms: u64,
    completed_unix_ms: u64,
    device_index: u8,
    observed_device: String,
    recorder: String,
    disposition: String,
}

#[derive(Serialize)]
struct DecodeProvenance {
    source_artifact: String,
    source_sha256: String,
    pipeline: String,
    output_directory: String,
    decoder: String,
    started_unix_ms: u64,
    completed_unix_ms: u64,
    disposition: String,
}

struct DeviceReservation(HANDLE);

struct PartialArtifact(PathBuf);

impl Drop for PartialArtifact {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.0);
    }
}

impl DeviceReservation {
    fn acquire() -> Result<Self, String> {
        let name = wide(OsStr::new(DEVICE_MUTEX));
        let handle = unsafe { CreateMutexW(std::ptr::null(), 0, name.as_ptr()) };
        if handle.is_null() {
            return Err(format!(
                "CreateMutexW failed: {}",
                std::io::Error::last_os_error()
            ));
        }
        let result = unsafe { WaitForSingleObject(handle, 0) };
        if result != WAIT_OBJECT_0 && result != WAIT_ABANDONED {
            unsafe { CloseHandle(handle) };
            return Err("RTL-SDR device 0 is reserved by another OrcLink capture".into());
        }
        Ok(Self(handle))
    }
}

impl Drop for DeviceReservation {
    fn drop(&mut self) {
        unsafe {
            ReleaseMutex(self.0);
            CloseHandle(self.0);
        }
    }
}

fn wide(value: &OsStr) -> Vec<u16> {
    value.encode_wide().chain(Some(0)).collect()
}

fn now_ms() -> Result<u64, String> {
    Ok(SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|error| error.to_string())?
        .as_millis() as u64)
}

fn available_space(path: &Path) -> Result<u64, String> {
    let root = path
        .canonicalize()
        .map_err(|error| format!("cannot resolve {}: {error}", path.display()))?;
    let root = wide(root.as_os_str());
    let mut available = 0;
    let ok = unsafe {
        GetDiskFreeSpaceExW(
            root.as_ptr(),
            &mut available,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
        )
    };
    if ok == 0 {
        Err(format!(
            "disk-space check failed: {}",
            std::io::Error::last_os_error()
        ))
    } else {
        Ok(available)
    }
}

fn sha256(path: &Path) -> Result<String, String> {
    let mut file = File::open(path).map_err(|error| error.to_string())?;
    let mut hash = Sha256::new();
    let mut buffer = [0; 64 * 1024];
    loop {
        let read = file.read(&mut buffer).map_err(|error| error.to_string())?;
        if read == 0 {
            break;
        }
        hash.update(&buffer[..read]);
    }
    Ok(format!("{:X}", hash.finalize()))
}

fn parse_u64(value: Option<String>, name: &str) -> Result<u64, String> {
    value
        .ok_or_else(|| format!("missing {name}"))?
        .parse()
        .map_err(|_| format!("invalid {name}"))
}

fn wait_until(start_ms: Option<u64>) -> Result<(), String> {
    if let Some(start_ms) = start_ms {
        let now = now_ms()?;
        if start_ms > now + 24 * 60 * 60 * 1000 {
            return Err("scheduled start must be within 24 hours".into());
        }
        if start_ms > now {
            thread::sleep(Duration::from_millis(start_ms - now));
        }
    }
    Ok(())
}

fn terminate(child: &mut Child) -> Result<(), String> {
    if child
        .try_wait()
        .map_err(|error| error.to_string())?
        .is_none()
    {
        child.kill().map_err(|error| error.to_string())?;
    }
    child.wait().map_err(|error| error.to_string())?;
    Ok(())
}

fn observed_device(stderr: &str) -> String {
    stderr
        .lines()
        .filter(|line| line.contains("SN:") || line.contains("tuner"))
        .collect::<Vec<_>>()
        .join("; ")
}

fn provenance_path(artifact: &Path) -> PathBuf {
    PathBuf::from(format!("{}.json", artifact.display()))
}

fn write_new_json(path: &Path, value: &impl Serialize) -> Result<(), String> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|error| format!("cannot create {}: {error}", path.display()))?;
    serde_json::to_writer_pretty(&mut file, value).map_err(|error| error.to_string())?;
    file.write_all(b"\n").map_err(|error| error.to_string())
}

fn capture(mut args: impl Iterator<Item = String>) -> Result<(), String> {
    let output = PathBuf::from(args.next().ok_or("missing output.cu8")?);
    let frequency = parse_u64(args.next(), "frequency_hz")?;
    let sample_rate = parse_u64(args.next(), "sample_rate_hz")?;
    let max_bytes = parse_u64(args.next(), "max_bytes")?;
    let start_ms = args
        .next()
        .map(|value| value.parse().map_err(|_| "invalid start_unix_ms"))
        .transpose()?;
    if args.next().is_some()
        || frequency == 0
        || sample_rate == 0
        || !(2..=MAX_CAPTURE_BYTES).contains(&max_bytes)
        || max_bytes % 2 != 0
    {
        return Err(
            "capture requires positive tuning values and an even max_bytes <= 1 GiB".into(),
        );
    }
    let parent = output
        .parent()
        .filter(|path| !path.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    fs::create_dir_all(parent).map_err(|error| error.to_string())?;
    let partial = PathBuf::from(format!("{}.partial", output.display()));
    let provenance = provenance_path(&output);
    if output.exists() || partial.exists() || provenance.exists() {
        return Err("output, partial output, or provenance already exists".into());
    }
    let _partial_cleanup = PartialArtifact(partial.clone());
    if available_space(parent)? < max_bytes + 1024 * 1024 {
        return Err("insufficient free space for bounded capture".into());
    }

    let _reservation = DeviceReservation::acquire()?;
    wait_until(start_ms)?;
    let recorder = env::var("ORCLINK_RTL_SDR").unwrap_or_else(|_| "rtl_sdr.exe".into());
    let actual_start = now_ms()?;
    let mut child = Command::new(&recorder)
        .args([
            "-d",
            "0",
            "-f",
            &frequency.to_string(),
            "-s",
            &sample_rate.to_string(),
            "-S",
        ])
        .arg(&partial)
        .stdout(Stdio::null())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|error| format!("cannot start {recorder}: {error}"))?;
    let expected_ms = max_bytes.saturating_mul(1000) / sample_rate / 2;
    let deadline = Instant::now()
        + Duration::from_millis(
            expected_ms
                .saturating_mul(4)
                .saturating_add(5000)
                .min(300_000),
        );
    loop {
        if partial.metadata().map(|value| value.len()).unwrap_or(0) >= max_bytes {
            break;
        }
        if let Some(status) = child.try_wait().map_err(|error| error.to_string())? {
            return Err(format!("rtl_sdr exited before the capture bound: {status}"));
        }
        if Instant::now() >= deadline {
            terminate(&mut child)?;
            return Err("rtl_sdr capture timed out before reaching the byte bound".into());
        }
        thread::sleep(Duration::from_millis(25));
    }
    terminate(&mut child)?;
    let mut stderr = String::new();
    if let Some(mut stream) = child.stderr.take() {
        stream
            .read_to_string(&mut stderr)
            .map_err(|error| error.to_string())?;
    }
    let device = observed_device(&stderr);
    if device.is_empty() {
        return Err("rtl_sdr did not report a device serial or tuner".into());
    }
    OpenOptions::new()
        .write(true)
        .open(&partial)
        .map_err(|error| error.to_string())?
        .set_len(max_bytes)
        .map_err(|error| error.to_string())?;
    fs::rename(&partial, &output).map_err(|error| error.to_string())?;
    let record = CaptureProvenance {
        artifact: output.display().to_string(),
        sha256: sha256(&output)?,
        bytes: max_bytes,
        format: "cu8".into(),
        frequency_hz: frequency,
        sample_rate_hz: sample_rate,
        requested_start_unix_ms: start_ms,
        actual_start_unix_ms: actual_start,
        completed_unix_ms: now_ms()?,
        device_index: 0,
        observed_device: device,
        recorder,
        disposition: "bounded_by_orclink".into(),
    };
    write_new_json(&provenance, &record)?;
    println!(
        "{}",
        serde_json::to_string(&record).map_err(|error| error.to_string())?
    );
    Ok(())
}

fn decode(mut args: impl Iterator<Item = String>) -> Result<(), String> {
    let artifact = PathBuf::from(args.next().ok_or("missing capture.cu8")?);
    let provenance_path = PathBuf::from(args.next().ok_or("missing capture provenance")?);
    let pipeline = args.next().ok_or("missing SatDump pipeline")?;
    let output = PathBuf::from(args.next().ok_or("missing decoder output directory")?);
    let timeout = args
        .next()
        .map(|value| value.parse().map_err(|_| "invalid timeout_seconds"))
        .transpose()?
        .unwrap_or(60_u64);
    let record_path = PathBuf::from(format!("{}.decode.json", artifact.display()));
    if args.next().is_some()
        || timeout == 0
        || timeout > 3600
        || output.exists()
        || record_path.exists()
    {
        return Err(
            "decode requires new output/provenance paths and timeout_seconds in 1..=3600".into(),
        );
    }
    let capture: CaptureProvenance =
        serde_json::from_reader(File::open(&provenance_path).map_err(|error| error.to_string())?)
            .map_err(|error| error.to_string())?;
    let actual_hash = sha256(&artifact)?;
    if actual_hash != capture.sha256
        || artifact
            .metadata()
            .map_err(|error| error.to_string())?
            .len()
            != capture.bytes
    {
        return Err("capture does not match its provenance".into());
    }
    let decoder = env::var("ORCLINK_SATDUMP").unwrap_or_else(|_| "satdump.exe".into());
    let started = now_ms()?;
    let mut child = Command::new(&decoder)
        .args([&pipeline, "baseband"])
        .arg(&artifact)
        .arg(&output)
        .args([
            "--samplerate",
            &capture.sample_rate_hz.to_string(),
            "--baseband_format",
            "cu8",
        ])
        .spawn()
        .map_err(|error| format!("cannot start {decoder}: {error}"))?;
    let deadline = Instant::now() + Duration::from_secs(timeout);
    let status = loop {
        if let Some(status) = child.try_wait().map_err(|error| error.to_string())? {
            break status;
        }
        if Instant::now() >= deadline {
            terminate(&mut child)?;
            return Err("SatDump decoder timed out".into());
        }
        thread::sleep(Duration::from_millis(100));
    };
    let record = DecodeProvenance {
        source_artifact: artifact.display().to_string(),
        source_sha256: actual_hash,
        pipeline,
        output_directory: output.display().to_string(),
        decoder,
        started_unix_ms: started,
        completed_unix_ms: now_ms()?,
        disposition: if status.success() {
            "process_completed_signal_unverified"
        } else {
            "failed"
        }
        .into(),
    };
    write_new_json(&record_path, &record)?;
    if !status.success() {
        return Err(format!("SatDump failed: {status}"));
    }
    println!(
        "{}",
        serde_json::to_string(&record).map_err(|error| error.to_string())?
    );
    Ok(())
}

fn run() -> Result<(), String> {
    let mut args = env::args().skip(1);
    match args.next().as_deref() {
        None | Some("snapshot") if args.next().is_none() => snapshot(),
        Some("capture") => capture(args),
        Some("decode") => decode(args),
        _ => Err("usage:\n  orclink-adapter-rtl-sdr snapshot\n  orclink-adapter-rtl-sdr capture <output.cu8> <frequency_hz> <sample_rate_hz> <max_bytes> [start_unix_ms]\n  orclink-adapter-rtl-sdr decode <capture.cu8> <capture.json> <pipeline> <new_output_dir> [timeout_seconds]".into()),
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
    #[test]
    fn extracts_only_device_identity() {
        let text = "Found 1 device(s):\n  0: Generic, RTL2832U, SN: 123\nFound Rafael Micro R828D tuner\nSampling at 2048000 S/s.";
        assert_eq!(
            super::observed_device(text),
            "  0: Generic, RTL2832U, SN: 123; Found Rafael Micro R828D tuner"
        );
    }
}
