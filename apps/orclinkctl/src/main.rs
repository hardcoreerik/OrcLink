use orclink_client::{CapabilityView, Client, TimelineEntry};
use orclink_protocol::{AdapterEnvelope, ProposalReceipt, TheOrcProposal};
use serde::Serialize;
use std::{env, fs::File, process::ExitCode, thread, time::Duration};

struct Options {
    url: String,
    json: bool,
    command: String,
    arguments: Vec<String>,
}

fn usage() -> String {
    "usage: orclinkctl [--url http://127.0.0.1:4017] [--json] <command>\n\
     commands:\n\
       status                 daemon/API status\n\
       doctor                 connectivity and registry checks\n\
       nodes                  latest node projections\n\
       node NODE_ID           one node projection\n\
       capabilities           observed capabilities and risk floors\n\
       operations             command lifecycle records\n\
       timeline [LIMIT]       bounded Chronicle facts\n\
       watch [SECONDS]        poll new Chronicle facts\n\
       propose FILE.json      submit a typed proposal\n\
       proposal PROPOSAL_ID   read a proposal receipt\n\
       openapi                print the local OpenAPI document"
        .into()
}

fn options() -> Result<Options, String> {
    let mut values = env::args().skip(1).collect::<Vec<_>>();
    let mut url = env::var("ORCLINK_URL").unwrap_or_else(|_| "http://127.0.0.1:4017".into());
    let mut json = false;
    let mut index = 0;
    while index < values.len() {
        match values[index].as_str() {
            "--json" => {
                json = true;
                values.remove(index);
            }
            "--url" => {
                if index + 1 >= values.len() {
                    return Err("--url requires a value".into());
                }
                url = values.remove(index + 1);
                values.remove(index);
            }
            _ => index += 1,
        }
    }
    let command = values.first().cloned().ok_or_else(usage)?;
    Ok(Options {
        url,
        json,
        command,
        arguments: values.into_iter().skip(1).collect(),
    })
}

fn pretty(value: &impl Serialize) -> Result<(), String> {
    println!(
        "{}",
        serde_json::to_string_pretty(value).map_err(|error| error.to_string())?
    );
    Ok(())
}

fn print_nodes(nodes: &[AdapterEnvelope], json: bool) -> Result<(), String> {
    if json {
        return pretty(&nodes);
    }
    for node in nodes {
        let payload = &node.payload;
        println!(
            "{:<32} {:<12} {}",
            payload["node_id"].as_str().unwrap_or("unknown"),
            payload["connection_state"].as_str().unwrap_or("observed"),
            payload["display_name"].as_str().unwrap_or("OrcLink node")
        );
    }
    Ok(())
}

fn print_capabilities(capabilities: &[CapabilityView], json: bool) -> Result<(), String> {
    if json {
        return pretty(&capabilities);
    }
    for capability in capabilities {
        println!(
            "{:<38} {:<8} {}",
            capability.name,
            capability
                .risk_floor
                .map(|risk| format!("{risk:?}"))
                .unwrap_or_else(|| "BLOCKED".into()),
            capability.nodes.join(", ")
        );
    }
    Ok(())
}

fn print_operations(operations: &[AdapterEnvelope], json: bool) -> Result<(), String> {
    if json {
        return pretty(&operations);
    }
    for operation in operations {
        let payload = &operation.payload;
        println!(
            "{:<28} {:<20} {:<18} {}",
            payload["command_id"]
                .as_str()
                .unwrap_or(&operation.message_id),
            payload["state"].as_str().unwrap_or("Unknown"),
            payload["node_id"].as_str().unwrap_or("unknown"),
            payload["capability"]["name"]
                .as_str()
                .or_else(|| payload["capability"].as_str())
                .unwrap_or("unknown")
        );
    }
    Ok(())
}

fn print_timeline(entries: &[TimelineEntry], json: bool) -> Result<(), String> {
    if json {
        return pretty(&entries);
    }
    for entry in entries {
        println!(
            "#{:<6} {:<24} {:<20} {}",
            entry.sequence,
            entry.source,
            format!("{:?}", entry.record.message_type),
            entry.record.message_id
        );
    }
    Ok(())
}

fn print_receipt(receipt: &ProposalReceipt, json: bool) -> Result<(), String> {
    if json {
        pretty(receipt)
    } else {
        println!(
            "{}: {:?} ({}) risk={:?} approval_required={} sequence={}",
            receipt.proposal_id,
            receipt.state,
            receipt.reason,
            receipt.risk,
            receipt.approval_required,
            receipt.sequence
        );
        Ok(())
    }
}

