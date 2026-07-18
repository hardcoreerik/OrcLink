use orclink_core::{Command, NodeBehavior, Simulator};
use std::{fs, time::SystemTime};

fn command<'a>(id: &'a str, target: &'a str) -> Command<'a> {
    Command {
        id,
        target_node: target,
        capability: "sim.health.read",
        idempotency_key: id,
        deadline_ms: 100,
        policy_allows: true,
        cancel_requested: false,
    }
}

#[test]
fn proves_phase0_lifecycle_and_recovery() {
    let path = std::env::temp_dir().join(format!(
        "orclink-phase0-{}-{}.sqlite",
        std::process::id(),
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    let mut simulator = Simulator::open(&path).unwrap();
    for (id, behavior) in [
        ("online", NodeBehavior::Online),
        ("offline", NodeBehavior::Offline),
        ("slow", NodeBehavior::Slow),
        ("failing", NodeBehavior::Failing),
    ] {
        simulator.register_node(id, "sim.health.read", behavior);
    }

    assert_eq!(
        simulator.run(&command("success", "online")).unwrap(),
        "Succeeded"
    );
    assert_eq!(
        simulator.timeline("success").unwrap(),
        [
            "Proposed",
            "Validated",
            "Authorized",
            "Queued",
            "Executing",
            "Verifying",
            "Succeeded"
        ]
    );
    assert_eq!(
        simulator.run(&command("success", "online")).unwrap(),
        "Succeeded"
    );
    assert_eq!(simulator.timeline("success").unwrap().len(), 7);

    let mut denied = command("denied", "online");
    denied.policy_allows = false;
    assert_eq!(simulator.run(&denied).unwrap(), "Rejected");
    assert_eq!(
        simulator.run(&command("offline", "offline")).unwrap(),
        "Rejected"
    );
    assert_eq!(
        simulator.run(&command("timeout", "slow")).unwrap(),
        "TimedOut"
    );
    let mut patient = command("slow-success", "slow");
    patient.deadline_ms = 2_000;
    assert_eq!(simulator.run(&patient).unwrap(), "Succeeded");
    assert_eq!(
        simulator.run(&command("failure", "failing")).unwrap(),
        "Failed"
    );

    let mut cancelled = command("cancelled", "online");
    cancelled.cancel_requested = true;
    assert_eq!(simulator.run(&cancelled).unwrap(), "Cancelled");

    assert_eq!(
        simulator.start(&command("interrupted", "online")).unwrap(),
        "Executing"
    );
    drop(simulator);
    let mut restarted = Simulator::open(&path).unwrap();
    assert_eq!(restarted.reconcile_after_restart().unwrap(), 1);
    assert_eq!(
        restarted.timeline("interrupted").unwrap().last().unwrap(),
        "Uncertain"
    );

    drop(restarted);
    fs::remove_file(path).unwrap();
}
