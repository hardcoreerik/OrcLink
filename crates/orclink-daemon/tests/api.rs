use orclink_daemon::Store;
use std::io::Cursor;

#[test]
fn persists_adapter_records_for_read_api() {
    let mut store = Store::open(":memory:").unwrap();
    let records = r#"{"type":"hello","message_id":"hello_1","protocol_version":{"major":1,"minor":0},"payload":{}}
{"type":"node_snapshot","message_id":"snapshot_1","protocol_version":{"major":1,"minor":0},"payload":{"node_id":"node_1"}}
{"type":"command_update","message_id":"command_1","protocol_version":{"major":1,"minor":0},"payload":{"state":"Succeeded"}}
"#;
    assert_eq!(store.ingest("test", Cursor::new(records)).unwrap(), 3);
    assert_eq!(store.ingest("test", Cursor::new(records)).unwrap(), 0);
    assert_eq!(
        store
            .response("/api/snapshot")
            .unwrap()
            .as_array()
            .unwrap()
            .len(),
        1
    );
    assert_eq!(
        store
            .response("/api/operations")
            .unwrap()
            .as_array()
            .unwrap()
            .len(),
        1
    );
    assert_eq!(
        store
            .response("/api/timeline")
            .unwrap()
            .as_array()
            .unwrap()
            .len(),
        3
    );
}

#[test]
fn exposes_versioned_registry_and_bounded_timeline() {
    let mut store = Store::open(":memory:").unwrap();
    let records = r#"{"type":"node_snapshot","message_id":"snapshot_1","protocol_version":{"major":1,"minor":0},"payload":{"node_id":"m5tab5_1","display_name":"Tab5","connection_state":"online","capabilities":["m5tab5.health.read","m5tab5.display.status"]}}
{"type":"heartbeat","message_id":"heartbeat_1","protocol_version":{"major":1,"minor":0},"payload":{"node_id":"m5tab5_1"}}
"#;
    store.ingest("test", Cursor::new(records)).unwrap();

    let status = store.handle("GET", "/api/v1/status", &[]);
    assert_eq!(status.status, 200);
    assert_eq!(status.body["api_version"], "1.0");
    assert_eq!(status.body["counts"]["nodes"], 1);

    let nodes = store.handle("GET", "/api/v1/nodes", &[]);
    assert_eq!(nodes.body.as_array().unwrap().len(), 1);
    let node = store.handle("GET", "/api/v1/nodes/m5tab5_1", &[]);
    assert_eq!(node.status, 200);
    assert_eq!(node.body["payload"]["display_name"], "Tab5");

    let capabilities = store.handle("GET", "/api/v1/capabilities", &[]);
    assert_eq!(capabilities.body.as_array().unwrap().len(), 2);
    assert!(
        capabilities
            .body
            .as_array()
            .unwrap()
            .iter()
            .all(|capability| capability["invokable"] == true)
    );

    let timeline = store.handle("GET", "/api/v1/timeline?after=0&limit=1", &[]);
    assert_eq!(timeline.body.as_array().unwrap().len(), 1);
    assert_eq!(store.handle("GET", "/api/v1/openapi.json", &[]).status, 200);
}

