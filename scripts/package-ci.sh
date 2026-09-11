#!/usr/bin/env bash
# Single entry point for GlyphaStore packaging CI.
#
# Resolves the release context from VERSION (never a hard-coded version), expands
# the package matrix for a CI profile, and runs the requested backend adapters.
# Backends without an implementation yet emit honest NOT_RUN / BLOCKED / OPEN_GATE
# evidence; nothing here fabricates a PASS. Workflows call this script instead of
# duplicating packaging logic in YAML.
set -euo pipefail

usage() {
  cat >&2 <<'USAGE'
usage: package-ci.sh --profile {pr|main|nightly|release}
                     [--backend NAME]... [--all]
                     [--stage {metadata|build|inspect|install|verify|upgrade|remove|full}]
                     [--package-revision N] [--output-dir DIR] [--release-context FILE]

Exit status is non-zero when a backend reports FAIL or when any tool refuses.
USAGE
  exit 2
}

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tools="$root/engineering/tools"
profile=""
stage="full"
package_revision=0
output_dir=""
release_context=""
all_backends=0
backends=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile) [[ $# -ge 2 ]] || usage; profile="$2"; shift 2 ;;
    --backend) [[ $# -ge 2 ]] || usage; backends+=("$2"); shift 2 ;;
    --all) all_backends=1; shift ;;
    --stage) [[ $# -ge 2 ]] || usage; stage="$2"; shift 2 ;;
    --package-revision) [[ $# -ge 2 ]] || usage; package_revision="$2"; shift 2 ;;
    --output-dir) [[ $# -ge 2 ]] || usage; output_dir="$2"; shift 2 ;;
    --release-context) [[ $# -ge 2 ]] || usage; release_context="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "error: unknown argument: $1" >&2; usage ;;
  esac
done

case "$profile" in
  pr|main|nightly|release) ;;
  *) echo "error: --profile is required and must be pr, main, nightly or release" >&2; usage ;;
esac
case "$stage" in
  metadata|build|inspect|install|verify|upgrade|remove|full) ;;
  *) echo "error: unsupported --stage: $stage" >&2; usage ;;
esac
[[ "$package_revision" =~ ^[0-9]+$ ]] || { echo "error: --package-revision must be a non-negative integer" >&2; exit 2; }
if (( all_backends == 1 )) && (( ${#backends[@]} > 0 )); then
  echo "error: --all and --backend are mutually exclusive" >&2
  exit 2
fi
if (( all_backends == 0 )) && (( ${#backends[@]} == 0 )); then
  echo "error: choose --all or at least one --backend" >&2
  exit 2
fi
command -v python3 >/dev/null 2>&1 || { echo "error: python3 is required" >&2; exit 1; }

version="$(<"$root/VERSION")"
commit="$(git -C "$root" rev-parse --short=12 HEAD)"
run_id="${GITHUB_RUN_ID:-local-$(date -u +%Y%m%dT%H%M%SZ)}"
if [[ -z "$output_dir" ]]; then
  output_dir="$root/build/package-ci/$version/$commit/$profile/$run_id"
fi
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd -P)"

if [[ -z "$release_context" ]]; then
  release_context="$output_dir/release-context.json"
  context_arguments=(--root "$root" --package-revision "$package_revision" --output "$release_context" --replace)
  [[ "$profile" == "release" ]] && context_arguments+=(--require-clean)
  python3 "$tools/generate_release_context.py" "${context_arguments[@]}" >/dev/null
fi
[[ -f "$release_context" ]] || { echo "error: release context is missing: $release_context" >&2; exit 1; }

matrix_json="$output_dir/package-matrix.json"
python3 "$tools/generate_package_matrix.py" expand --profile "$profile" \
  --output "$matrix_json" --replace >/dev/null

available=()
while IFS= read -r line; do
  [[ -n "$line" ]] && available+=("$line")
done < <(python3 "$tools/generate_package_matrix.py" backends --profile "$profile")
(( ${#available[@]} > 0 )) || {
  echo "error: the package matrix lists no backend for profile '$profile'" >&2
  exit 1
}

if (( all_backends == 1 )); then
  backends=("${available[@]}")
fi
for backend in "${backends[@]}"; do
  found=0
  for candidate in "${available[@]}"; do
    [[ "$backend" == "$candidate" ]] && found=1
  done
  if (( found == 0 )); then
    echo "error: backend '$backend' does not run in profile '$profile' (available: ${available[*]})" >&2
    exit 2
  fi
done

# Structural adapter for backends whose packaging lands in a later wave. It runs the
# checks that genuinely can run here and reports every other check as NOT_RUN, or as
# BLOCKED when the host cannot possibly run it.
run_backend() {
  local backend="$1"
  local directory="$output_dir/$backend/$stage"
  mkdir -p "$directory"

  local plan="$directory/check-plan.json"
  local evidence="$directory/$backend-$profile-$stage-package-evidence.json"
  local structural_log="structural-metadata.log"
  local plan_arguments=(check-plan --backend "$backend" --profile "$profile")
  local result="OPEN_GATE"
  local lifecycle_state="STRUCTURAL"
  local limitations=()
  local host; host="$(uname -s)"

  {
    echo "backend=$backend profile=$profile stage=$stage"
    echo "release_context=$release_context"
    echo "package_matrix=$matrix_json"
    echo "product_version=$version package_revision=$package_revision commit=$commit"
    echo "structural metadata resolved without a hard-coded version"
  } >"$directory/$structural_log"

  plan_arguments+=(--default-status NOT_RUN --status "structural-metadata=PASS"
    --evidence-ref "structural-metadata=$structural_log")

  case "$backend" in
    freebsd|openbsd)
      local port_log="reference-port-structure.log"
      local port_status="PASS"
      if ! python3 "$tools/validate_bsd_packaging.py" --root "$root" \
        >"$directory/$port_log" 2>&1; then
        port_status="FAIL"
      fi
      plan_arguments+=(--status "reference-port-structure=$port_status"
        --evidence-ref "reference-port-structure=$port_log")
      if [[ "$port_status" == "FAIL" ]]; then
        result="FAIL"
        lifecycle_state="NONE"
      fi
      # The native lifecycle needs the matching operating system; say so instead of
      # reporting NOT_RUN on a host that could never run it.
      local expected_host="FreeBSD"
      [[ "$backend" == "openbsd" ]] && expected_host="OpenBSD"
      if [[ "$host" != "$expected_host" ]]; then
        local blocked
        for blocked in package-build package-inspect package-install external-consumer \
          service-lifecycle put-get-erase restart-recovery package-upgrade package-remove; do
          plan_arguments+=(--status "$blocked=BLOCKED"
            --detail "$blocked=requires a native $expected_host host; this runner is $host")
        done
      fi
      limitations+=("The native $expected_host package and service lifecycle still runs in scripts/test-$backend-package-lifecycle.sh; Wave D folds it into package-ci.sh.")
      limitations+=("PORTS_ACCOUNT_REGISTERED is an upstream allocation and is never created by this script.")
      ;;
    deb|rpm|macports|homebrew)
      limitations+=("No packaging implementation exists for $backend yet; only structural metadata was resolved.")
      ;;
    *)
      echo "error: no adapter for backend '$backend'" >&2
      return 2
      ;;
  esac

  if [[ "$profile" == "release" && "$result" != "FAIL" ]]; then
    limitations+=("The release profile requires sealed artifacts; this run produced none.")
  fi

  python3 "$tools/generate_package_matrix.py" "${plan_arguments[@]}" \
    --output "$plan" --replace >/dev/null

  local emit_arguments=(emit --backend "$backend" --profile "$profile" --stage "$stage"
    --result "$result" --lifecycle-state "$lifecycle_state"
    --release-context "$release_context" --check-plan "$plan" --output "$evidence")
  local limitation
  for limitation in "${limitations[@]}"; do
    emit_arguments+=(--limitation "$limitation")
  done
  emit_arguments+=(--residual "packaging-not-implemented=Backend $backend has no packaged artifact in Wave A|later packaging waves")

  python3 "$tools/validate_package_evidence.py" "${emit_arguments[@]}" >/dev/null
  python3 "$tools/validate_package_evidence.py" validate "$evidence" \
    --release-context "$release_context" --backend "$backend" --profile "$profile" >/dev/null

  echo "PACKAGE-CI $backend $profile $stage $result $evidence"
  [[ "$result" == "FAIL" ]] && return 1
  return 0
}

status=0
for backend in "${backends[@]}"; do
  run_backend "$backend" || status=1
done

echo "package-ci profile=$profile stage=$stage backends=${backends[*]} output=$output_dir"
exit "$status"
