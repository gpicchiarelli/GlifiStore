# Shared installed-file inventory

Placeholder. The shared list of files a GlyphaStore package installs (binaries,
libraries with their ABI-major symlinks, headers, pkg-config files, manual pages,
sample configuration) will live here so that the Debian, RPM, MacPorts, Homebrew
and BSD payload checks compare against one inventory.

No inventory exists yet. Until one does, the `package-install` check stays
`NOT_RUN` for every backend that has no packaging implementation, and the BSD
reference ports keep their own native `pkg-plist` / `PLIST` as the authority.
