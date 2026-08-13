# P0 architecture notes

## Failure model

The OBS callback is the producer. The helper is the consumer. Four slots are deliberately used so a timed-out `Processing` slot is never overwritten by a later callback.

Slot lifecycle:

`Free -> Claimed -> Ready -> Processing -> Done -> Claimed/Free`

On deadline expiration:

- `Ready` can be cancelled back to `Free` because helper has not claimed it.
- `Processing` is left untouched until helper completes or dies.
- OBS returns its unchanged dry buffer.

## Why a global response event is safe

The response event is only a wake hint. Correctness is determined exclusively from the slot state and sequence number. A wake for an older timed-out slot cannot cause stale audio to be copied into a newer callback.

## P0 latency policy

The realtime wait budget is:

`clamp(block_duration * deadline_fraction, 0.5 ms, 10 ms)`

Default `deadline_fraction` is 0.70.

This is deliberately conservative for the spike. P0 measurement data should determine the P1 scheduling policy.
