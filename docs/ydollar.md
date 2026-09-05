# YDollar in YecWallet — development notes

This file records the wallet-side baseline (plan §6 Phase 5b, "Phase 0 (wallet side)") and the
conventions the `feature/digidollar` fork follows. The node-side contract lives in
`ycash-dd/doc/ydollar-rpc.md`; every RPC method and result field this wallet depends on is
listed once in `src/ydollarrpc.h`.

## Phase 0 baseline

| Item | Result |
|---|---|
| Host | macOS 26.0 (Darwin 25.0.0), arm64, Apple clang 17.0.0 |
| CMake | **not installed** (`which cmake` → nothing; no `/opt/homebrew/Cellar/cmake`) |
| Qt 6 (system) | **not installed** (no `/opt/homebrew/Cellar/qt*`, no `~/Qt`) |
| Qt 6 (static, `build.sh`) | not attempted: `build.sh` fetches and builds Qt 6.5.8 (`build.sh:57`) and needs CMake too |
| `Qt6::Test` | unknown on this host. Homebrew's `qt` formula ships `qtbase`, which includes the Test module, so a system-Qt build would have it; the static release Qt never does (`scripts/build-qt.sh` passes `-no-feature-testlib`, plan H1) |
| Build of the unmodified app | **blocked** by the two missing tools; nothing was installed (see the rule below) |

Nothing was `brew install`ed. The commands that would build the development configuration on
this host, once the tools are present:

```bash
brew install cmake ninja qt            # Homebrew qt = Qt 6 with qtbase (Test), qttools (LinguistTools)
cd yecwallet-dd
cmake -S . -B build -G Ninja \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# QTest target (only configured when Qt6::Test is found, plan H1):
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

The release configuration is unchanged: `./build.sh --package` builds the static Qt 6.5.8 and
links the wallet against it (`-no-feature-testlib`, so the test target is skipped there).

## Attaching the GUI to a regtest playground node (plan H2)

No new options were added. The stock options already do it:

```bash
build/bin/yecwallet --conf <tmpdir>/node0/ycash.conf --no-embedded
```

`--conf` makes the wallet read `rpcuser`, `rpcpassword` and `rpcport` from that file
(`src/connection.cpp`, `ConnectionLoader::autoDetectZcashConf`), and `--no-embedded` stops it
from starting its own `ycashd` (`src/main.cpp`, `noembeddedOption`). The playground's per-node
conf (`test_framework/util.py`) writes exactly those keys. The QTest target attaches the same way.

## Network detection (plan H3)

`Controller::getInfoThenRefresh` sets testnet mode from `getinfo.testnet`, which a regtest node
reports as `false`. The stock tabs still work on regtest (regtest transparent addresses share the
testnet prefixes and pass `Settings::isTAddress`), but a YDollar address check keyed on that flag
would pick the mainnet `yd` prefix. The YDollar code therefore takes the network from
`yd_getinfo.network` (`main` / `test` / `regtest` → `yd` / `yt` / `yr`) and never reads
`Settings::isTestnet()` for anything YDollar-specific (`YDollarController::network()`).

## Verification status of the fork

The YDollar code in this fork was written without a compiler or Qt on the development host (see
the table above). It has been reviewed for C++20 / Qt 6 correctness by hand, but it has **not**
been compiled or run. The first build on a host with CMake and Qt 6 is expected to surface
ordinary compile errors; none of the design depends on anything unverified beyond that.

## Conventions

- Naming: `YDollar` / `ydollar` / `YD` everywhere; `DigiDollar` appears only in comments citing
  `ref/digibyte` files.
- Amounts: integer cents in code; `YDollarController::formatCents` renders them.
- Heights: shown with an estimated date at 75 s per block, labelled as an estimate.
- Copy: the wallet never describes YDollar as trustless or shielded (plan §8.1). It says
  "transparent" and "federated" where a user might expect otherwise.
- Errors: node error strings are stable identifiers and are always shown verbatim.
