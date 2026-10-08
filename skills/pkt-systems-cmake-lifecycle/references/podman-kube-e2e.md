# Declared local service tests

Use rootless Podman Kube when in-process fixtures cannot satisfy selected integration
tests. No new service tier is implied by this skill. Preserve declared product behavior
during a separately authorized migration from Compose or another engine.

One tracked manifest/template declares the service graph and pinned images. Render
only known project placeholders to ignored build/devenv paths; tracked source contains
no local paths/secrets. Use checkout-specific resource names and loopback ports.
Mutable bind mounts and generated credentials stay in owned build state.

Choose user mappings that let the same host user write and delete state. Prove
service startup, representative writes, teardown and unprivileged reset. Do not
hide a bad mapping with privileged cleanup, recursive chown or ownership-changing mounts.

Expose simple up/down/status/log/reset targets as needed. Keep the rendered manifest
until teardown uses it. Readiness is a real bounded protocol probe, not fixed sleeps
or assumed container order. Tests stop resources they started and report useful logs.
The caller owns whole-job cancellation; scripts do not become service/process brokers.

Live external providers and credentials are explicit opt-in. A lifecycle-only
migration cannot introduce new services, privileges or optional inspectors.
