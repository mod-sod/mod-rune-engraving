# Testing

The engine ships Google Test unit tests that run inside AzerothCore's existing
`unit_tests` target — **no core edits**, and they only build when you ask for them.

## What's covered

The tests target the **pure eligibility logic** — the slot/class bitmask rules in
`RuneRules` (`src/RuneEngravingMgr.h`) that the `rune_template` catalog contract
depends on:

- `FitsSlot(slotMask, slot)` — single/multi-slot masks, empty masks, out-of-range slots.
- `AllowedForClass(classMask, classId)` — `0` = any class, single- and multi-class masks.
- `SlotIsValid`, `SlotName`, and the slot-enum/class-mask **contract constants**
  (e.g. `1 << RUNE_SLOT_CHEST == 16`, Mage mask `== 128`) — so a reorder can't
  silently break every content module's SQL.

The manager delegates its real eligibility checks to these functions, so the tests
exercise the production code, not a copy.

### What's *not* unit-tested (and why)

The stateful, side-effecting paths — `Engrave`, `RemoveRune`, `ApplyAll`,
`LoadPlayer`, the quest-unlock flow — depend on a live `Player` and the world /
character databases. Those aren't constructable in the unit harness, so they're
left to in-game verification ([Deploy & verify](deploy-and-verify.md)). Covering
them would need a DB-backed integration harness, which this module doesn't set up.

## How it's wired

- `mod-rune-engraving.cmake` (included inline by `modules/CMakeLists.txt`) appends
  the test source and the `src/` include dir to the global
  `ACORE_MODULE_TEST_SOURCES` / `ACORE_MODULE_TEST_INCLUDES` properties — but only
  when `BUILD_TESTING` is on.
- The core's `src/test/CMakeLists.txt` adds those sources to `unit_tests` and links
  the `modules` library, so the tests compile and link against the module.
- Test sources live in `tests/` (a sibling of `src/`), so they are **not** compiled
  into the `modules` library itself — only into `unit_tests`.

## Building and running

```bash
mkdir -p build && cd build
cmake .. -DBUILD_TESTING=ON -DSCRIPTS=static -DMODULES=static
make -j$(nproc) unit_tests

# run everything…
ctest --output-on-failure
# …or just the rune tests
./src/test/unit_tests --gtest_filter='Rune*'
```

When configured, the log shows `mod-rune-engraving: registered unit tests` and
`Added module tests to unit_tests target`.

## Running in the Docker dev container (isolated)

If you run the server via the AzerothCore Docker compose stack, build and run the
tests in the **`ac-dev-server`** service (compose profile `dev`) instead of
installing a toolchain on the host. It bind-mounts the source at `/azerothcore`
and keeps the build tree and ccache in dedicated Docker volumes (`ac-build-dev`,
`ac-ccache-dev`), so it's isolated from your running `ac-worldserver`/`ac-database`
and leaves no build files in your project folder.

```bash
# from the repo root (where docker-compose.yml lives) — uses the helper script
docker compose --profile dev run --rm ac-dev-server \
  bash modules/mod-rune-engraving/tests/run-in-docker.sh        # all cores
# …or leave the host headroom:
docker compose --profile dev run --rm ac-dev-server \
  bash modules/mod-rune-engraving/tests/run-in-docker.sh 4      # -j4
```

The helper (`tests/run-in-docker.sh`) just runs the configure/build/test steps:

```bash
cmake -S . -B var/build -DCMAKE_INSTALL_PREFIX=/azerothcore/env/dist \
  -DCMAKE_BUILD_TYPE=Release -DSCRIPTS=static -DMODULES=static -DBUILD_TESTING=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build var/build --target unit_tests -j"$(nproc)"
./var/build/src/test/unit_tests --gtest_filter="Rune*"
```

Notes:

- `docker compose run` does **not** publish ports, so it never collides with the
  live worldserver/authserver. `--rm` removes the one-off container when done.
- Building the `unit_tests` target still compiles the full `game` and `modules`
  libraries (it links them), so the **first** run is a heavy compile. Subsequent
  runs reuse the `ac-build-dev` volume + ccache and are fast — re-run the same
  command after a change to retest.
- `-DCMAKE_BUILD_TYPE=Release` keeps the build tree small (no debug info). Reclaim
  the space anytime with `docker volume rm <project>_ac-build-dev <project>_ac-ccache-dev`
  (after the container exits).
- The build is CPU-heavy; if the live server is busy, lower `-j` (e.g. `-j4`) to
  leave it headroom.

