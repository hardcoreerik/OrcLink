use orclink_core::{Command, NodeBehavior, Simulator};

fn main() -> Result<(), String> {
    let mut simulator = Simulator::open(":memory:")?;
    for (id, behavior) in [
        ("online", NodeBehavior::Online),
        ("offline", NodeBehavior::Offline),
        ("slow", NodeBehavior::Slow),
        ("failing", NodeBehavior::Failing),
    ] {
        simulator.register_node(id, "sim.health.read", behavior);
    }

    for (id, target, deadline_ms, allowed, cancel) in [
        ("success", "online", 100, true, false),
        ("policy-denied", "online", 100, false, false),
        ("offline", "offline", 100, true, false),
        ("timeout", "slow", 100, true, false),
        ("failure", "failing", 100, true, false),
        ("cancelled", "online", 100, true, true),
    ] {
        let state = simulator.run(&Command {
            id,
            target_node: target,
            capability: "sim.health.read",
            idempotency_key: id,
            deadline_ms,
            policy_allows: allowed,
            cancel_requested: cancel,
        })?;
        println!("{id}={state}");
    }
    Ok(())
}
