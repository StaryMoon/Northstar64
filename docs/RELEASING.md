# Releasing

1. Move user-visible changes from `Unreleased` into a dated changelog section.
2. Set `kVersion` in `include/northstar64/version.hpp` to the exact release version.
3. Add `docs/releases/vX.Y.Z.md` with highlights, evidence, and explicit non-claims.
4. Run `make clean && make check CXXFLAGS='-std=c++20 -O2 -g -Werror'`.
5. Confirm CI and CodeQL are green on the release commit.
6. Create an annotated `vX.Y.Z` tag pointing to that commit and push it.

The tag workflow builds Linux x86-64 and macOS arm64 archives, runs the suite, emits SHA-256 files,
attests archive provenance through GitHub OIDC, and publishes the release. A checksum detects changes
relative to a trusted checksum file; provenance binds the archive to the GitHub workflow. Neither is
a substitute for a locally verified signed maintainer tag.

