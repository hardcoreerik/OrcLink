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
