# Offline durable Store migration (Worker reshard)

Status: implemented for stopped Stores (logical live-key copy)
Applies to: durable persistence v1
Owner: persistence maintainers
Last reviewed: 2026-09-11

## Contract

Worker-count changes are **offline** operations. Stop `glifistored` / close every Store on the
source directory. Migration never mutates the source. It creates or resumes a **destination**
durable Store with the target Worker count and copies every live visible key (value + expiry).

Procedure:

1. Exclusive-lock and verify the source (`verify_durable_store`).
2. Open the source Store (`open_existing`).
3. Snapshot live Index keys under Worker locks; sort lexicographically for deterministic resume.
4. Create the destination (`create_new`) or resume an interrupted destination (`open_existing`) when
   a matching sibling checkpoint exists.
5. For each remaining key: `get` from source, `put` into destination (preserving `expire_at_ns`).
6. Persist a sibling checkpoint after each successful put (`<destination>.migrate-state`).
7. Flush and close both Stores; verify the destination; delete the checkpoint on success.

Live/hot migration and online resharding are unsupported ([ADR 0024](../adr/0024-offline-worker-migration.md)).

## Checkpoint and resume

```text
<source-data-dir>          # unchanged
<destination-data-dir>/    # new Store catalog
<destination-data-dir>.migrate-state
```

Resume requires the checkpoint's `source_store_id`, `source_worker_count`, and
`target_worker_count` to match the current invocation. Keys before `last_key_hex` are skipped only
after their destination values and expiries have been matched against the source; the last key may
be rewritten idempotently. Existing destination keys must be a subset of the source snapshot, so a
matching checkpoint cannot authorize unrelated destination contents.

The version-1 checkpoint schema is strict: `source_store_id`, `source_worker_count`,
`target_worker_count`, `keys_copied`, `last_key_hex`, and `phase=copying` must each occur exactly
once. Store IDs use canonical lowercase hexadecimal, Worker counts must be within the supported
range, unknown or empty lines are rejected, and the complete file is bounded to twice the maximum
record size plus 1 KiB of metadata. Missing, duplicated, malformed, oversized, or mismatched
checkpoint state is refused before the destination Store is opened.

A destination directory without a matching checkpoint is also refused (fail closed).

## What is and is not preserved

| Preserved | Not preserved |
|---|---|
| Live key bytes | Segment files / physical layout |
| Live value bytes | Per-Worker sequences |
| Absolute expiry (`expire_at_ns`) | Source `store_id` / `routing_epoch` |
| Logical visibility (Index live set) | Tombstone history / sealed dead bytes |

## Tooling

```bash
glifistore_migrate_store [--json] [--no-scan] --workers N -- /path/to/source /path/to/destination
```

## Explicit non-goals

- In-place Worker-count rewrite
- Online / dual-ownership reshard
- Preserving sequences or Store identity
- Copying crash temporaries or compaction intents
