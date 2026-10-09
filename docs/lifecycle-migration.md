# Lifecycle cutover

Ordinary build and test commands now select native Debug. Release matrices are
explicit. The core repository owns its twelve declared components; dependency
producers are shared by matching consumers.

The implementation uses native CMake, ExternalProject and CTest dependencies.
The former operation brokers, evidence receipts, preset adapters, Python test
drivers and their controller fixtures are removed. Tests use compiled C programs
or small CMake/Bash cases. Python remains for actual source/data generation and
small local response-data fixtures.

Verification must follow the developer's current execution envelope. During the
implementation-only cutover, builds and tests are deferred; `make format` is the
required exception. Earlier test results do not certify the revised repository.
After cutover, prove failures in isolation before one affected broader rerun.

The shared lifecycle skill is authoritative for scope and simplicity.
See [build lifecycle](build-lifecycle.md) for the supported commands.
