# cpkt independent lifecycle

## Preserved

- Public C namespace, facade symbols and ABI majors.
- Owned upstream pins, patches, generators, facade behavior, tests and examples.
- Seven-target build matrix, strict warnings, C89 checks, static/shared package consumers, privacy/relocation gates and source reconstruction.
- Shared checksum cache policy and configured concurrency.

## Changed

- One repository owns one payload and release version. `GROUP=all` means this repository only.
- Archives and checksums use the repository name.
- GitHub workflow and publication use `c89systems/cpkt`.
- Core alone owns and installs both lifecycle skills.

## Verification

Initial migration validates source ownership, inventory/helper closure, Python/shell syntax, Make dispatch and workflow identities before any build. Build/test verification follows only after all three repositories are separated. Core 0.1.0 is the first release; optional provider pins are completed from its verified published assets.
