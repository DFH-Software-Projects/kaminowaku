# Contributing to Kaminowaku

Thanks for taking an interest in extending Kaminowaku.

Community contributions are currently centered on **Lua Books and reusable Lua protocol modules**. This keeps the contribution process lightweight for Book authors while allowing the core application to remain a curated, maintainer-controlled codebase.

If you have an idea for a new enumeration workflow, protocol parser, or service-specific Book, the contribution path below is the best way to develop it independently and make it available for review.

## Contribution model

Develop your Book or module in **your own GitHub repository**.

A typical workflow is:

1. create a repository for your Book or related collection of Books/modules;
2. develop and test it against a current Kaminowaku installation;
3. link back to the main Kaminowaku project;
4. document what the Book does and how it was tested;
5. submit the repository for maintainer review.

Main project:

https://github.com/DFH-Software-Projects/kaminowaku

Keeping contributions in their own repositories gives authors room to iterate, version, document, and share their work without coupling development directly to the Kaminowaku core tree.

If a contribution is a good fit for the official distribution, the maintainer may review, test, adapt where necessary, and incorporate it into the main Kaminowaku repository.

Official inclusion is determined through maintainer review so the bundled Books remain consistent with Kaminowaku's behavior, evidence model, and release quality.

## Contribution scope

The community contribution path currently welcomes:

- executable Kaminowaku Books;
- reusable Lua protocol modules used by Books;
- supporting documentation and examples for those Books/modules.

A typical repository might look like:

```text
your-repository/
├── README.md
├── LICENSE
├── <book-name>.lua
└── modules/
    └── <optional-module>.lua
```

A repository may contain one Book or a related collection. Each Book should remain focused enough that its behavior, network activity, evidence, and output are straightforward to understand and review.

## Core project changes

The core Kaminowaku implementation is maintained directly by the project maintainer. This includes areas such as:

- Kaminowaku C source;
- command and context behavior;
- target/project persistence;
- the Book parser/runtime;
- the private native Book ABI;
- trusted `kami.*` modules;
- core networking integration;
- build/install/uninstall behavior;
- bundled release configuration;
- core UI behavior.

Bug reports, design feedback, and well-scoped issue reports in these areas are welcome. The community contribution workflow itself is simply focused on Books and reusable Lua modules rather than direct core-code submissions.

## Repository documentation

A submission repository should include a README covering:

- the Book name;
- what it enumerates;
- expected target or service conditions;
- ports or transports used;
- whether TLS is used;
- expected output fields;
- tested Kaminowaku version or commit;
- tested operating system(s);
- known limitations;
- example invocation;
- example output when practical.

Please link back to the main Kaminowaku repository:

https://github.com/DFH-Software-Projects/kaminowaku

It is also useful to link directly to the authoritative Book language reference:

https://github.com/DFH-Software-Projects/kaminowaku/blob/main/docs/BOOK_LANGUAGE_V1.md

## Licensing

Please include a clear license with your repository so the maintainer can review, test, modify, redistribute, and—if accepted—include the submitted code in Kaminowaku.

The main Kaminowaku repository uses the BSD 3-Clause License. Contributions intended for official inclusion should therefore use the BSD 3-Clause License or another license that is clearly compatible with redistribution in Kaminowaku.

Only submit code that you have the right to license and redistribute.

## Book authoring requirements

Submitted Books should follow the current Book language contract in:

[`docs/BOOK_LANGUAGE_V1.md`](docs/BOOK_LANGUAGE_V1.md)

An executable Book should:

- use the `.lua` extension;
- declare exactly one matching `book { name = "..." }`;
- use supported Lua syntax and standard-library functions;
- use public `kami.*` modules rather than private `_kami`;
- keep network reads, parsing, and loops bounded;
- use managed Kaminowaku transport rather than creating a separate socket path;
- use `result.emit()` for durable findings;
- use `result.bail()` when valid current-state results cannot be produced;
- leave Kaminowaku `.out` and PCAP persistence to the runtime;
- rely on `kami.send` and `kami.recv` for the standard TX/RX notices rather than duplicating them.

Reusable modules should:

- return a stable module interface;
- keep reusable protocol logic outside the executable Book where practical;
- return structured values and explicit errors;
- leave final durable-output selection to the calling Book.

## Scope expectations

Kaminowaku is designed for network enumeration and evidence collection.

Good community Book ideas include:

- protocol negotiation;
- service enumeration;
- banner and metadata collection;
- safe capability discovery;
- configuration observation;
- bounded request/response analysis.

Books intended primarily for exploitation, credential attacks, persistence, destructive behavior, malware delivery, or unauthorized access are outside the scope of the official project.

All submissions should remain consistent with the project's [license and Acceptable Use Policy](LICENSE.txt).

## Testing expectations

Before submitting a repository for review, please:

- run the Book through the normal `book <name>` path;
- test both successful execution and expected failure/bail behavior;
- verify that networked Books produce usable PCAP evidence;
- verify that a failed or bailed run preserves previous successful `.out` data;
- confirm that durable output is intentional and readable;
- confirm that the Book/module runs without an external Lua interpreter;
- test IPv4 and IPv6 when the protocol and Book are intended to support both;
- document platform-specific behavior where applicable;
- keep reads and protocol lengths bounded.

For service-specific Books, testing against at least one known-good implementation is strongly encouraged. Documenting what you exercised makes review much easier.

## Style expectations

The shipped Books and modules are the best style reference.

In general, prefer:

- small, named helper functions;
- explicit bounds;
- clear error strings;
- protocol constants near the top of the module;
- structured return tables;
- uppercase durable result keys;
- lowercase kebab-case Book names;
- comments that explain protocol or safety decisions rather than restating obvious code.

If the supported Book runtime already provides the primitive you need, prefer using it over adding an external dependency.

## Submitting for review

When your repository is ready, open an issue on the main Kaminowaku repository with a concise title such as:

```text
Book submission: <book-name>
```

Include:

- repository URL;
- Book name;
- one-paragraph purpose;
- services, ports, and protocols exercised;
- tested Kaminowaku version or commit;
- tested platform(s);
- example result output;
- known limitations, if any.

There is no need to paste a large Book implementation into the issue. Your repository is the review source.

During review, the maintainer may suggest changes or additional testing. If the Book is accepted for the official distribution, it may then be incorporated into the main repository.

## Security-sensitive findings

If Book development uncovers a vulnerability in Kaminowaku itself, please keep the details out of the public contribution repository and submission issue until the issue has been addressed.

Follow [`SECURITY.md`](SECURITY.md) for the appropriate reporting path.

## Contributor attribution

When a community Book is incorporated into the official repository, contributor attribution should be preserved where practical through source authorship notices and project history.

Inclusion in the official distribution applies to the accepted Book/module itself and does not imply endorsement of unrelated content in an external contributor repository.