#[test]
fn persists_idempotent_proposals_and_enforces_risk_floors() {
    let mut store = Store::open(":memory:").unwrap();
    store
        .ingest(
            "test",
            Cursor::new(
                r#"{"type":"node_snapshot","message_id":"snapshot_1","protocol_version":{"major":1,"minor":0},"payload":{"node_id":"m5tab5_1","connection_state":"online","capabilities":["m5tab5.health.read","m5tab5.display.status","m5tab5.network.configure"]}}
"#,
            ),
        )
        .unwrap();

    let read = br#"{"schema_version":1,"proposal_id":"proposal_read","requester_id":"control-room.local","target":{"node_id":"m5tab5_1","component_id":null},"capability":{"name":"m5tab5.health.read","schema_version":1},"arguments":{},"risk":"R0","approval_required":false,"idempotency_key":"read_1"}"#;
    let created = store.handle("POST", "/api/v1/proposals", read);
    assert_eq!(created.status, 201);
    assert_eq!(created.body["state"], "Proposed");
    assert_eq!(created.body["approval_required"], false);

    let duplicate = store.handle("POST", "/api/v1/proposals", read);
    assert_eq!(duplicate.status, 200);
    assert_eq!(duplicate.body["duplicate"], true);
    assert_eq!(duplicate.body["sequence"], created.body["sequence"]);

    let display = br#"{"schema_version":1,"proposal_id":"proposal_display","requester_id":"control-room.local","target":{"node_id":"m5tab5_1","component_id":null},"capability":{"name":"m5tab5.display.status","schema_version":1},"arguments":{"message":"Ready"},"risk":"R1","approval_required":false,"idempotency_key":"display_1"}"#;
    let approval = store.handle("POST", "/api/v1/proposals", display);
    assert_eq!(approval.status, 201);
    assert_eq!(approval.body["state"], "ApprovalRequired");
    assert_eq!(approval.body["approval_required"], true);

    let underclassified = br#"{"schema_version":1,"proposal_id":"proposal_wifi","requester_id":"control-room.local","target":{"node_id":"m5tab5_1","component_id":null},"capability":{"name":"m5tab5.network.configure","schema_version":1},"arguments":{},"risk":"R0","approval_required":false,"idempotency_key":"wifi_1"}"#;
    let rejected = store.handle("POST", "/api/v1/proposals", underclassified);
    assert_eq!(rejected.status, 422);
    assert_eq!(rejected.body["state"], "Rejected");
    assert_eq!(rejected.body["reason"], "risk_below_capability_floor");

    let operations = store.handle("GET", "/api/v1/operations", &[]);
    assert_eq!(operations.body.as_array().unwrap().len(), 3);
}

#[test]
fn rejects_direct_secrets_without_persisting_their_values() {
    let mut store = Store::open(":memory:").unwrap();
    store
        .ingest(
            "test",
            Cursor::new(
                r#"{"type":"node_snapshot","message_id":"snapshot_1","protocol_version":{"major":1,"minor":0},"payload":{"node_id":"m5tab5_1","connection_state":"online","capabilities":["m5tab5.network.configure"]}}
"#,
            ),
        )
        .unwrap();
    let proposal = br#"{"schema_version":1,"proposal_id":"proposal_secret","requester_id":"control-room.local","target":{"node_id":"m5tab5_1","component_id":null},"capability":{"name":"m5tab5.network.configure","schema_version":1},"arguments":{"wifi_password":"do-not-store-this"},"risk":"R2","approval_required":true,"idempotency_key":"secret_1"}"#;
    let rejected = store.handle("POST", "/api/v1/proposals", proposal);
    assert_eq!(rejected.status, 422);
    assert_eq!(
        rejected.body["reason"],
        "secret_values_require_protected_references"
    );
    let timeline = store.handle("GET", "/api/v1/timeline", &[]);
    assert!(!timeline.body.to_string().contains("do-not-store-this"));
}

#[test]
fn blocks_capabilities_when_the_observed_target_is_offline() {
    let mut store = Store::open(":memory:").unwrap();
    store
        .ingest(
            "test",
            Cursor::new(
                r#"{"type":"node_snapshot","message_id":"snapshot_1","protocol_version":{"major":1,"minor":0},"payload":{"node_id":"rtl-sdr.device0","connection_state":"offline","capabilities":["rtl_sdr.capture"]}}
"#,
            ),
        )
        .unwrap();
    let catalog = store.handle("GET", "/api/v1/capabilities", &[]);
    assert_eq!(catalog.body[0]["invokable"], false);
    assert_eq!(catalog.body[0]["online_nodes"].as_array().unwrap().len(), 0);

    let proposal = br#"{"schema_version":1,"proposal_id":"proposal_capture","requester_id":"control-room.local","target":{"node_id":"rtl-sdr.device0","component_id":null},"capability":{"name":"rtl_sdr.capture","schema_version":1},"arguments":{},"risk":"R1","approval_required":true,"idempotency_key":"capture_1"}"#;
    let rejected = store.handle("POST", "/api/v1/proposals", proposal);
    assert_eq!(rejected.status, 422);
    assert_eq!(rejected.body["reason"], "target_unavailable");
}
