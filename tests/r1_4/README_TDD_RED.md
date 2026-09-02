# R1-4 TDD RED checkpoint

Fixed-point base: `6bb2719c480064c90235928acb4fdaa136246c2c`.

This checkpoint is intentionally test/workflow only. Production `src/rack` is unchanged.

The R1-4 integration requires a Rack crash breadcrumb and bounded recovery contract that does not exist at the base:

- `RackBreadcrumbPhase` and coherent breadcrumb transport fields;
- `read_rack_breadcrumb`;
- `RackFailureConfidence` + `classify_rack_helper_death`;
- `RackRecoveryPolicy` using the existing bounded product recovery discipline.

Expected RED is a compile/source-contract failure for those missing production contracts after the real crash-once VST3 fixtures configure/build normally.
