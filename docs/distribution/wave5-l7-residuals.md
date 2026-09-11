# Wave 5 (L7) — release, portability, supply-chain residuals

Status: living honesty ledger (not a gate closure)
Applies to: C ABI cross-release, wire/disk N↔N−1, BSD packages, the multi-backend packaging
framework, signing/SLSA/SBOM, sealed publish
Owner: L7 / release maintainers
Last reviewed: 2026-09-11

Claim ceiling remains **architectural prototype**. Producers and validators may exist without
retained tagged evidence; absence of evidence is a hard residual, not a waiver.

## C ABI v1 — permanent fixture scaffolding

| Layer | Status |
| --- | --- |
| Spec + ADR 0038 + `ABI_VERSION` | Present |
| Symbol allowlist / layout probes / installed pure-C consumer | Present (`ci.yml` `install-consumer`) |
| Deterministic consumer packager + fixture schema/validator | Present |
| Same-run `abi-compatibility-evidence` producer | Present; fail-closed without prior attested release |
| Permanent old-binary × new-library row from a real tag | **Blocked** — no complete official ABI-1 release asset retained yet |

First-tag runbook (do not invent digests):

1. Cut annotated `vX.Y.Z` only when other release prerequisites are intentional residuals or closed.
2. Candidate packages `glyphastore-abi-v1-consumer-…tar.xz` via `scripts/package-abi-consumer.py`.
3. Retain that asset on the GitHub Release; optionally record a pointer under
   `engineering/evidence/release/` (never hand-written SHA substitutes).
4. The **next** tag's `abi-compatibility-evidence` job selects that prior release and runs both
   library directions through `scripts/test-installed-abi-compat.sh`.

Until step 4 succeeds with retained logs, matrix row `ABI-C1-N-MINUS-1` stays `not_promised`.

## Wire N↔N−1 and disk fixtures

| Kind | Present | Residual |
| --- | --- | --- |
| In-tree golden codecs | `tests/fixtures/*.hex` | Not cross-release proof |
| Released codec harness | `tests/fixtures/released/self-v1/` + `release-compat.yml` | Permanent `<semver>/` trees absent |
| Complete Store drops | `tests/fixtures/released-stores/` (README only) | No `<semver>/` baseline |
| Wire sealed client producer | `package-wire-client.py` + release job | No prior attested client/server bytes |

Fail-closed producers refuse same-build substitutes. Documented in
[`n-n1-matrix.yaml`](../../engineering/compatibility/n-n1-matrix.yaml) and
[`release-evidence.md`](release-evidence.md).

## FreeBSD / OpenBSD

| Signal | What it proves | What it does **not** prove |
| --- | --- | --- |
| `freebsd.yml` / `openbsd-libressl.yml` | Native build + tests (+ ABI install smoke) | Ports/pkg lifecycle |
| `packaging/{freebsd,openbsd}/` | Structural reference ports | Official ports acceptance or package bytes |
| `scripts/test-freebsd-package-lifecycle.sh` | Same-run producer (blocked on account markers) | Retained tagged package evidence |
| `scripts/test-openbsd-package-lifecycle.sh` | Same-run producer (blocked on account markers) | Retained tagged package evidence |
| `package-ci.sh --backend freebsd\|openbsd` | Structural rows plus a fail-closed hand-off to the native producers | Any native build, package, service or upstream row on a non-BSD runner |

`PORTS_ACCOUNT_REGISTERED` markers are deliberately absent until upstream ports allocation exists.
Do not commit circular `distinfo` into the source archive.

BSD package evidence separates `structural`, `native-build`, `package`, `service` and
`upstream-accepted` rows ([bsd-packaging.md](bsd-packaging.md)). Open by construction:

- `upstream-ports-acceptance` stays an open gate, so neither BSD backend can ever report `PASS`.
- `package-upgrade` is `NOT_APPLICABLE_INITIAL_BASELINE` (no prior annotated release) and becomes
  `NOT_RUN` — never `PASS` — until a sealed N−1 package artifact is admitted.
- `external-consumer` against the installed package prefix is unbuilt, capping the BSD lifecycle
  state at `FUNCTIONALLY_VERIFIED`.
- `package-ci.yml` retains the structural BSD rows per profile, but the native producers still run
  only in the release workflow VM. `GATE-PACKAGE-LIFECYCLE` now cites that workflow and stays
  `IMPLEMENTATA`: citing a retention path is not the same as having retained a run.
- The Linux container lifecycle (`deb`, `rpm`) is enabled from the `nightly` and `release`
  profiles only (`main` stays structural until a retained container run exists). A retained
  `nightly` dispatch on tip `16e1a0c` exercised the containers: deb built and installed packages
  but crashed before emitting evidence when `service-lifecycle` referenced an empty log and the
  protocol BACKUP pre-created a destination that `create_new` refuses; rpm linked successfully
  then aborted `%build` because a `# ... %cmake ...` comment was macro-expanded. Those three
  defects are corrected in-tree; a fresh retained nightly is still required to close the residual.
  When the inner driver writes evidence and exits non-zero, the outer dispatcher prefers that
  report over inventing `BLOCKED`. Deb rules no longer pass a Ninja generator into a dh/`make`
  build; the RPM `%cmake` line forces `-DBUILD_SHARED_LIBS=OFF`.
- MacPorts and Homebrew have no hosted runner that may install into the host package manager, so
  their native rows stay opt-in (`--allow-native`, never on by default) and unproven.

## Packaging framework (Waves A–G)

