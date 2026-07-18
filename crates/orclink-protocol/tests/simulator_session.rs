use orclink_protocol::{Summary, load_and_admit};
use std::{fs, io::Cursor, path::PathBuf};

#[test]
fn admits_fixture_and_rejects_cross_record_violations() {
    let fixture = fs::read_to_string(
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../schemas/examples/simulator-session.jsonl"),
    )
    .unwrap();
    let admit = |input: &str| load_and_admit(Cursor::new(input));

    assert_eq!(
        admit(&fixture).unwrap(),
        Summary {
            nodes: 1,
            events: 1,
            capabilities: 1,
            commands: 1,
            adapter_envelopes: 1,
        }
    );

    for invalid in [
        fixture.replace("\"major\":1", "\"major\":2"),
        fixture.replace(
            "\"capabilities\":[{\"name\":\"sim.health.read\",\"schema_version\":1}]",
            "\"capabilities\":[{\"name\":\"sim.health.read\",\"schema_version\":2}]",
        ),
        fixture.replace("\"node_id\":\"node_sim_01\"", "\"node_id\":\"missing\""),
        fixture.replace(
            "\"capabilities\":[{\"name\":\"sim.health.read\",\"schema_version\":1}]",
            "\"capabilities\":[]",
        ),
        fixture
            .lines()
            .filter(|line| !line.contains("\"availability\":\"online_only\""))
            .collect::<Vec<_>>()
            .join("\n"),
        fixture.replace(
            "\"deadline\":\"2026-07-18T12:00:02-07:00\"",
            "\"deadline\":\"2026-07-18T12:00:01-07:00\"",
        ),
    ] {
        assert!(admit(&invalid).is_err());
    }
}