fn run() -> Result<(), String> {
    let options = options()?;
    let client = Client::new(&options.url).map_err(|error| error.to_string())?;
    match (options.command.as_str(), options.arguments.as_slice()) {
        ("status", []) => {
            let status = client.status().map_err(|error| error.to_string())?;
            if options.json {
                pretty(&status)
            } else {
                println!(
                    "OrcLink {} | API {} | protocol {}.{} | {} nodes | {} capabilities | {} proposals",
                    status.status,
                    status.api_version,
                    status.protocol.major,
                    status.protocol.minor,
                    status.counts.nodes,
                    status.counts.capabilities,
                    status.counts.proposals
                );
                Ok(())
            }
        }
        ("doctor", []) => {
            let status = client.status().map_err(|error| error.to_string())?;
            let nodes = client.nodes().map_err(|error| error.to_string())?;
            let capabilities = client.capabilities().map_err(|error| error.to_string())?;
            let invokable = capabilities.iter().filter(|value| value.invokable).count();
            let fixed_floor = capabilities
                .iter()
                .filter(|value| value.risk_floor.is_some())
                .count();
            if options.json {
                pretty(
                    &serde_json::json!({"status":status,"nodes":nodes.len(),"capabilities":capabilities.len(),"fixed_risk_floor":fixed_floor,"invokable":invokable}),
                )
            } else {
                println!(
                    "[PASS] daemon {} at {}",
                    status.daemon_version,
                    client.base_url()
                );
                println!(
                    "[PASS] protocol {}.{} / API {}",
                    status.protocol.major, status.protocol.minor, status.api_version
                );
                println!(
                    "[{}] {} latest node projections",
                    if nodes.is_empty() { "WARN" } else { "PASS" },
                    nodes.len()
                );
                println!(
                    "[{}] {fixed_floor}/{} capabilities have fixed risk floors",
                    if fixed_floor == capabilities.len() {
                        "PASS"
                    } else {
                        "WARN"
                    },
                    capabilities.len()
                );
                println!(
                    "[{}] {invokable}/{} capabilities are currently invokable",
                    if invokable == capabilities.len() {
                        "PASS"
                    } else {
                        "WARN"
                    },
                    capabilities.len()
                );
                Ok(())
            }
        }
        ("nodes", []) => print_nodes(
            &client.nodes().map_err(|error| error.to_string())?,
            options.json,
        ),
        ("node", [node_id]) => pretty(&client.node(node_id).map_err(|error| error.to_string())?),
        ("capabilities", []) => print_capabilities(
            &client.capabilities().map_err(|error| error.to_string())?,
            options.json,
        ),
        ("operations", []) => print_operations(
            &client.operations().map_err(|error| error.to_string())?,
            options.json,
        ),
        ("timeline", arguments) if arguments.len() <= 1 => {
            let limit = arguments
                .first()
                .map(|value| value.parse().map_err(|_| "LIMIT must be an integer"))
                .transpose()?
                .unwrap_or(100);
            print_timeline(
                &client
                    .timeline(0, limit)
                    .map_err(|error| error.to_string())?,
                options.json,
            )
        }
        ("watch", arguments) if arguments.len() <= 1 => {
            let seconds: u64 = arguments
                .first()
                .map(|value| value.parse().map_err(|_| "SECONDS must be an integer"))
                .transpose()?
                .unwrap_or(30);
            let mut after = 0;
            for _ in 0..seconds {
                let entries = client
                    .timeline(after, 1_000)
                    .map_err(|error| error.to_string())?;
                if let Some(last) = entries.last() {
                    after = last.sequence;
                }
                print_timeline(&entries, options.json)?;
                thread::sleep(Duration::from_secs(1));
            }
            Ok(())
        }
        ("propose", [path]) => {
            let proposal: TheOrcProposal =
                serde_json::from_reader(File::open(path).map_err(|error| error.to_string())?)
                    .map_err(|error| error.to_string())?;
            print_receipt(
                &client
                    .propose(&proposal)
                    .map_err(|error| error.to_string())?,
                options.json,
            )
        }
        ("proposal", [proposal_id]) => print_receipt(
            &client
                .proposal(proposal_id)
                .map_err(|error| error.to_string())?,
            options.json,
        ),
        ("openapi", []) => pretty(&client.openapi().map_err(|error| error.to_string())?),
        _ => Err(usage()),
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
