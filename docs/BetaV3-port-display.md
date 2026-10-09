# BetaV3: Port display policy

The displayed port table is a **filtered view** of the existing `.ports` history, not a change to the persisted scan results.

- Keep current `OPEN` ports (TCP and UDP), even if banner enrichment returned no application bytes.
- Hide `CLOSED`, `FILTERED`, and `OPEN|FILTERED` entries for both TCP and UDP.
- Keep `ERROR` rows for diagnostic visibility.
- A TCP `SYN+ACK` is positive TCP reachability evidence. When a later scan reports `CLOSED`, `FILTERED`, or `ERROR`, retain the **last confirmed** SYN+ACK observation in the table with state `OPEN (prior)`; its timestamp and RX byte count refer to that older observation. Do **not** present it as a current open port.
- A TCP `RST` is not a successful connection, and therefore does not qualify for historical preservation.
- Default table truncation still caps ordinary/noisy observations, but does not truncate entries supported by TCP `SYN+ACK` evidence.
- The filters apply to target and project port displays, including detailed and observed modes. Explicit `-p` host selection continues to require a *currently* open TCP port according to its existing selector; it is not redefined by historical display evidence.

TCP SYN scanning observes the `SYN+ACK` response, **not necessarily completion of the three-way handshake**. Successful application/banner connects are attempted in the enrichment stage for ports that were found open by the scan. No new TCP handshake probes were added.

The `tests/betav3_port_display.c` policy tests and the BetaV3 compiler checks guard this behavior. No target file schema or `.ports` storage changes are required.
