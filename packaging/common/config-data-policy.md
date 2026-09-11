# Configuration and data retention policy (draft)

Intent shared by every packaging backend. Nothing here is proven yet: the
`package-remove` and `package-upgrade` checks in
[`../../engineering/distribution/package-matrix.yaml`](../../engineering/distribution/package-matrix.yaml)
are `NOT_RUN` until the backend that owns them executes and retains a log.

## Categories

| Category | Example | Install | Upgrade | Remove | Purge |
| --- | --- | --- | --- | --- | --- |
| Program files | `bin/glyphastored`, `lib/libglyphastore.so.*` | installed | replaced | removed | removed |
| Configuration | `glyphastored.conf` | installed as a sample; an existing file is never overwritten | operator edits preserved | preserved | intended to be removed, per-backend decision open |
| Durable data | Store directory (`/var/db/glyphastore`, `/var/glyphastore`) | created, owned by the service account | untouched | preserved | never removed by a package action |
| Logs and runtime state | pid files, sockets | created at runtime | untouched | removed if empty | removed if empty |

## Rules

1. A package action must never destroy durable Store data. Data removal is an
   explicit operator action, not a side effect of `remove` or `purge`.
2. Configuration modified by the operator survives reinstall and upgrade. Backends
   express this with their native mechanism (`@sample` on the BSD ports,
   `conffiles` on Debian, `%config(noreplace)` on RPM).
3. The service account owns the data directory; packages must not widen its
   permissions.
4. Purge semantics differ per packaging system and are deliberately left open until
   a backend implements and proves them.

## Open questions

- Whether `apt purge` should remove configuration while leaving data (Wave B).
- Whether a service must be stopped by the package or by the operator before removal,
  and how that interacts with durable shutdown (Wave B).
