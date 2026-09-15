# Manual pages

GlifiStore ships portable [mdoc(7)](https://man.openbsd.org/mdoc.7) sources for
every binary installed by the Runtime component.

| Page | Section | Binary |
|---|---|---|
| `glifistore.7` | 7 | overview |
| `glifistored.8` | 8 | `glifistored` |
| `glifistore_demo.1` | 1 | `glifistore_demo` |
| `glifistore_inspect_segment.1` | 1 | `glifistore_inspect_segment` |
| `glifistore_verify_store.1` | 1 | `glifistore_verify_store` |
| `glifistore_backup_store.1` | 1 | `glifistore_backup_store` |
| `glifistore_migrate_store.1` | 1 | `glifistore_migrate_store` |
| `glifistore_repair_store.1` | 1 | `glifistore_repair_store` |
| `glifistore_rebuild_index.1` | 1 | `glifistore_rebuild_index` |

## Build and install

```bash
cmake -S . -B build -DGLIFISTORE_MAN_DATE="August 1, 2026"
cmake --build build --target glifistore_manpages
cmake --install build --component Runtime
```

Pages land under `${CMAKE_INSTALL_MANDIR}/man{1,7,8}` (from `GNUInstallDirs`),
typically `share/man` on Linux/BSD and Homebrew prefixes on macOS.

Optional gzip at install time:

```bash
cmake -S . -B build -DGLIFISTORE_COMPRESS_MANPAGES=ON
```

Default is uncompressed so installs do not require `gzip`; packaging may compress
afterward.

## Validation

```bash
./scripts/validate-manpages.sh
# or against a build tree:
./scripts/validate-manpages.sh build/man
```

Requires `mandoc` (OpenBSD/mandoc, macOS, or `mandoc` packages on Linux).
On systems with only groff, the script falls back to `groff -mandoc -Tutf8`.
