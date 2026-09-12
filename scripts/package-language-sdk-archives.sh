#!/usr/bin/env bash
# Build the sealed language SDK archives the package-installed matrix consumes.
#
# Unlike package-all-sdk-clients.sh this does not rebuild the C++ client package or
# the complete checksum index: packaging CI only needs python/perl/ruby/go/erlang
# archives under sdk/*/dist so the container can prove a package-owned daemon.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export GLYPHASTORE_ROOT="$root"
# shellcheck disable=SC1091
source "$root/scripts/export-reproducible-build-env.sh"

"$root/scripts/check-sdk-versions.sh"
"$root/scripts/sync-sdk-fixtures.sh"
"$root/scripts/package-python-client.sh"
"$root/scripts/package-perl-client.sh"
"$root/scripts/package-go-client.sh"
"$root/scripts/package-ruby-client.sh"
"$root/scripts/package-erlang-client.sh"

shopt -s nullglob
python_wheels=("$root"/sdk/python/dist/glyphastore-*.whl)
perl_tarballs=("$root"/sdk/perl/dist/GlyphaStore-*.tar.gz)
ruby_gems=("$root"/sdk/ruby/dist/glyphastore-*.gem)
go_archives=("$root"/sdk/go/dist/glyphastore-go-*.tar.gz)
erlang_archives=("$root"/sdk/erlang/dist/glyphastore-erlang-*.tar.gz)
shopt -u nullglob

missing=()
[[ ${#python_wheels[@]} -eq 1 ]] || missing+=("python")
[[ ${#perl_tarballs[@]} -eq 1 ]] || missing+=("perl")
[[ ${#ruby_gems[@]} -eq 1 ]] || missing+=("ruby")
[[ ${#go_archives[@]} -eq 1 ]] || missing+=("go")
[[ ${#erlang_archives[@]} -eq 1 ]] || missing+=("erlang")
if ((${#missing[@]} > 0)); then
  echo "error: sealed SDK archives missing after packaging: ${missing[*]}" >&2
  exit 1
fi

echo "Language SDK archives ready for the package-installed matrix"
