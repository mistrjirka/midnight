# Cancellation on Canon 600D / 6D

Midnight uses a single DryOS worker for astronomy calculations, SD-card I/O and
autofocus. Magic Lantern calls `CBR_KEYPRESS` from the GUI task, before its
built-in SET + main-dial playback handler.

## Protocol

1. An action is accepted under the worker's existing DryOS semaphore. A new
   nonzero 32-bit request ID is reserved **before** posting the action to the
   worker queue. Only one foreground action can be queued or running.
2. A press of **Q** reads the current active ID and stores it as the requested
   cancellation ID. The GUI callback never waits for the worker lock or
   posts a cancellation message behind the busy worker.
3. On dequeue, the worker takes the semaphore and associates that action with
   its request ID. The worker's check returns true only when the cancellation
   ID matches the request it is currently executing.
4. On completion, the active ID is cleared under the semaphore. A new action
   gets a different ID. Old Q requests do not cancel new work.
5. QUIT blocks further queue posts. Hot unload refuses to proceed while a
   worker task remains active.

Only `active_ticket` and `cancel_ticket` cross between the GUI and worker
without a semaphore. They use aligned **32-bit relaxed atomic load/store**.
No other data is published through these fields. The request counter is
incremented under the semaphore, not with an atomic read-modify-write.

**Why relaxed?** Magic Lantern builds portable modules for ARMv5TE, shared by
the 600D and 6D. On the pinned toolchain, relaxed 32-bit atomics compile to
native `LDR`/`STR`. Acquire/release synchronization introduces the
non-exported `__sync_synchronize` helper, and atomic fetch-add introduces
`__atomic_fetch_add_4`. Use DryOS semaphores for state that needs ordering.

## Where cancellation can take effect

| Operation | Checkpoint / limitation |
| --- | --- |
| Live View settling | Every 100 ms |
| Guiding pause | Every 50 ms |
| Sky solving | Every 8 quadrilaterals or pattern reads; every 16 verification checks |
| AF confirmation and sample waits | Every 25 ms |
| AF lens movement | Before each command, immediately after its confirmation, and before the next step |
| Uninterruptible Canon focus call | One already-issued `lens_focus(1, ...)` command must return safely first |
| Completed gallery or file result | Q hides results before considering generic work cancellation |

The Canon lens API may wait roughly **1 s for the property change**, plus
**1 s for lens completion**, plus Midnight's 120 ms extra delay. Q cannot
safely interrupt that in-flight property call. Cancellation must instead
prevent *subsequent* motor commands. No focus reset/reversal is attempted
after cancellation, since that would move the lens again.

A cancellation requested while foreground work is queued behind a background
lookup remains pending until that foreground work actually starts. Background
work does not see the foreground request's cancellation.

## Tests and remaining limitations

The production worker was compiled with host ASan/UBSan and tested for:
pre-start Q, Q during queue posting, cancelling queued foreground work,
cancellation not leaking to the next request, posting failure, rollover of
the 32-bit ID, and safe QUIT behavior.

The production autofocus core was compiled under ASan/UBSan and tested for
Q before the scan, during waits, while a simulated 2 s Canon command is in
flight, after several steps, and after sensor acquisition. The unchanged
scan is also tested without cancellation.

The real Magic Lantern module is compiled for both 600D and 6D using
`-march=armv5te`, with no new unresolved atomic helper imports.
A Canon 600D QEMU plate-solve smoke test verifies binary compatibility.

**Still needed:** physical-camera Q-button latency measurement during a
real AF lens command, and a long-duration test with other Magic Lantern
modules active. Do not kill the DryOS worker or send unconfirmed autofocus
commands to speed up cancellation.

The v0.4.0-alpha.1 SD-card firmware is unchanged by this source-only patch.
