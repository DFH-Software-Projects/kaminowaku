# Packaged NOSIX

Only the redistributable NOSIX ABI (versioned shared libraries, public
headers, ABI metadata, build manifest, and proprietary license) is packaged
here. **Never copy NOSIX implementation source into Kaminowaku.**

Release staging migrates the existing `nosix_abi/` payload into
`libs/nosix/`. Subsequent NOSIX ABI updates should be staged directly
into `libs/nosix/` by the deployment script. Keep both native amd64
builds, their `abi.env` files, and the original NOSIX license.

The application installer recreates the SONAME/linker symlinks from each
platform's `abi.env`, installs only the selected shared library in
`/usr/local/lib/kaminowaku/`, and embeds an executable RPATH to that
private directory. It does not expose or install NOSIX into system-wide
include/library locations.
