# Packaging and publishing (RubyGems)

Gem name: `glifistore` · Module: `GlifiStore` · License: BSD-3-Clause · Ruby ≥ 3.2

Runtime dependencies: none (stdlib). Optional async stack: `async` gem (loaded only via
`require "glifi_store/async_client"`).

## Version

`GlifiStore::VERSION` in `lib/glifi_store/version.rb` is the single source. It must match the
repository root `VERSION` file (`./scripts/check-sdk-versions.sh`).

## Local verification

```bash
./scripts/package-ruby-client.sh
```

The script:

1. Confirms vendored wire fixtures match the repository corpus
2. Asserts `GlifiStore::VERSION` matches `VERSION`
3. Builds `sdk/ruby/dist/glifistore-VERSION.gem`
4. Installs the gem into a clean `GEM_HOME`
5. Runs the installed `glifistore-interop` executable with no source-tree load path
6. Runs the Minitest suite against the installed gem (plus development gems `async`, `minitest`)

With Python/Perl packages and TLS daemon/client peers already built, the installed gem participates
in the fail-closed secure-profile matrix through:

```bash
RUBY=/path/to/ruby-3.2-or-newer \
PERL=/path/to/perl-with-TLS-1.3 \
GLIFISTORED=/path/to/glifistored \
GLIFISTORE_INTEROP_CLIENT=/path/to/glifistore_interop_client \
GLIFISTORE_GO_INTEROP=/path/to/glifistore-interop \
./scripts/test-secure-profile-installed-artifacts.sh
```

## Manual build

```bash
cd sdk/ruby
gem build glifistore.gemspec --output dist/glifistore-0.1.0.gem
```

## Publish to RubyGems

```bash
gem push sdk/ruby/dist/glifistore-0.1.0.gem
```

Requires an account with MFA. `allowed_push_host` is restricted to `https://rubygems.org`.
Do not commit API keys.

## Version bump checklist

1. Bump `lib/glifi_store/version.rb`
2. Update `CHANGELOG.md`
3. Ensure root `VERSION` matches (SDK lockstep)
4. Run `./scripts/check-sdk-versions.sh` and `./scripts/package-ruby-client.sh`
5. Tag/release and `gem push`
