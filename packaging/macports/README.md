# MacPorts port (reference packaging)

Upstream-submittable material for a MacPorts `databases/glyphastore` port. It is
**not** an accepted port: no `glyphastore` entry exists in the MacPorts ports tree,
and nothing in this repository may claim otherwise. The `upstream-ports-acceptance`
check stays `OPEN_GATE` until that changes.

## Contents

| Path | Role |
| --- | --- |
| [`Portfile.in`](Portfile.in) | Portfile template; carries no version, master site or checksum |
| [`files/glyphastored.conf.sample`](files/glyphastored.conf.sample) | Prefix-relative configuration sample installed as `${prefix}/etc/glyphastore/glyphastored.conf.sample` |

A usable `Portfile` is produced by
[`engineering/tools/render_macos_packaging.py`](../../engineering/tools/render_macos_packaging.py),
which injects the port version from the `VERSION` authority through the release
context and the distfile identity from the source archive it is given. There is no
checked-in `Portfile`, so a port version can never drift from `VERSION`.

## How to render and lint

```bash
python3 engineering/tools/generate_release_context.py --output /tmp/ctx.json
python3 engineering/tools/render_macos_packaging.py --backend macports --profile pr \
  --release-context /tmp/ctx.json \
  --source-url "file:///path/to/GlyphaStore-<version>.tar.xz" \
  --source-sha256 "<sha256>" --output-dir /tmp/port

mkdir -p /tmp/tree/databases/glyphastore
cp /tmp/port/Portfile /tmp/tree/databases/glyphastore/
cp -R packaging/macports/files /tmp/tree/databases/glyphastore/
cd /tmp/tree/databases/glyphastore && port lint --nitpick
```

`scripts/package-ci.sh --profile pr --backend macports` does the same thing and
records the outcome as package evidence.

## Prefix isolation

A MacPorts build must resolve everything inside its own prefix. The Portfile
refuses `/opt/homebrew` and `/usr/local` through `CMAKE_IGNORE_PATH` and
`CMAKE_IGNORE_PREFIX_PATH`, and the `prefix-isolation` check re-proves it after
installation with `otool -L` over the installed daemon and shared library.

A link into a foreign prefix is a defect. The only way to accept one is to write
it, with a reason, into `packaging/macports/prefix-exceptions.txt`
(`<install name> # <reason>`); no such file exists today.

## Service integration: open gate

The port deliberately installs **no launchd startup item**. MacPorts startup items
run as root, while GlyphaStore requires the dedicated unprivileged `glyphastore`
account the port creates with `add_users`. Until a launchd model that keeps the
daemon unprivileged is designed, reviewed and proven with a retained run, the
`service-lifecycle` check is `OPEN_GATE` and the backend can never report `PASS`.

The `put-get-erase` and `restart-recovery` checks, when they run, start the
installed daemon **directly** under a driver-owned configuration. That is a binary
exercise, not an init-system managed service run.

## Release sources

The release profile refuses anything that is not a sealed source archive: the URL
must be `https`, must be named `GlyphaStore-<version>.tar.xz`, and must not look
like a git ref or checkout tarball. Set `GLYPHASTORE_SOURCE_ARCHIVE_URL` and
`GLYPHASTORE_SOURCE_ARCHIVE_SHA256` (optionally `..._SIZE` and `..._RMD160`).
For `pr`, `main` and `nightly` the adapter may build from a `file://` archive of
`HEAD`, whose digest it verifies before pinning it.

## Native lifecycle

`port lint`, `port destroot`, `port install`, `port contents`, the external
consumer build, the daemon exercise, `port deactivate` / `port activate`,
`port uninstall` and `port clean` only run on a macOS host with MacPorts **and**
`GLYPHASTORE_PACKAGE_CI_NATIVE=1`, because they mutate the host package manager.
Everything that did not run is reported as `NOT_RUN` or `BLOCKED`, never as `PASS`.
