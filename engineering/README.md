# Assurance catalog (authority)

Machine-readable requirements, hazards, quality gates, waivers, claims, and evidence
pointers for GlyphaStore. Markdown under `docs/production-readiness.md` and
`docs/assurance/` is **generated** from this tree.

GlyphaStore remains an **architectural prototype**. Do not treat closed checklist
boxes as production readiness.

## Layout

| Path | Contents |
| --- | --- |
| `schemas/` | JSON Schema for requirements, hazards, gates, waivers |
| `requirements/` | `GS-*` requirements by category |
| `hazards/` | Hazard register (brief §7 coverage) |
| `gates/` | Multi-state quality gates |
| `waivers/` | Time-bounded waivers (CI rejects expired) |
| `build/` | CMake dependency matrix + structure debt thresholds |
| `compatibility/` | N↔N-1 matrix |
| `distribution/` | Package backend matrix (backends, targets, CI profiles, check vocabulary) |
| `performance/` | Hosted vs hardware budgets + soak/overload linkage |
| `claims/` / `evidence/` | Release claims and retained evidence pointers |
| `tools/validate_assurance.py` | Validator + Markdown generator |
| `tools/validate_documentation.py` | All tracked Markdown: UTF-8 and repository-local link integrity |
| `tools/run_clang_tidy_gate.py` | Production compile database: fail-closed high-signal diagnostics |
| `tools/validate_cmake_deps.py` | Phase C CMake layout + include rules |
| `tools/validate_structure_debt.py` | Size/TODO thresholds with waivers |
| `tools/validate_actions_pins.py` | SHA-pinned Actions |
| `tools/validate_compat_matrix.py` | N↔N-1 matrix |
| `tools/validate_claims.py` | Release claim schema |
| `tools/validate_perf_budgets.py` | Performance/soak budget catalog |
| `tools/release_identity.py` / `release_bundle.py` | Tag identity, manifests, checksums and transitive seals |
| `tools/prior_release.py` / fixture validators | Fail-closed prior ABI/wire release selection and retained consumer extraction |
| `tools/compare_release_rebuild.py` | Closed-set independent release archive and build-authority comparison |
| `tools/validate_bsd_packaging.py` | Reference-port invariants and native release prerequisites |
| `tools/generate_package_matrix.py` | Package matrix validation and profile expansion |
| `tools/generate_package_status.py` | Generator for the derived `docs/distribution/package-status.md` view |
| `tools/validate_package_evidence.py` | Fail-closed package evidence: a result never exceeds its checks |
| `tools/upgrade_baseline.py` / `n1_package_artifacts.py` / `validate_package_admission.py` / `run_package_admission.py` | SemVer-aware sealed N-1 selection, install→seed→upgrade→verify wiring for Linux deb/rpm, FreeBSD/OpenBSD, and MacPorts/Homebrew when `GLYPHASTORE_N1_PACKAGE_DIR` supplies sealed predecessor bytes, and exact-byte package admission (wired in package-ci.yml / release.yml; positive admission / sealed N-1 PASS still residual) |
| `formal/shard_pair/` | Reduced TLA+ ShardPair model + TLC helper |
| `formal/persistence/` | Abstract write/sync/commit-slot/Manifest/recovery TLA+ model |

## Validate

```bash
python3 -m pip install PyYAML jsonschema
python3 engineering/tools/validate_assurance.py
python3 engineering/tools/validate_assurance.py --write-generated
python3 engineering/tools/validate_documentation.py
python3 engineering/tools/run_clang_tidy_gate.py --build-dir build/unix-clang-tidy
python3 engineering/tools/validate_cmake_deps.py
python3 engineering/tools/validate_structure_debt.py
python3 engineering/tools/validate_actions_pins.py
python3 engineering/tools/validate_compat_matrix.py
python3 engineering/tools/validate_claims.py
python3 engineering/tools/validate_perf_budgets.py
python3 engineering/tools/validate_bsd_packaging.py
python3 engineering/tools/generate_package_matrix.py validate
python3 engineering/tools/generate_package_status.py
python3 engineering/tools/generate_package_status.py --write-generated
python3 -m unittest scripts.tests.test_release_artifacts scripts.tests.test_bsd_packaging \
  scripts.tests.test_artifact_release_workflow
```

CI: `.github/workflows/assurance.yml`.
