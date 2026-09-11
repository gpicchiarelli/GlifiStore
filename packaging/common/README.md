# Shared packaging assets

Backend-independent packaging material lives here so that Debian, RPM, MacPorts,
Homebrew and the BSD reference ports describe the same product instead of drifting
apart. The directory is scaffolding introduced with the Wave A packaging framework:
it holds policy, not yet artifacts.

## Current contents

| Path | Role | State |
| --- | --- | --- |
| [`config-data-policy.md`](config-data-policy.md) | Normative intent for configuration vs. data retention across install, upgrade and removal | Draft; not proven by any backend yet |
| [`file-lists/`](file-lists/) | Home for the shared installed-file inventory used by package payload checks | Empty placeholder |
| [`service/`](service/) | Home for service units (systemd, launchd) | Empty placeholder; no unit exists |

## Honest state

- No systemd unit, launchd job or file list exists yet. Nothing in this directory is
  installed by any package, and no packaging check may report PASS on their behalf.
- The only backends with real in-repo packaging today are the FreeBSD and OpenBSD
  reference ports under [`../freebsd/`](../freebsd/) and [`../openbsd/`](../openbsd/).
- Package versions are never written by hand. They are derived from `VERSION` and the
  package revision by `engineering/tools/semver_policy.py` and recorded in the release
  context; see the mapping table below.

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
