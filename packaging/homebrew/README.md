# Homebrew formula (reference packaging)

Upstream-submittable material for a `glyphastore` Homebrew formula. It is **not**
in `homebrew/core` and there is no official GlyphaStore tap; the
`upstream-ports-acceptance` check stays `OPEN_GATE` until one exists.

## Contents

| Path | Role |
| --- | --- |
| [`glyphastore.rb.in`](glyphastore.rb.in) | Formula template; carries no version, url or sha256 |

A usable `glyphastore.rb` is produced by
[`engineering/tools/render_macos_packaging.py`](../../engineering/tools/render_macos_packaging.py),
which injects the version from the `VERSION` authority through the release context
and the source identity from the archive it is given. Nothing is checked in already
rendered, so the formula version can never drift from `VERSION`.

## How to render and audit

```bash
python3 engineering/tools/generate_release_context.py --output /tmp/ctx.json
python3 engineering/tools/render_macos_packaging.py --backend homebrew --profile main \
  --release-context /tmp/ctx.json \
  --source-url "file:///path/to/GlyphaStore-<version>.tar.xz" \
  --source-sha256 "<sha256>" --output-dir /tmp/formula

brew audit --strict --formula /tmp/formula/glyphastore.rb
```

`scripts/package-ci.sh --profile main --backend homebrew` does the same thing and
records the outcome as package evidence.

## Prefix isolation

A Homebrew build must not reach into a MacPorts prefix. The formula refuses
`/opt/local` through `CMAKE_IGNORE_PATH` and `CMAKE_IGNORE_PREFIX_PATH`, and the
`prefix-isolation` check re-proves it after installation with `otool -L` over the
installed daemon and shared library. `/usr/local` is *not* forbidden here: on
Intel macOS that is Homebrew's own prefix.

A link into `/opt/local` is a defect. The only way to accept one is to write it,
with a reason, into `packaging/homebrew/prefix-exceptions.txt`
(`<install name> # <reason>`); no such file exists today.

## Service integration: open gate

The formula declares a `service do` block, so `brew services start glyphastore`
would generate a launchd job running as the invoking user. **No retained run has
ever exercised it.** The `service-lifecycle` check is therefore `OPEN_GATE` and the
backend can never report `PASS` today; a declared service block is packaging
source, not a proof.

The `put-get-erase` and `restart-recovery` checks, when they run, start the
installed daemon **directly** under a driver-owned configuration, which is a binary
exercise rather than an init-system managed service run.

## Configuration and data

`cmake --install` writes a sample whose `data-dir` points inside the versioned keg,
so the formula rewrites it to `#{var}/glyphastore` and installs it as
`#{etc}/glyphastore/glyphastored.conf.sample`. Homebrew preserves `etc` files
across upgrades, and `brew uninstall` never touches `var`, which is what the shared
[configuration and data policy](../common/config-data-policy.md) requires.

## Release sources

The release profile refuses anything that is not a sealed source archive: the URL
must be `https`, must be named `GlyphaStore-<version>.tar.xz`, and must not look
like a git ref or checkout tarball. Set `GLYPHASTORE_SOURCE_ARCHIVE_URL` and
`GLYPHASTORE_SOURCE_ARCHIVE_SHA256`. For `main` and `nightly` the adapter may build
from a `file://` archive of `HEAD`, whose digest it verifies before pinning it.

## Native lifecycle

`brew audit`, `brew install --build-from-source`, `brew list`, the external consumer
build, `brew test`, the daemon exercise, `brew uninstall` and `brew cleanup` only run
on a macOS host with Homebrew **and** `GLYPHASTORE_PACKAGE_CI_NATIVE=1`, because they
mutate the host package manager. Everything that did not run is reported as
`NOT_RUN` or `BLOCKED`, never as `PASS`.
