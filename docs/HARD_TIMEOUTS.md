# Hard scan deadlines

Kaminowaku uses **elapsed, monotonic time**, not the time since the most
recent packet. Unrelated network traffic and partial replies never restart a
receive timeout.

## Limits

| Scope | Effective limit | Outcome |
| --- | --- | --- |
| One raw receive (NOSIX) | At most 30 seconds; shorter values honored | `NOSIX_TIMEOUT` |
| ICMPv4/v6 or DNS receive | Profile or query timeout, capped at 30 seconds | Unanswered / timeout |
| One TCP or UDP port batch | `rx.timeout_ms`, capped at 30 seconds after transmission | Unanswered ports retired and next batch scheduled |
| One banner collection | Requested timeout, capped at 30 seconds | Existing partial bytes preserved |
| One Book I/O call | Explicit timeout (or profile default), capped by `rx.timeout_ms`, 30 seconds and remaining Book time | Timeout / transport status |
| One Book execution | 120 seconds, including script execution and its network I/O | `LIMIT_ERROR` |

The profile's recommended `rx.timeout_ms = 3000` is unchanged. Setting it
higher than 30000 does not disable the hard safety ceiling. A Book cannot
request an I/O timeout greater than its active profile allows. The Book
runtime checks the session wall-clock deadline while interpreting instructions;
native operations receive only the remaining Book time.

## Retirement and evidence

Each port batch returns at its deadline even on a busy/promiscuous interface.
Unanswered TCP probes remain `FILTERED`; unanswered UDP probes remain
`OPEN_FILTERED`. The existing finalizers write their individual results and
advance to subsequent ports and targets. The project scheduler retires expired
pending transactions and releases their reserved capture memory.

For `rx_exact`, the total timeout covers all partial reads, not each read
separately. Existing partial bytes and their timeout status are returned
according to the Book API. Book PCAPs remain durable even when a Book is
terminated by its execution limit.

Native NOSIX uses nonblocking receive calls following bounded readiness waits
on Linux packet sockets, FreeBSD BPF and local IPv4, as well as nonblocking
TCP/UDP streams. Repeated `EINTR` or `EAGAIN` cannot refresh a deadline.

## Deployment dependency

**Build NOSIX before packaging Kaminowaku.** The NOSIX
`fix/hard-receive-deadlines` branch changes implementation without changing
the public ABI. Kaminowaku's currently committed platform binaries are
separately packaged ABI snapshots; updating Kaminowaku source alone does not
replace those binaries. Build and package the updated native NOSIX shared
library for **Linux and FreeBSD** as part of the deployment workflow.

## Regression checks

- NOSIX: `make test` includes a real-time signal storm that repeatedly
  interrupts a 120ms poll, verifying that the deadline still expires.
- Kaminowaku: `sh tests/run_kwire_deadline.sh` on the [`beta-v2` development branch](https://github.com/DFH-Software-Projects/kaminowaku/tree/beta-v2/tests) verifies expiration, the native ceiling and that repeated checks do not reset the timer.
- `main` uses native Linux/FreeBSD build verification; regression sources and workflows remain on `beta-v2`. Validate actual raw capture and network behavior on both target platforms before merging or shipping updated shared libraries.

These deadlines bound cooperative userspace and normal native socket/BPF
operations. No in-process timeout can guarantee recovery from an uninterruptible
kernel operation, indefinitely blocked filesystem or suspended process; those
failure classes require an external process supervisor.
