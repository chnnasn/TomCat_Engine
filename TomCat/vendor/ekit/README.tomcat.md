# Vendored ekit

Upstream: https://github.com/chnnasn/ekit
Pinned commit: `82d4de67f37d5d146bb7287e07116dc7567af996`
Base main: `03ba18e85e09ef7487ccf02c5bebbede82e05f57`
Upstream PR: https://github.com/chnnasn/ekit/pull/2 (pending merge)
License: MIT (see LICENSE).

The include directory is an unmodified copy of that commit's include directory.
The PR supports owning sparse components, preserves references during sparse
storage growth, rejects conflicting storage registration, and fixes multi-TU
linkage of Entity::Null. Dense components remain trivially copyable.

To update, copy include/ and LICENSE from a tested upstream commit and update
this pin. Run the upstream CMake/CTest suite and TomCat's native regressions.
