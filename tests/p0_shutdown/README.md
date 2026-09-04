# P0 Rack shutdown timing contract

Real OBS 32.1.2 smoke testing exposed multi-second application close latency while a Rack helper was active.

Acceptance target for the bridge stop path:

- publish `shutdown_requested` before waiting for helper exit;
- wake the helper request/UI/response event paths immediately;
- graceful helper wait <= 250 ms;
- forced-exit observation wait <= 250 ms;
- total bridge-owned helper wait budget <= 500 ms;
- preserve out-of-process isolation and dry/fail-safe OBS behavior.

This contract is intentionally scoped to shutdown latency. Rack visual redesign is tracked separately.
