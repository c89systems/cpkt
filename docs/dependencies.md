# cpkt dependencies

## Shipped libraries

- openssl (pinned recipe and license: `cmake/components.json`).
- zlib (pinned recipe and license: `cmake/components.json`).
- nghttp2 (pinned recipe and license: `cmake/components.json`).
- libssh2 (pinned recipe and license: `cmake/components.json`).
- curl (pinned recipe and license: `cmake/components.json`).
- libxml2 (pinned recipe and license: `cmake/components.json`).
- lua (pinned recipe and license: `cmake/components.json`).
- mqttc (pinned recipe and license: `cmake/components.json`).
- krb5 (pinned recipe and license: `cmake/components.json`).
- cyrus-sasl (pinned recipe and license: `cmake/components.json`).
- openldap (pinned recipe and license: `cmake/components.json`).
- cmocka (pinned recipe and license: `cmake/components.json`).

## Prerequisites and test tooling

This repository has no other cpkt SDK prerequisite.

`libpslog` is a pinned test-only logging consumer and is not shipped or linked into production facades. CMake, Ninja/Make, Python and host development tools are build/test requirements. Upstream archives, patch series and enabled static/shared features are recorded by the component inventory and recipe. Artifact-local notices accompany each delivered component.
