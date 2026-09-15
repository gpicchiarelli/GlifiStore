# Shared packaging assets

Backend-independent packaging material lives here so that Debian, RPM, MacPorts,
Homebrew and the BSD reference ports describe the same product instead of drifting
apart.

## Current contents

| Path | Role | State |
| --- | --- | --- |
| [`config-data-policy.md`](config-data-policy.md) | Normative intent for configuration vs. data retention across install, upgrade and removal | Resolved for deb/rpm; intent only for the other backends |
| [`file-lists/payload.yaml`](file-lists/payload.yaml) | The one installed-file inventory, componentised by retention policy | Used by the deb and rpm payload checks |
| [`service/`](service/) | systemd unit and service-account snippet, shared by both Linux backends | Rendered and installed; installed disabled, never auto-started |
| [`consumer/`](consumer/) | External consumer sources that link against the *installed* package | Built outside the checkout by the `external-consumer` check |

## Honest state

- The Linux backends render their packaging metadata from the release context and
  can walk the full lifecycle — build, install, external consumer, service, protocol
  round trip, restart recovery, configuration preservation, removal — but only
  inside the digest-pinned container or on a disposable root Linux host. Anywhere
  else those rows report `BLOCKED` with the reason. No CI workflow retains their
  evidence yet, so no gate cites them and `required_for_release` is `false`.
- The macOS templates under [`../macports/`](../macports/) and
  [`../homebrew/`](../homebrew/) still have no launchd or `brew services`
  integration: their `service-lifecycle` check is an open gate. The FreeBSD and
  OpenBSD reference ports under [`../freebsd/`](../freebsd/) and
  [`../openbsd/`](../openbsd/) are not accepted by an upstream ports tree.
- Package versions are never written by hand. They are derived from `VERSION` and the
  package revision by `engineering/tools/semver_policy.py` and recorded in the release
  context; see the mapping table below. The metadata renderer refuses to read a
  template that contains a literal product version.

## Version mapping

`package_revision` in the release context is packaging-only: it changes when the
package changes without a product change. It defaults to `0`.

| Backend | Version fields | `0.1.0`, revision `0` |
| --- | --- | --- |
| Debian | `upstream_version`, `debian_revision` | `0.1.0-1` |
| RPM | `version`, `release` | `0.1.0-1` |
| MacPorts | `version`, `revision` | `0.1.0_0` |
| Homebrew | `version`, `revision` | `0.1.0` |
| FreeBSD | `DISTVERSION`, `PORTREVISION` | `0.1.0` |
| OpenBSD | `V`, `REVISION` | `0.1.0` |

Debian and RPM map a prerelease with `~` so that it sorts before the final release.
MacPorts, Homebrew and the BSD ports carry documented prerelease ordering limitations
in the release context instead of an unproven ordering claim.
