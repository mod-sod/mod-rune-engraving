#!/usr/bin/env bash
#
# Build and run mod-rune-engraving's unit tests inside the AzerothCore
# `ac-dev-server` container, isolated from the live stack. The build tree and
# ccache live in Docker volumes (ac-build-dev / ac-ccache-dev), so nothing lands
# in the project folder and the running server is untouched.
#
# Run from the repo root:
#   docker compose --profile dev run --rm ac-dev-server \
#       bash modules/mod-rune-engraving/tests/run-in-docker.sh [JOBS]
#
# JOBS defaults to all cores; pass a number (e.g. 4) to leave the host headroom.
set -e

cd /azerothcore

# Keep ccache inside the (writable, persisted) build volume. The dedicated
# ac-ccache-dev volume mounts root-owned, which the non-root build user can't
# write to; var/build is owned by the build user and persists the same way.
export CCACHE_DIR=/azerothcore/var/build/.ccache
mkdir -p "$CCACHE_DIR"
JOBS="${1:-$(nproc)}"

# unit_tests links the whole `modules` library, so it compiles every sibling
# module together — that's intentional: the build only passes if all installed
# modules compile as a set.
cmake -S . -B var/build \
  -DCMAKE_INSTALL_PREFIX=/azerothcore/env/dist \
  -DCMAKE_BUILD_TYPE=Release \
  -DSCRIPTS=static -DMODULES=static -DBUILD_TESTING=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

cmake --build var/build --target unit_tests -j"${JOBS}"

./var/build/src/test/unit_tests --gtest_filter="Rune*"