Declarative status is generated into [package-status.md](package-status.md); the operator path is
[package-ci.md](package-ci.md). Gates: `GATE-PACKAGE-LIFECYCLE` and `GATE-PACKAGE-ADMISSION`, both
`IMPLEMENTATA`. Requirements: `GS-RELEASE-PACKAGE-001`, `GS-RELEASE-UPGRADE-001`. Hazard: `HAZ-034`.

| Residual | State today | What would close it |
| --- | --- | --- |
| deb / rpm container lifecycle | Retained nightly on `a6dfdcc`: deb reaches `FUNCTIONALLY_VERIFIED` (service-lifecycle BLOCKED without systemd PID 1). RPM builds/inspects but `dnf` under container `tsflags=nodocs` omitted manuals so install inventory FAIL; install now clears `tsflags`. Fresh retained nightly still required for rpm parity | A retained `nightly` run whose deb/rpm build/install/protocol rows are not FAIL |
| MacPorts `launchd` startup item | Not installed by the port, so `service-lifecycle` is `OPEN_GATE` | A packaged startup item plus a retained run that starts and stops it |
| Homebrew `brew services` | Declared in the formula, never started or stopped in a retained run (`OPEN_GATE`) | A retained native run that exercises the services block |
| MacPorts / Homebrew native rows | Opt-in only (`--allow-native`); no hosted runner may install into the host package manager | A disposable macOS host or an accepted runner policy, with retained logs |
| `external-consumer` on BSD | Unbuilt, so both BSD backends cap at `FUNCTIONALLY_VERIFIED` | Building an external consumer against the installed package prefix in the native scripts |
| `package-upgrade` anywhere | Never positively run: `NOT_APPLICABLE_INITIAL_BASELINE` today, `NOT_RUN` once a predecessor exists | A sealed N−1 package artifact admitted through `upgrade_baseline.py admit` |
| Wave F admission tools | `upgrade_baseline.py`, `generate_artifact_manifest.py` and `validate_package_admission.py` exist with negative tests, but **no workflow calls them**; admission is a local manual step | Wiring them into the release graph fail-closed, with retained reports |
| Cross-SDK post-install matrix | `NOT_RUN`; the admission report blocks on its absence by construction | Running every SDK against a package-installed daemon and retaining the report |
| Optional backends | `deb`, `rpm`, `macports`, `homebrew` are `required_for_release: false` and cannot admit a release artifact | Retained `LIFECYCLE_VERIFIED` evidence, a release-policy artifact, an ADR and a gate update |
| Upstream acceptance | `OPEN_GATE` for FreeBSD, OpenBSD, MacPorts and Homebrew; in-repo packaging is the project pipeline only | Actual acceptance by the upstream ports tree or tap |
| Apple `.pkg` | Deliberately out of scope; refused by the matrix validator | An accepted ADR, an Apple signing/notarization identity and an update model |
| Windows / MSI | Out of scope; refused by the matrix validator | An accepted ADR plus platform durability evidence rows |
| Package evidence retention | No annotated tag exists, so no packaging evidence has ever been retained from a release | The first complete tagged release run |

Empty by design until real runs exist: [`engineering/evidence/release/`](../../engineering/evidence/release/README.md)
and the per-filesystem trees under
[`engineering/evidence/platform-durability/`](../../engineering/evidence/platform-durability/README.md)
carry READMEs only. An empty fixture directory is a residual, never an implicit pass.

## Supply chain and sealed publish

- Actions: 40-char SHA pins (`validate_actions_pins.py`) including Dependabot SHA-discipline check
  against `.github/dependabot.yml`.
- Publish (`release.yml`): re-verifies seal/checksums/attestation, **refuses an existing release
  tag**, never rebuilds, never `--clobber`s assets — different bytes require a new version.
- Least privilege: candidate/verify default `contents: read`; publish alone gets `contents: write`
  behind the protected `release` Environment; signing jobs elevate `id-token` / `attestations` only
  when needed.
- Overlapping PR vs tag scans (Trivy/gitleaks in `supply-chain-scan.yml` and tag
  `security-supply-chain`) are intentional: PR regression gate ≠ tag-retained matrix evidence.
  Do not prune the tag path while `security-matrix-evidence` requires those logs.

## Candidate build profile

The release candidate is built with `-DGLYPHASTORE_ENABLE_TLS=OFF` and records `--tls-backend none`
in its build metadata ([`release-candidate.yml`](../../.github/workflows/release-candidate.yml)).
Every artifact derived from that candidate — source archive, Linux prefix, ABI and wire fixtures,
and any package built from the sealed source — is therefore a TLS-less build. No release artifact
or package may be described as providing transport security, and a future TLS-enabled candidate is
a different artifact set, not a relabelling of this one.

## Signing / SLSA / SBOM

Implemented paths (Cosign keyless, SPDX bind-sbom, optional GitHub attestations) are **not**
retained tagged evidence until a successful tag deposits artifacts and pointers under
[`engineering/evidence/release/`](../../engineering/evidence/release/README.md). Project GPG and
full SLSA Build L3 remain explicit residuals.

## Correctness gates vs performance gates

Correctness / portability / distribution gates (`CI`, `Assurance`, FreeBSD/OpenBSD, install-consumer,
release sealing) stay independent of absolute performance budgets and hard-pinned scaling
([evidence-taxonomy.md](../assurance/evidence-taxonomy.md)). Wave 6 hardware/`ACCETTATA` work does
not reopen Wave 5 sealing residuals, and Wave 5 must not absorb absolute perf claims from macOS
`local` benches.

## Related

- [Release checklist](../assurance/release-checklist.md)
- [Artifact delivery](artifact-delivery.md)
- [Package CI operator guide](package-ci.md)
- [Package backend status](package-status.md)
- [BSD packaging](bsd-packaging.md)
- [Debt remediation lanes](../assurance/debt-remediation-lanes.md)
