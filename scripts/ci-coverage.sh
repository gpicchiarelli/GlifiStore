#!/usr/bin/env bash
# Diagnostic coverage report (not a merge gate). See docs/development/test-strategy.md §7.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

outdir="${GLYPHASTORE_COVERAGE_OUT:-$root/build/coverage}"
builddir="$root/build/unix-coverage"
rm -rf "$outdir" "$builddir"
mkdir -p "$outdir"

export CC="${CC:-clang}"
export CXX="${CXX:-clang++}"
export CFLAGS="${CFLAGS:---coverage -O0 -g}"
export CXXFLAGS="${CXXFLAGS:---coverage -O0 -g}"
export LDFLAGS="${LDFLAGS:---coverage}"

cmake -S "$root" -B "$builddir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$CFLAGS" \
  -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS" \
  -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS" \
  -DGLYPHASTORE_FAULT_INJECTION=ON \
  -DBUILD_TESTING=ON

cmake --build "$builddir" --target glyphastore_tests
ctest --test-dir "$builddir" --output-on-failure --tests-regex '^glyphastore_tests$'

if ! command -v lcov >/dev/null 2>&1; then
  echo "lcov is required to generate the diagnostic coverage report" >&2
  exit 1
fi

gcov_args=()
compiler_version="$("$CXX" --version 2>/dev/null || true)"
if grep -qi clang <<<"$compiler_version"; then
  coverage_llvm_cov="${LLVM_COV:-}"
  if [[ -z "$coverage_llvm_cov" ]]; then
    coverage_llvm_cov="$(command -v llvm-cov || true)"
  fi
  if [[ -z "$coverage_llvm_cov" ]]; then
    clang_major="$("$CXX" -dumpversion | cut -d. -f1)"
    coverage_llvm_cov="$(command -v "llvm-cov-$clang_major" || true)"
  fi
  if [[ -z "$coverage_llvm_cov" ]]; then
    echo "llvm-cov is required to decode Clang coverage data" >&2
    exit 1
  fi
  export LLVM_COV="$coverage_llvm_cov"
  gcov_args=(--gcov-tool "$root/scripts/llvm-gcov.sh")
fi

lcov --capture --directory "$builddir" --output-file "$outdir/coverage.raw.lcov" \
  "${gcov_args[@]}" --ignore-errors mismatch,inconsistent,unused
lcov --remove "$outdir/coverage.raw.lcov" \
  '/usr/*' '*/tests/*' '*/_deps/*' \
  --output-file "$outdir/coverage.lcov" \
  --ignore-errors inconsistent,unused
lcov --list "$outdir/coverage.lcov" --ignore-errors inconsistent \
  | tee "$outdir/coverage-report.txt"
[[ -s "$outdir/coverage.raw.lcov" && -s "$outdir/coverage.lcov" &&
   -s "$outdir/coverage-report.txt" ]]
grep -q '^SF:' "$outdir/coverage.raw.lcov"
grep -q '^DA:' "$outdir/coverage.raw.lcov"
grep -q '^SF:' "$outdir/coverage.lcov"
grep -q '^DA:' "$outdir/coverage.lcov"

echo "Coverage artifacts under $outdir (diagnostic only; not an acceptance gate)."
ls -la "$outdir"
