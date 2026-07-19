use orclink_protocol::TheOrcProposal;

#[test]
fn admits_typed_proposal_and_rejects_execution_channels() {
    let proposal = r#"{
        "schema_version":1,
        "proposal_id":"proposal_1",
        "requester_id":"theorc",
        "target":{"node_id":"m5tab5_30eda0e2e695","component_id":null},
        "capability":{"name":"m5tab5.display.status","schema_version":1},
        "arguments":{"message":"Ready"},
        "risk":"R0",
        "approval_required":false,
        "idempotency_key":"proposal_1"
    }"#;
    let parsed: TheOrcProposal = serde_json::from_str(proposal).unwrap();
    assert_eq!(parsed.requester_id, "theorc");

    let with_shell = proposal.replace(
        "\"idempotency_key\":\"proposal_1\"",
        "\"idempotency_key\":\"proposal_1\",\"shell\":\"whoami\"",
    );
    assert!(serde_json::from_str::<TheOrcProposal>(&with_shell).is_err());
}
