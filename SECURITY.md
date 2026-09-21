# Security Policy

Kaminowaku is a low-level network enumeration framework. Security reports involving memory safety, sandbox escape, unsafe file handling, command execution, privilege boundaries, evidence integrity, or network-runtime behavior are taken seriously.

This policy covers vulnerabilities and security-relevant behavior affecting Kaminowaku. It does not grant authorization to test third-party systems.

## Supported versions

Kaminowaku is currently pre-1.0.

| Version | Security support |
| --- | --- |
| Current `main` / latest 0.1.x release | Supported |
| Older development snapshots | Not actively supported |

Security fixes may be made against `main` first and included in the next release.

## What to report

Examples of relevant security issues include:

- memory corruption or memory-safety failures;
- unintended arbitrary code execution;
- Book sandbox escape;
- unauthorized access to the private `_kami` native interface;
- module resolver path traversal or symlink escape;
- unsafe tool-registration or execution behavior;
- privilege-boundary problems;
- unsafe temporary-file or artifact handling;
- corruption or unintended disclosure of project/target evidence;
- malformed input causing materially unsafe behavior;
- capture or persistence behavior that can falsely represent successful evidence;
- unexpected behavior at an external library or runtime boundary used by Kaminowaku.

If you are unsure whether a problem originates in Kaminowaku or one of its dependencies, report it to the Kaminowaku project first. The maintainer can triage the boundary and determine where the fix belongs.

## What is not a Kaminowaku vulnerability

The following are generally not security vulnerabilities in Kaminowaku:

- expected packet generation during an authorized scan;
- a remote service reacting badly to ordinary authorized probing;
- vulnerabilities discovered in a scanned target;
- behavior caused by running a deliberately malicious external tool registered by the operator;
- loss of data caused by intentionally running the documented destructive uninstall process;
- unsupported behavior from modified builds that bypass documented safety boundaries.

Non-sensitive bugs should be reported through the Kaminowaku issue tracker.

## Reporting a vulnerability

Do **not** open a public issue containing an unpatched vulnerability, sensitive reproduction details, or a working exploit against Kaminowaku.

Use GitHub's private vulnerability-reporting mechanism for the Kaminowaku repository when it is available. If private reporting is not available, contact the maintainer privately through the project owner's GitHub presence before public disclosure.

For non-sensitive security-adjacent bugs or behavior that does not expose an unpatched vulnerability, use the normal Kaminowaku issue tracker.

A useful report should include:

- affected Kaminowaku version or commit;
- operating system and architecture;
- build mode where relevant;
- affected command, Book, or subsystem;
- clear reproduction steps;
- expected behavior;
- observed behavior;
- security impact;
- crash output, sanitizer trace, or debugger information when applicable;
- a minimal reproducer where practical.

Please avoid including unrelated sensitive target data in the report.

## Coordinated disclosure

Please allow reasonable time for the issue to be reproduced, fixed, tested, and released before public disclosure.

The maintainer may ask for clarification, additional reproduction information, or validation of a proposed fix.

Once a fix is available, public disclosure and credit can be coordinated with the reporter where appropriate.

## Safe research expectations

Testing should be limited to systems and environments you own or are explicitly authorized to assess.

When investigating Kaminowaku itself, prefer:

- local test networks;
- disposable virtual machines;
- controlled target services;
- sanitizer-enabled debug builds;
- synthetic Book/parser/runtime inputs.

Do not use a suspected Kaminowaku flaw as a reason to test unrelated third-party infrastructure.

## Integration and dependency issues

Kaminowaku relies on external libraries and platform facilities, but users should not need to determine ownership before reporting a problem.

If a suspected security issue is observable through Kaminowaku, report it to Kaminowaku first. This includes issues that appear to cross a library, operating-system, capture, TLS, or runtime boundary.

The maintainer will determine whether the problem belongs in Kaminowaku itself, in its integration code, or should be coordinated with another project.

## Evidence preservation

If a security bug affects project data, PCAPs, Book output, or logs, preserve the minimum evidence needed to reproduce the issue before modifying the affected datastore.

Do not publish captured credentials, private traffic, customer data, or other sensitive evidence in a public issue.
