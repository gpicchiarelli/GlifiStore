# Debian / Ubuntu packaging

[`templates/`](templates/) holds the `debian/` sources as templates. There is no
checked-in `debian/` directory, and there deliberately never will be: the version,
the changelog entry, the maintainer, the package names and the install layout all
come from the release context, which derives the version from `VERSION` plus the
packaging-only `package_revision`. A checked-in `debian/changelog` would be a second
version authority.

Rendering is done by
[`../../engineering/tools/render_package_metadata.py`](../../engineering/tools/render_package_metadata.py):

```bash
python3 engineering/tools/generate_release_context.py --output /tmp/release-context.json
python3 engineering/tools/render_package_metadata.py \
    --backend deb --release-context /tmp/release-context.json --output /tmp/deb-metadata
```

It writes `debian/` plus a `rendered-metadata.json` manifest (tokens, layout, per-file
sha256) and a `layout.env` for the shell steps that run before Python exists in a
container. It refuses a template containing a literal product version, an unknown
`@TOKEN@`, and an unsubstituted placeholder left behind after substitution.

## Packages

| Package | Contents |
| --- | --- |
| `glifistore` | daemon, operator tools, systemd unit, `/etc/glifistore/glifistored.conf` |
| `libglifistore1` | `libglifistore.so.<abi-major>` — the ABI-major soname, so two ABI generations can coexist |
| `glifistore-dev` | headers, static libraries, the `.so` link, pkg-config and CMake package files |

The library package is **not** `Multi-Arch: same` and installs into `/usr/lib`
rather than `/usr/lib/<triplet>`. That is not laziness: the installed
`glifistore-abi.pc` derives its prefix as `${pcfiledir}/../..`, which only resolves
correctly from a non-multiarch libdir. Making the package multiarch-co-installable
requires changing how that prefix is computed, which is a separate change with its
own compatibility argument.

## Running the lifecycle

```bash
# Structural only: renders debian/ and reports the rest BLOCKED with reasons.
scripts/package-ci.sh --profile pr --backend deb --output-dir build/package-ci

# Full lifecycle inside the digest-pinned image from the package matrix.
GLIFISTORE_PACKAGE_CI_CONTAINER=1 \
    scripts/package-ci.sh --profile pr --backend deb --output-dir build/package-ci

# Full lifecycle on this host. Installs and removes system packages; needs root
# and a machine you are willing to lose.
sudo GLIFISTORE_PACKAGE_CI_NATIVE=1 \
    scripts/package-ci.sh --profile pr --backend deb --output-dir build/package-ci
```

Evidence lands in `build/package-ci/deb/<stage>/` with one retained log per check
and a `deb-<profile>-<stage>-package-evidence.json` validated against the
package-evidence schema.

## What the lifecycle actually checks

`package-lint` runs `dpkg-parsechangelog` and confirms the changelog version equals
the one the release context computed. `package-build` runs `dpkg-buildpackage -b`.
`package-inspect` reads the built `.deb` with `dpkg-deb` and compares its declared
contents against [`../common/file-lists/payload.yaml`](../common/file-lists/payload.yaml).
`prefix-isolation` reads the installed ELF objects with `readelf` and fails on an
`RPATH`/`RUNPATH`, a writable-executable stack, or a missing `GNU_RELRO`.
`external-consumer` copies [`../common/consumer/`](../common/consumer/) *outside* the
checkout, builds it with `CMAKE_PREFIX_PATH=/usr` and a stripped environment, and
then runs
[`../../engineering/tools/assert_consumer_isolation.py`](../../engineering/tools/assert_consumer_isolation.py)
over its `compile_commands.json`, logs and the installed `.pc`/`.cmake` files — so a
consumer that quietly found headers in `GITHUB_WORKSPACE` fails instead of passing.
`put-get-erase` and `restart-recovery` speak wire protocol v2 to the **installed**
`glifistored`, not to a build-tree binary.

## Residual

`package-upgrade` stays `NOT_APPLICABLE_INITIAL_BASELINE` or `NOT_RUN` until sealed N−1
packages are supplied via `GLIFISTORE_N1_PACKAGE_DIR`. The container lifecycle then runs
install→seed→upgrade→verify against those `.deb` bytes (never rebuilt from HEAD). It is not
a pass, and it does not promote the backend to `UPGRADE_VERIFIED`. Upstream Debian acceptance
is not claimed and not in scope here.
