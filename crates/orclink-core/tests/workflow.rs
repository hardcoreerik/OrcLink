use orclink_core::{StepCondition, WorkflowStep, run_workflow};

#[test]
fn bounds_retries_runs_cleanup_and_skips_dependents() {
    let steps = [
        WorkflowStep {
            name: "prepare",
            condition: StepCondition::Always,
            approval_required: false,
            max_attempts: 2,
            cleanup: Some("cleanup"),
        },
        WorkflowStep {
            name: "apply",
            condition: StepCondition::PreviousSucceeded,
            approval_required: false,
            max_attempts: 1,
            cleanup: None,
        },
    ];
    let mut calls = Vec::new();
    let result = run_workflow(&steps, true, |name, attempt| {
        calls.push((name.to_string(), attempt));
        (name == "cleanup").then_some(()).ok_or("failed".into())
    });

    assert_eq!(
        calls,
        [
            ("prepare".into(), 1),
            ("prepare".into(), 2),
            ("cleanup".into(), 1)
        ]
    );
    assert_eq!(result[0].attempts, 2);
    assert_eq!(result[0].state, "Failed");
    assert_eq!(result[0].cleanup_state.as_deref(), Some("Succeeded"));
    assert_eq!(result[1].state, "Skipped");
}

#[test]
fn approval_gate_prevents_invocation() {
    let steps = [WorkflowStep {
        name: "apply",
        condition: StepCondition::Always,
        approval_required: true,
        max_attempts: 1,
        cleanup: None,
    }];
    let result = run_workflow(&steps, false, |_, _| panic!("must not execute"));
    assert_eq!(result[0].state, "ApprovalRequired");
    assert_eq!(result[0].attempts, 0);
}
