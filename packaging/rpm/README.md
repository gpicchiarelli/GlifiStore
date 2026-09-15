# Fedora / RPM packaging

[`templates/glifistore.spec.in`](templates/glifistore.spec.in) is the spec
template. There is no checked-in `.spec`, because `Version`, `Release`, the ABI
suffix of the library subpackage, the `%changelog` entry and the source archive name
all come from the release context, which derives the version from `VERSION` plus the
packaging-only `package_revision`. A checked-in spec would be a second version
authority.

Rendering is done by
[`../../engineering/tools/render_package_metadata.py`](../../engineering/tools/render_package_metadata.py):

```bash
python3 engineering/tools/generate_release_context.py --output /tmp/release-context.json
python3 engineering/tools/render_package_metadata.py \
    --backend rpm --release-context /tmp/release-context.json --output /tmp/rpm-metadata
```

It writes `glifistore.spec`, the systemd unit as `Source1`, a
`rendered-metadata.json` manifest (tokens, layout, per-file sha256) and a
`layout.env` for the shell steps that run before Python exists in a container. It
refuses a template containing a literal product version, an unknown `@TOKEN@`, and
an unsubstituted placeholder left behind after substitution.

The `libdir` and `unitdir` tokens are not guessed: the lifecycle asks the local rpm
for `%{_libdir}` and `%{_unitdir}` and overrides the defaults with the answer, so the
payload check and the spec agree on a system where those macros differ.

## Subpackages

| Subpackage | Contents |
| --- | --- |
| `glifistore` | daemon, operator tools, systemd unit, `%config(noreplace) /etc/glifistore/glifistored.conf` |
| `glifistore-libs` | `libglifistore.so.<abi-major>` — the ABI-major soname |
| `glifistore-devel` | headers, static libraries, the `.so` link, pkg-config and CMake package files |

The spec builds with the distribution `%cmake` macros, uses
`%systemd_post`/`%systemd_preun`/`%systemd_postun_with_restart` for the unit and
`%ldconfig_scriptlets` for the library, and creates the `glifistore` system account
in `%pre` from the shared snippet in
[`../common/service/service-account.sh.in`](../common/service/service-account.sh.in) —
the same snippet the Debian `postinst` uses.

## Running the lifecycle

```bash
# Structural only: renders the spec and reports the rest BLOCKED with reasons.
scripts/package-ci.sh --profile main --backend rpm --output-dir build/package-ci

# Full lifecycle inside the digest-pinned image from the package matrix.
GLIFISTORE_PACKAGE_CI_CONTAINER=1 \
    scripts/package-ci.sh --profile main --backend rpm --output-dir build/package-ci

# Full lifecycle on this host. Installs and removes system packages; needs root
# and a machine you are willing to lose.
sudo GLIFISTORE_PACKAGE_CI_NATIVE=1 \
    scripts/package-ci.sh --profile main --backend rpm --output-dir build/package-ci
```

`rpm` does not run in the `pr` profile — the matrix declares it for `main`,
`nightly` and `release` — so `--profile pr --backend rpm` is refused rather than
silently skipped.

## What the lifecycle actually checks

`package-lint` runs `rpmspec --query` and confirms the queried version-release
matches the one the release context computed. `package-build` runs `rpmbuild -bb`
against a source archive produced by `git archive`, or against the sealed candidate
in the release profile. `package-inspect` reads the built RPM with `rpm --query` and
compares its declared contents against
[`../common/file-lists/payload.yaml`](../common/file-lists/payload.yaml).
`prefix-isolation` reads the installed ELF objects with `readelf` and fails on an
`RPATH`/`RUNPATH`, a writable-executable stack, or a missing `GNU_RELRO`.
`external-consumer` copies [`../common/consumer/`](../common/consumer/) *outside* the
checkout, builds it with `CMAKE_PREFIX_PATH=/usr` and a stripped environment, and
then runs
[`../../engineering/tools/assert_consumer_isolation.py`](../../engineering/tools/assert_consumer_isolation.py)
over its `compile_commands.json`, logs and the installed `.pc`/`.cmake` files.
`put-get-erase` and `restart-recovery` speak wire protocol v2 to the **installed**
`glifistored`, not to a build-tree binary.

## Residuals

`package-upgrade` stays `NOT_APPLICABLE_INITIAL_BASELINE` or `NOT_RUN` until sealed N−1
packages are supplied via `GLIFISTORE_N1_PACKAGE_DIR`. The container lifecycle then runs
install→seed→upgrade→verify against those `.rpm` bytes (never rebuilt from HEAD). It is not
a pass, and it does not promote the backend to `UPGRADE_VERIFIED`.

Only the Fedora row in the package matrix may ever be reported. Rocky, Alma and RHEL
are *not* claimed: an untested `%dist` is a different build. Upstream Fedora package
review is not claimed and not in scope here.
