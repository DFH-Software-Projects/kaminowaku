# Licensed NOSIX ABI

Kaminowaku 0.3.1 consumes the proprietary NOSIX **binary ABI**. The
private NOSIX source is not included in this public repository. Both
shipped platforms target amd64/x86-64.

The checked-in public headers `include/nosix.h`, `nosix_poll.h`, and
`nosix_datagram.h` declare ABI **1.5**. Both `linux/` and `freebsd/`
contain platform-specific `abi.env` metadata and the matching
`lib/libnosix.so.1.5.0` runtime library (SONAME `libnosix.so.1`).
The root `BUILD-MANIFEST.txt` records source provenance, checksums,
and native platform builds. Keep binaries, matching metadata, and
`LICENSE` together when distributing an ABI package.

The installer places NOSIX in the private `PREFIX/lib/kaminowaku/`
library directory. Staging recreates SONAME and linker symlinks,
including after Windows-hosted release assembly. Do not substitute
renamed older ABI 1.4 builds or publish NOSIX private source.

Historical BetaV3 staging notes are archived under `docs/BetaV3-*`
and do not describe the current 0.3.1 dependency package.
