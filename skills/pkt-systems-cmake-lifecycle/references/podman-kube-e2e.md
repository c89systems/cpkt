# Podman Kube Local Services

Use rootless Podman and `podman kube play` for deterministic local services when in-process fixtures are insufficient. This is the lifecycle target for new repositories and for migration of existing containerd/Compose service workflows. Do not introduce Docker Compose, `nerdctl`, or a fallback between container engines.

## Repository contract

```text
devenv.yaml.in                 # tracked Podman Kube manifest template
devenv/                        # tracked, static service configuration when needed
scripts/devenv.sh              # Make-facing up/down/ps/logs/reset/render entry point
scripts/test-e2e.sh
build/devenv/devenv.yaml        # rendered manifest; ignored
build/devenv/state/             # bind-mounted service data and caches; ignored
build/devenv/tmp/               # test work and temporary data; ignored
build/devenv/logs/              # captured diagnostics; ignored
build/devenv/credentials/       # generated local credentials; ignored
```

A repository with no host-specific paths, names, or ports may track a directly playable root `devenv.yaml` instead. Otherwise render `devenv.yaml.in` to `build/devenv/devenv.yaml` with a small, deterministic renderer. The checked-in manifest must declare every container used by local integration tests, including bootstrap/init containers and optional local inspector tools. Use supported Kubernetes `Pod`, `initContainers`, `ConfigMap`, `Secret`, or `Job` objects as appropriate; avoid a second container definition in shell scripts. Keep project-owned data under `build/devenv/`, which is covered by the repository's `/build/` ignore rule and excluded from source and release artifacts.

Render only known placeholders for the canonical repository root, `build/devenv/` paths, a stable checkout-specific resource name, and project-prefixed host-port variables. Use absolute `hostPath` paths in the rendered manifest; never commit machine paths. Give each checkout distinct pod/resource names and high, non-privileged host ports so parallel checkouts can coexist. Bind published ports to loopback unless an external client is part of the test contract. Write the rendered manifest with mode `0600` if it contains generated secrets. Keep it intact until `podman kube down` has used it; `dev-reset` tears down first, then removes `build/devenv/`.

Use pinned image versions or digests, explicit `restartPolicy`, read-only static config mounts, and idempotent one-shot initialization. Put tightly coupled services in a pod when they can communicate over pod-local `localhost`; use explicit endpoints between pods. Do not assume Kubernetes `Service` resources or Compose DNS are available. Avoid privileged containers, host networking, broad host mounts, fixed global resource names, and real credentials unless the engineer approves the requirement. Generate local credentials only under `build/devenv/credentials/` with restrictive permissions. Do not use Podman named volumes for project-owned caches, databases, or other mutable data that the repository's reset command must remove.

## Ownership and deletion gate

Run Podman rootlessly as the developer, never with `sudo`. A rootless invocation alone does not prove that files written into a bind mount can be removed by the host user: an image's effective UID and the pod's user namespace determine host ownership. For each image that writes under `build/devenv/`, choose a working user namespace and `securityContext.runAsUser`/`runAsGroup` combination, such as `keep-id` or `keep-id:uid=<service-uid>,gid=<service-gid>`, that maps its effective writer to the invoking host user. Separate services into pods when their required UID mappings differ. Verify each image still starts and can write its expected directories. Do not use recursive ownership-changing mounts, `sudo rm`, or cleanup-time `chown` to hide a bad mapping.

The observable gate is: start the service, make it write representative cache/state/temp files, stop it with `podman kube down`, and remove the affected `build/devenv/` directory as the same unprivileged user. `dev-reset` must pass this gate without permission errors; a clean `podman kube down` alone is insufficient. If an image cannot satisfy both startup and host deletion, adapt its image/user mapping or stop the migration and report that constraint. Keep static tracked configuration outside the reset root.

## Command behavior

`scripts/devenv.sh` resolves the repository root independently of the caller's working directory and is the only Make-facing Podman entry point. It must fail clearly if rootless Podman is unavailable. `dev-up` renders the manifest, creates state directories, runs `podman kube play` detached, runs bootstrap work, polls real service readiness, and prints effective endpoints, credential locations, and the state root. Do not treat pod creation, Kubernetes `readinessProbe`, or container start order as readiness: Podman Kube does not support every Kubernetes readiness field, so use explicit HTTP/TCP/socket/command probes and bounded timeouts.

`dev-down` runs `podman kube down` against the same rendered manifest. `dev-ps` and `dev-logs` use the checkout-specific pod names with `podman pod ps` and `podman pod logs` (or container logs where needed). `dev-reset` invokes `dev-down` before deleting `build/devenv/`, preserving tracked `devenv/` config. Do not use `podman kube play --wait` for detached services; it runs in the foreground and removes created objects on exit. Record the effective image names, pod names, ports, and manifest path in diagnostics. [Podman Kube reference](https://docs.podman.io/en/latest/markdown/podman-kube-play.1.html).

`test-e2e` builds or verifies the debug/e2e preset, starts or checks services, keeps its own temporary work under `build/devenv/tmp/`, starts any local example daemons, waits for readiness, exercises success and expected-failure paths, and traps cleanup of child processes and services that it started. Support a documented project-prefixed keep flag such as `<P>_E2E_KEEP_DEVSERVICES=1`. On failure print the failed command, endpoint, relevant local logs, `podman pod ps` state, and `podman pod logs` for involved pods. Project-prefixed port overrides must be honored and shown by `make help` or `dev-up`.

Real cloud services and credentials stay under explicit opt-in `test-integration` gates. Optional containerized inspectors or upstream CLIs may be opt-in e2e gates, but declare their containers in the same source manifest and render their documents only when the project-prefixed opt-in is set. Give them the same generated-state and cleanup contract. Include deterministic, bounded local e2e in `test-all` or release gates when appropriate; otherwise expose `make test-e2e` as an explicit gate.

## Migration from Compose

When migrating an existing repository, inventory each Compose service, init job, port, bind mount, named volume, network assumption, readiness check, and test dependency. Move the full local service graph to the Podman Kube manifest, preserving observable test behavior and the public Make targets. Replace the Compose wrapper and remove the old `docker-compose.yaml` only after `dev-up`, `test-e2e`, `dev-down`, and unprivileged `dev-reset` pass. Do not leave both orchestration paths in the completed lifecycle. Migrate repositories as focused changes; this reference does not authorize changing unrelated repositories during ordinary work.
