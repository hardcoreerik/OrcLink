use orclink_protocol::AdapterEnvelope;
use serde::{Deserialize, Serialize};
use serialport::SerialPortType;
use sha2::{Digest, Sha256};
use std::{
    env, fs,
    fs::{File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
    process::{Command, ExitCode, Output},
    thread,
    time::{SystemTime, UNIX_EPOCH},
};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Manifest {
    schema_version: u64,
    chip: String,
    usb_vid: u16,
    usb_pid: u16,
    target_mac: String,
    target_node_id: String,
    flash_offset: u32,
    flash_mode: String,
    flash_frequency: String,
    flash_size: String,
    firmware_version: String,
    sha256: String,
}

#[derive(Serialize)]
struct AuditRecord<'a> {
    recorded_at_ms: u128,
    stage: &'a str,
    status: &'a str,
    detail: &'a str,
}

fn append_audit(path: &Path, stage: &str, status: &str, detail: &str) -> Result<(), String> {
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)
        .map_err(|error| error.to_string())?;
    serde_json::to_writer(
        &mut file,
        &AuditRecord {
            recorded_at_ms: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map_err(|error| error.to_string())?
                .as_millis(),
            stage,
            status,
            detail,
        },
    )
    .map_err(|error| error.to_string())?;
    writeln!(file).map_err(|error| error.to_string())
}

fn checked<T>(audit: &Path, stage: &str, result: Result<T, String>) -> Result<T, String> {
    match result {
        Ok(value) => {
            append_audit(audit, stage, "ok", "verified")?;
            Ok(value)
        }
        Err(error) => {
            append_audit(audit, stage, "error", &error)?;
            Err(error)
        }
    }
}

fn file_sha256(path: &Path) -> Result<String, String> {
    let mut file = File::open(path).map_err(|error| format!("{}: {error}", path.display()))?;
    let mut digest = Sha256::new();
    let mut buffer = [0; 64 * 1024];
    loop {
        let count = file.read(&mut buffer).map_err(|error| error.to_string())?;
        if count == 0 {
            break;
        }
        digest.update(&buffer[..count]);
    }
    Ok(format!("{:X}", digest.finalize()))
}

fn require_usb_port(name: &str, vid: u16, pid: u16) -> Result<(), String> {
    let port = serialport::available_ports()
        .map_err(|error| error.to_string())?
        .into_iter()
        .find(|port| port.port_name.eq_ignore_ascii_case(name))
        .ok_or_else(|| format!("{name} is not connected"))?;
    match port.port_type {
        SerialPortType::UsbPort(info) if info.vid == vid && info.pid == pid => Ok(()),
        SerialPortType::UsbPort(info) => Err(format!(
            "{name} USB identity {:04x}:{:04x} does not match approved {vid:04x}:{pid:04x}",
            info.vid, info.pid
        )),
        _ => Err(format!("{name} is not an approved USB serial device")),
    }
}

fn command_output(mut command: Command) -> Result<Output, String> {
    let output = command.output().map_err(|error| error.to_string())?;
    if output.status.success() {
        Ok(output)
    } else {
        Err(format!(
            "{}\n{}",
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr)
        )
        .trim()
        .to_string())
    }
}

fn parse_mac(output: &str) -> Result<String, String> {
    output
        .lines()
        .find_map(|line| line.trim().strip_prefix("MAC:"))
        .map(|value| value.trim().to_ascii_lowercase())
        .filter(|value| value.matches(':').count() == 5)
        .ok_or_else(|| "esptool did not report a MAC address".into())
}

fn esptool_python() -> Result<PathBuf, String> {
    if let Some(path) = env::var_os("ORCLINK_ESPTOOL_PYTHON") {
        return Ok(path.into());
    }
    let profile = env::var_os("USERPROFILE").ok_or("USERPROFILE is not set")?;
    Ok(PathBuf::from(profile)
        .join(".platformio")
        .join("penv")
        .join("Scripts")
        .join("python.exe"))
}

fn read_mac(python: &Path, manifest: &Manifest, port: &str) -> Result<String, String> {
    let mut command = Command::new(python);
    command.args([
        "-m",
        "esptool",
        "--chip",
        &manifest.chip,
        "--port",
        port,
        "read-mac",
    ]);
    let output = command_output(command)?;
    parse_mac(&String::from_utf8_lossy(&output.stdout))
}

fn flash(python: &Path, manifest: &Manifest, artifact: &Path, port: &str) -> Result<(), String> {
    let mut command = Command::new(python);
    command
        .args([
            "-m",
            "esptool",
            "--chip",
            &manifest.chip,
            "--port",
            port,
            "--baud",
            "1500000",
            "write-flash",
            "--flash-mode",
            &manifest.flash_mode,
            "--flash-freq",
            &manifest.flash_frequency,
            "--flash-size",
            &manifest.flash_size,
            &format!("0x{:x}", manifest.flash_offset),
        ])
        .arg(artifact);
    command.env("PYTHONUTF8", "1");
    command_output(command).map(|_| ())
}

fn adapter_path() -> Result<PathBuf, String> {
    if let Some(path) = env::var_os("ORCLINK_M5TAB5_ADAPTER") {
        return Ok(path.into());
    }
    let executable = env::current_exe().map_err(|error| error.to_string())?;
    Ok(executable
        .parent()
        .ok_or("workflow executable has no directory")?
        .join(format!("orclink-adapter-m5tab5{}", env::consts::EXE_SUFFIX)))
}

