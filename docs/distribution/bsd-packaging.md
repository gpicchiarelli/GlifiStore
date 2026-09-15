# BSD reference packaging

Status: structural reference + fail-closed native producers; retained tagged evidence pending

`packaging/freebsd/` and `packaging/openbsd/` are upstream reference ports, not evidence that an
official ports tree has accepted GlyphaStore or that a package has passed clean-host installation.
`engineering/tools/validate_bsd_packaging.py` checks their structural invariants and ties product
and shared-library versions back to `VERSION` and `ABI_VERSION`.

The FreeBSD reference uses staged CMake installation, system TLS, `USE_LDCONFIG`, a static plist,
the `rc.subr` framework, `/usr/local/etc/glyphastored.conf`, `/var/db/glyphastore`, and a dedicated
`glyphastore` account. The service is disabled by default, uses `daemon(8)` only as a supervisor,
records the child PID, drops privileges, and forwards normal stop into the daemon's SIGTERM drain.

The OpenBSD reference uses the ports CMake module, base LibreSSL, `SHARED_LIBS` major/minor
authority, WANTLIB, `${TRUEPREFIX}`, `@sample`, `@rcscript`, `/etc/glyphastored.conf`,
`/var/glyphastore`, and `_glyphastore`. The daemon's existing kqueue and pledge/unveil paths remain
native; no TLS implementation is bundled.

The `--release` validator additionally requires explicit account-registration markers. Those
markers do not exist before ports-tree review. A `distinfo` file is deliberately not committed here:
including the digest of the source archive inside that same source archive would be circular. Each
native release producer must generate and retain `distinfo` from the already sealed source archive,
then run the native checksum target before package construction. This prevents structural reference
material from being promoted as a tested package.

The normal FreeBSD and OpenBSD workflows now also install the C ABI into a temporary native prefix,
build external consumers without source include/library paths, execute the CMake and pkg-config ABI
smokes, and retain their VM logs. This raises the portability signal but deliberately does not emit
`freebsd-package-evidence.json` or `openbsd-package-evidence.json`: no native package manager or
service lifecycle was exercised by that installed-prefix test.

The tag-only release graph contains fail-closed FreeBSD and OpenBSD package producers
(`scripts/test-freebsd-package-lifecycle.sh`, `scripts/test-openbsd-package-lifecycle.sh`). Each
consumes the exact sealed source archive, requires the account-registration marker, generates
same-run `distinfo`, builds through the ports framework, and exercises the installed package and
native service lifecycle (`rc.subr` / `rcctl`). They are implementation, not proof: the account
markers and retained tagged runs are still absent. Honest Wave 5 residuals live in
[`wave5-l7-residuals.md`](wave5-l7-residuals.md).

## Running the BSD backends through package-ci

`scripts/package-ci.sh --profile {pr|main|nightly|release} --backend freebsd|openbsd` is the single
entry point. It resolves the release context from `VERSION`, validates the reference port, reads the
account marker, admits an optional `--candidate` sealed directory, and hands those observations to
`engineering/tools/bsd_package_lifecycle.py`, which owns every status decision;
`scripts/lib/package-backend-bsd.sh` is the backend module that probes the host. The native
lifecycle script above is invoked unchanged, and only when a native host, the account marker, an
admitted sealed candidate, a complete ports tree, root privileges and the `full` stage hold
together. Every blocker is retained in `native-prerequisites.log`, and on any other host the native
rows are `BLOCKED` rather than silently missing.

Evidence rows are separated by category so one signal can never be read as another:

| Category | Rows | What a pass means |
| --- | --- | --- |
| `structural` | `structural-metadata`, `reference-port-structure` | the in-repo port matches `VERSION`, `ABI_VERSION` and the service policy |
| `native-build` | `sealed-source-admission`, `package-build` | the sealed candidate was admitted and the ports framework built a package from it |
| `package` | `package-inspect`, `package-install`, `external-consumer`, `package-upgrade`, `config-preservation`, `package-remove` | `pkg`/`pkg_add` installed, inspected, reinstalled or removed the package |
| `service` | `service-lifecycle`, `put-get-erase`, `restart-recovery` | the packaged daemon ran under `rc.subr`/`rcctl` and answered protocol v2 |
| `upstream-accepted` | `ports-account-registration`, `upstream-ports-acceptance` | an upstream project actually allocated the account or accepted the port |

Consequences that hold by construction:

- `upstream-ports-acceptance` is an `OPEN_GATE` until an upstream ports tree accepts the packaging,
  so the backend result is never `PASS`, however far the native lifecycle got. The proven depth is
  carried by `lifecycle_state` instead.
- `ports-account-registration` reads `packaging/{freebsd,openbsd}/PORTS_ACCOUNT_REGISTERED` and
  never writes it. Without the marker the native lifecycle stays `BLOCKED`.
- A native log without its own `PASSED` marker is a `FAIL`, never an inferred pass; after the first
  unproven row the remaining rows are `NOT_RUN`.
- `package-upgrade` is `NOT_APPLICABLE_INITIAL_BASELINE` while no annotated release precedes the
  current one, and `NOT_RUN` once one exists and sealed N−1 packages are not supplied (or the
  native lifecycle did not reach the walk). Upgrade continuity is never inferred from a rebuild;
  FreeBSD/OpenBSD run install→seed→upgrade→verify when `GLYPHASTORE_N1_PACKAGE_DIR` supplies
  sealed predecessor `.pkg`/`.tgz` packages (same contract as Linux deb/rpm).
- `LIFECYCLE_VERIFIED` requires an external consumer built against the installed package
  prefix (`scripts/lib/package-external-consumer.sh`); with that step retained, the native
  ceiling is `LIFECYCLE_VERIFIED` until a sealed N−1 `package-upgrade` PASSes.
  `upstream-ports-acceptance` keeps the overall result `OPEN_GATE`.

Before either artifact enters a manifest, retain native evidence for build/fake or stage,
packing-list and shared-symbol checks, package creation, install, service start/stop, protocol
PUT/GET/ERASE, durable restart/recovery, upgrade/reinstall, deinstall, configuration preservation,
and expected data-directory preservation. Cross-compilation is not accepted.
