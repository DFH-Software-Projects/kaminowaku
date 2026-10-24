# Beta 0.3.1 — Live Book argument acceptance

**Status:** Ready for operator acceptance on controlled lab VMs. Source compilation and Lua integration are already covered by CI; these instructions exercise real NOSIX traffic, capture, and persistence, which are not yet demonstrated by CI.

## Topology and prerequisites

Use two machines on an isolated test network. The **fixture VM** runs Python 3 and serves a bounded SSH identification/KEXINIT exchange on TCP/2222, a plain HTTP response on TCP/8080, and a synthetic GitLab-like sign-in response on TCP/8081. The **scanner VM** runs Kaminowaku from `beta-0.3.1` with its NOSIX interface/profile configured to reach the fixture's IPv4 address. Only test assets you own or are authorized to assess.

The fixture binds only to `127.0.0.1` by default. For two VMs, bind to the fixture's private IPv4 address or `0.0.0.0` **on the isolated lab interface only**. Ensure the VM firewall permits 2222, 8080 and 8081 from the scanner. A second VM is preferred to a loopback-only scanner setup: L2/NOSIX packet capture on loopback may have different behavior.

## A. Start the fixture

On the fixture VM, from the branch checkout:

```sh
python3 scripts/beta031_book_lab.py serve --bind 0.0.0.0
```

The fixture logs incoming requests to stdout and stops with Ctrl-C. You can change its three ports using `--ssh-port`, `--web-port`, and `--gitlab-port`. This is a non-exploiting, synthetic responder; its claimed GitLab version is not evidence about any installed GitLab product.

Independent fixture and verifier tests can be run without Kaminowaku:

```sh
python3 -m unittest discover -s tests -p 'test_beta031_book_lab.py' -v
```

These unit tests do **not** establish end-to-end Book network functionality.

## B. Invoke the three shipped Books

On the scanner VM, build/install the beta branch as appropriate for that VM, start Kaminowaku as root, and confirm the active project's NOSIX network runtime is online. Use a profile whose interface and receiver timeout are suitable for the private test network.

Within the Kaminowaku console:

```text
new beta031lab
targets add -4 <FIXTURE_IPV4>
book ssh-kex --port 2222
book web-enum --port 8080 --tls false --path /status
book cve-2026-110264 --port 8081 --tls false --path /users/sign_in --timeout 1000
```

Replace `<FIXTURE_IPV4>` with the reachable lab address. The CVE Book's `--timeout` is in milliseconds; it **must not exceed** the active profile/native receive maximum. Without `--timeout`, the Book uses the profile/native default (it does not force 4,000 ms). If the profile maximum is below 1000 ms, reduce the argument or explicitly configure a suitable lab profile.

To confirm the same argument handling in target context, run:

```text
targets -4 <FIXTURE_IPV4>
book web-enum --port=8080 --tls=false --path=/status
back
```

The fixture should log one SSH exchange and HTTP requests to `/status` and `/users/sign_in`. Every executed Book must finish successfully. The fixture advertises GitLab 19.5.0 strictly to exercise public release-string classification.

## C. Inspect captured traffic and saved output

Find the TID directory under `/root/.kaminowaku/projects/beta031lab/`. From the scanner's checkout, run:

```sh
python3 scripts/beta031_book_lab.py verify \
  --target-dir /root/.kaminowaku/projects/beta031lab/<TID>
```

The verifier reads the real `KAMINOWAKU_BOOK_OUT` format, decodes hex field values, checks the expected port and service response for all three Books, and validates **at least one captured TCP packet with the requested port** in each latest matching PCAP (Ethernet/VLAN or raw IPv4). It rejects incomplete PCAP records, bad headers, missing files, and mismatched result fields. Change its `--ssh-port`, `--web-port`, and `--gitlab-port` options if the fixture used non-default ports.

Pass criteria:

| Book | Required evidence |
| --- | --- |
| `ssh-kex` | `PORT=2222`, `SSH-2.0-KamiLab_031` banner, valid populated PCAP |
| `web-enum` | `PORT=8080`, plain HTTP, status 200, valid populated PCAP |
| `cve-2026-110264` | `PORT=8081`, synthetic GitLab fingerprint, expected affected-release label, valid populated PCAP |

A completed Book without packet evidence must **not** count as a passed acceptance test.

## D. BAIL and malformed-command regression

After a successful `web-enum` run, snapshot its prior durable output and PCAP files:

```sh
python3 scripts/beta031_book_lab.py snapshot \
  --target-dir /root/.kaminowaku/projects/beta031lab/<TID> \
  --book web-enum --file /tmp/beta031-web-snapshot.json
```

In the Kaminowaku console, force **Book-level validation failure**, not a syntax/parser failure:

```text
book web-enum --port 70000 --tls false
```

The CLI accepts that named argument, the Lua Book rejects the invalid port before opening a TCP connection, and the session must preserve the previous `web-enum.out` while retaining the new PCAP. Verify:

```sh
python3 scripts/beta031_book_lab.py verify-bail \
  --target-dir /root/.kaminowaku/projects/beta031lab/<TID> \
  --file /tmp/beta031-web-snapshot.json
```

A header-only PCAP is acceptable **for this deliberately early BAIL**, not for a successful networked Book.

Finally, test invalid **CLI syntax**. The following must be rejected *before* session creation; verify that the count of new PCAPs does not change:

```text
book web-enum --port
book web-enum --port 8080 --port 8081
book web-enum 8080
```

## Acceptance matrix

| Gate | Pass criterion |
| --- | --- |
| Native parser and Lua argument path | Branch CI is green |
| Real network custom ports | All three fixtures receive requests on their assigned non-default ports |
| Target and project contexts | Both produce correct Book output |
| Durable results | `.out` fields correctly reflect invoked options and fixture responses |
| Forensic evidence | Real, structurally valid, nonempty PCAPs for successful networked sessions |
| BAIL | Existing `.out` unchanged; new PCAP retained |
| Invalid CLI | Rejects malformed names/values without starting a session or PCAP |

If this lab fails, check NOSIX state, routing, gateway, interface, profile receive timeout, firewall, and packet capture. Do not weaken the packet-evidence expectation just to make the test pass.

**Release gate:** Live acceptance is not considered passed until these commands have run on the actual lab network and the verifier has reported success.