fn prepare_device(manifest: &Manifest, port: &str) -> Result<(), String> {
    let credential = PathBuf::from(".orclink").join(format!("m5tab5-{port}.key"));
    let mut command = Command::new(adapter_path()?);
    command
        .arg(port)
        .arg(credential)
        .arg("--once")
        .arg("--prepare-flash")
        .arg(&manifest.sha256);
    command_output(command).map(|_| ())
}

fn verify_boot(manifest: &Manifest, port: &str) -> Result<(), String> {
    thread::sleep(std::time::Duration::from_secs(3));
    let credential = PathBuf::from(".orclink").join(format!("m5tab5-{port}.key"));
    let mut last_error = "post-flash adapter did not return a valid snapshot".to_string();
    for _ in 0..3 {
        let output = command_output({
            let mut command = Command::new(adapter_path()?);
            command.arg(port).arg(&credential).arg("--once");
            command
        });
        let (stdout, stderr) = match output {
            Ok(output) => (
                String::from_utf8(output.stdout).map_err(|error| error.to_string())?,
                String::from_utf8_lossy(&output.stderr).into_owned(),
            ),
            Err(error) => {
                let bad_log = error.to_ascii_lowercase();
                for marker in ["watchdog", "guru meditation", "panic'ed", "abort()"] {
                    if bad_log.contains(marker) {
                        return Err(format!("post-flash log contains {marker}"));
                    }
                }
                last_error = error;
                thread::sleep(std::time::Duration::from_secs(2));
                continue;
            }
        };
        let bad_log = format!("{stdout}\n{stderr}").to_ascii_lowercase();
        for marker in ["watchdog", "guru meditation", "panic'ed", "abort()"] {
            if bad_log.contains(marker) {
                return Err(format!("post-flash log contains {marker}"));
            }
        }

        let mut version_ok = false;
        let mut identity_ok = false;
        for line in stdout.lines().filter(|line| line.starts_with('{')) {
            let envelope: AdapterEnvelope =
                serde_json::from_str(line).map_err(|error| error.to_string())?;
            version_ok |= envelope
                .payload
                .get("adapter")
                .and_then(|value| value.get("version"))
                .and_then(|value| value.as_str())
                == Some(manifest.firmware_version.as_str());
            identity_ok |= envelope
                .payload
                .get("node_id")
                .and_then(|value| value.as_str())
                == Some(manifest.target_node_id.as_str());
        }
        if version_ok && identity_ok {
            return Ok(());
        }
        last_error = "post-flash snapshot did not match manifest version and node".into();
        thread::sleep(std::time::Duration::from_secs(2));
    }
    Err(last_error)
}

fn run() -> Result<(), String> {
    let arguments = env::args().collect::<Vec<_>>();
    if arguments.len() != 6 || arguments[4] != "--approve" {
        return Err(
            "usage: orclink-firmware-test <manifest.json> <artifact.bin> <port> --approve <sha256>"
                .into(),
        );
    }
    let manifest_path = Path::new(&arguments[1]);
    let artifact = Path::new(&arguments[2]);
    let port = &arguments[3];
    let manifest: Manifest =
        serde_json::from_slice(&fs::read(manifest_path).map_err(|error| error.to_string())?)
            .map_err(|error| error.to_string())?;
    if manifest.schema_version != 1 {
        return Err("unsupported artifact manifest schema".into());
    }

    fs::create_dir_all(".orclink").map_err(|error| error.to_string())?;
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|error| error.to_string())?
        .as_secs();
    let audit = PathBuf::from(".orclink").join(format!("firmware-test-{now}.jsonl"));

    let digest = checked(&audit, "artifact_hash", file_sha256(artifact))?;
    append_audit(&audit, "artifact_identity", "observed", &digest)?;
    checked(
        &audit,
        "approval",
        if digest.eq_ignore_ascii_case(&manifest.sha256)
            && digest.eq_ignore_ascii_case(&arguments[5])
        {
            Ok(())
        } else {
            Err("artifact or approval digest does not match manifest".into())
        },
    )?;
    checked(
        &audit,
        "serial_discovery",
        require_usb_port(port, manifest.usb_vid, manifest.usb_pid),
    )?;
    checked(
        &audit,
        "device_update_display",
        prepare_device(&manifest, port),
    )?;
    let python = esptool_python()?;
    let mac = checked(
        &audit,
        "stable_identity",
        read_mac(&python, &manifest, port),
    )?;
    append_audit(&audit, "target_identity", "observed", &mac)?;
    checked(
        &audit,
        "target_binding",
        if mac.eq_ignore_ascii_case(&manifest.target_mac) {
            Ok(())
        } else {
            Err(format!(
                "connected MAC {mac} does not match approved {}",
                manifest.target_mac
            ))
        },
    )?;
    checked(&audit, "flash", flash(&python, &manifest, artifact, port))?;
    checked(&audit, "reboot_verify", verify_boot(&manifest, port))?;
    append_audit(
        &audit,
        "result",
        "verified",
        &format!(
            "node={} version={} sha256={digest}",
            manifest.target_node_id, manifest.firmware_version
        ),
    )?;

    println!(
        "firmware_test=verified target={} artifact_sha256={} version={} audit={}",
        manifest.target_node_id,
        digest,
        manifest.firmware_version,
        audit.display()
    );
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
    fn parses_only_mac_lines_and_detects_bad_boot_markers() {
        assert_eq!(
            super::parse_mac("noise\nMAC: 30:ed:a0:e2:e6:95\n").unwrap(),
            "30:ed:a0:e2:e6:95"
        );
        assert!(super::parse_mac("MAC: invalid").is_err());
        assert!(
            "Guru Meditation Error"
                .to_ascii_lowercase()
                .contains("guru meditation")
        );
    }
}
