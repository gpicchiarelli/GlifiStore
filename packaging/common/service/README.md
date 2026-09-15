# Service integration

Shared service definitions used by the Linux packages. Both the Debian and the RPM
backend install the *same* unit, rendered from one template, so the two packages
cannot drift into different runtime behaviour.

| Path | Role |
| --- | --- |
| [`glyphastored.service.in`](glyphastored.service.in) | systemd unit template |
| [`service-account.sh.in`](service-account.sh.in) | shell snippet that creates the `glyphastore` system user and group, inlined into `postinst` (deb) and `%pre` (rpm) |

Both files are rendered by
[`../../../engineering/tools/render_package_metadata.py`](../../../engineering/tools/render_package_metadata.py),
which substitutes the layout tokens (`@BINDIR@`, `@STATEDIR@`, `@CONFIG_FILE@`,
`@SERVICE_USER@`, `@SERVICE_GROUP@`) for the backend being built. Neither file is
installed from this directory directly, and neither may be edited after rendering.

## What the unit commits to

- The daemon runs as the unprivileged `glyphastore` system account, never as root.
- `StateDirectory=glyphastore` gives systemd ownership of the durable directory's
  creation and mode (`0750`), so an install does not have to guess.
- `ProtectSystem=strict` with an explicit `ReadWritePaths` for the state directory:
  the daemon cannot write anywhere else, including its own configuration.
- `TimeoutStopSec=90s` deliberately exceeds the daemon's `shutdown-drain-ms`, so
  systemd does not `SIGKILL` a daemon that is still draining. Shortening it would
  change durable shutdown behaviour and needs an ADR.
- The unit is **installed but not enabled**: `dh_installsystemd --no-enable
  --no-start` on Debian, and no `%systemd_post` enablement on RPM. Installing a
  package never starts a storage daemon on an operator's machine.

## What is proven

Nothing here is certified. The `service-lifecycle`, `put-get-erase` and
`restart-recovery` checks exercise the unit — start, `systemctl is-active`, a
protocol round trip against the *installed* daemon, a restart, and re-read of the
data written before the restart — but only where the lifecycle can actually run:
inside the digest-pinned container or on a disposable root Linux host. Everywhere
else those checks report `BLOCKED` with the reason. No gate references them and
`required_for_release` stays `false` for both Linux backends.

The macOS backends still have no launchd or `brew services` integration; the BSD
reference ports keep their own native scripts:

- FreeBSD rc.subr script: [`../../freebsd/files/glyphastored.in`](../../freebsd/files/glyphastored.in)
- OpenBSD rc.d script: [`../../openbsd/pkg/glyphastored.rc`](../../openbsd/pkg/glyphastored.rc)
