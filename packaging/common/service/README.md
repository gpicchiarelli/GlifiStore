# Service integration

Placeholder for the shared service definitions used by packages: a systemd unit for
the Linux backends and, if macOS packaging ever ships a daemon, a launchd job.

**No unit exists yet.** No package installs a service from this directory, so the
`service-lifecycle` check reports `NOT_RUN` for every backend that has no packaging
implementation. The only working service integrations today are the native ones in
the reference ports:

- FreeBSD rc.subr script: [`../../freebsd/files/glyphastored.in`](../../freebsd/files/glyphastored.in)
- OpenBSD rc.d script: [`../../openbsd/pkg/glyphastored.rc`](../../openbsd/pkg/glyphastored.rc)

A unit added here must run the daemon as a dedicated unprivileged service account,
stop it in a way that preserves durable shutdown ordering, and be proven by a
`service-lifecycle` check with a retained log before any gate may reference it.
