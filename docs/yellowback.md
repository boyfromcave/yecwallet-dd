# Yellowback in YecWallet — development notes

**Status (2026-09-10, Yellowback v2, Phase 7b-b):** the wallet talks to the miner-enforced
Yellowback v2 node over `rpcversion 2` (`docs/plans/yellowback-v2-development-plan.md` in the
workspace, §4.8 and Phase 7b). Phase 0 removed the federation prototype (the co-signing redemption
wizard, the operator-endpoint settings, `yed_getroster` / `yed_submitredeem` / `yed_abortredeem`).
Phase 7b-a bumped `RPC_VERSION` to 2 and built the node-context screens (status banner, Overview,
Vaults, Claim list, Transactions). Phase 7b-b wired the five spending actions — Mint, Send,
Redeem/Release, Claim, Sweep — each as one confirmation dialog and one `yed_*` call. The generated
RPC contract this wallet is checked against is `docs/yellowback-rpc-contract.json` (written by the
workspace's `make spec`, never edited here); `tests/check-rpc-contract.py` asserts that every field
`src/yellowbackrpc.h` reads is in it.

This file records the wallet-side baseline and the conventions the `feature/yellowback-sf` fork
follows (`feature/digidollar` is the retired prototype, kept as a record). The node-side contract
lives in `ycash-dd/doc/yellowback-rpc.md`; every RPC method and result field this wallet depends
on is listed once in `src/yellowbackrpc.h`.

## The v2 flow, screen by screen

The wallet never talks to anything but the local node, and never holds key material: every
transaction is built, checked against the enforcement rules, signed and committed by `ycashd`.
The wallet validates what it can locally, shows one confirmation with the figures the node
reported, makes one call, and shows the node's result.

| Action | Before the confirmation | The call | After |
|---|---|---|---|
| **Mint** (Mint page) | amount in `[MIN_MINT, MAX_MINT]`, MINTPOL-1 gate from `yed_getstats` (`haltMask` names, `mintingAllowed`, cap headroom), class derived from the lock length (`params.classes`), a fresh `yed_estimatecollateral <cents> <lockBlocks>` (`requiredZat`, `termClass`, `lockHeight`, `claimHeight`, `minRatioBps`, `baseRatioBps`, `sigmaMultBps`, `pMint`, `refHeight`), the enforcement fee and payee from `yed_getfeepayee <refHeight> <requiredZat>` (`feeZat`, `default.payoutAddress`, `preferred`; `fee-no-eligible-payee` is shown as "none") | `yed_mint <cents> <lockBlocks> [from]` (`from` = a `ys1…` funding address, plan I2) | `txid`, `vault`, `termClass`, `lockHeight`, `claimHeight`, `collateralZat`, `feeZat`, `payee`, `fundedFrom`, `warning`; the wallet.dat backup nag is raised |
| **Send** (Send page) | prefix check (`ye`/`yt`/`yr` from `yed_getinfo.network`), `s1…`/`ys…`/`z…` refused locally, amount ≥ `MIN_OUTPUT`, confirmed balance, `yed_validateaddress` (`isvalid`, `reason`) | `yed_send <address> <cents>` | `txid`, `changeCents`, `expiryHeight`; a `change-floor` refusal's two workable amounts ("send N cents … or at most M cents") are parsed into the hint |
| **Redeem** (Vaults row, Redeem page) | ACTIVE at or past `lockHeight`, `mintedCents` ≤ confirmed YED, fee and payee from `yed_getfeepayee <tip − refLag> <collateralZat>`, destination (Redeem page: a fresh own address, or one of the wallet's `s1…`/`ys1…` addresses) | `yed_redeem <vaultTxid> [to]` | `txid`, `burnedCents`, `feeZat`, `payee`, `collateralOut`, `to` |
| **Release** (Vaults row, VOID) | VOID at or past `lockHeight`; the dialog says no YED is burned and no fee is paid, and names `sweepBefore` (= `claimHeight`) | the same `yed_redeem <vaultTxid>` (L14) | `burnedCents = 0`, `feeZat = 0`, `payee = null` |
| **Claim** (Claim page) | a `yed_listclaimable` row (`vault`, `ownerAddress`, `collateralZat`, `mintedCents`, `feeZat`, `claimHeight`, `underwaterAt`, `pClaim`); `mintedCents` ≤ confirmed YED | `yed_claim <vaultTxid> [to]` | the `yed_redeem` shape |
| **Sweep** (Vaults row, ACTIVE while `yed_getinfo.abandoned`) | the dialog carries `I understand this leaves YED unbacked` verbatim and names `sweepBefore` | `yed_sweep <vaultTxid> "I understand this leaves YED unbacked" [to]` (L10) | `txid`, `hex` (copied to the clipboard for submission elsewhere), `collateralOut`, `to`, `unbackedCents` |

Refresh reads `yed_getinfo`, `yed_getstats`, `yed_getactivation`, `yed_getbalance`,
`yed_listpositions`, `yed_listclaimable` and `yed_listtransactions 200 0` on the stock
`Controller`'s block-changed branch and after every successful action.

**Errors.** Node error strings are stable identifiers and are always shown verbatim
(`yed_x failed: <message>`); `YellowbackController::explainError` appends what the identifier
means for `yellowback-unhealthy`, `change-floor`, `not-a-yellowback-address`, `insufficient-yed`,
`vault-locked`, `vault-not-active`, `vault-not-owned`, `vault-not-found`, `sweep-not-abandoned`,
`sweep-acknowledgement-missing`, `claim-not-yet`, `claim-not-underwater`, the six `mintpol-*`,
`mint-unsatisfiable`, `mint-bad-lock`, `mempool-check-failed:<verdict>` and a locked wallet. A node
that lacks `yed_claim` or `yed_sweep` answers JSON-RPC `-32601`; the dialog reports that the node
does not offer the command. `yellowback-unhealthy` from any call takes the whole tab down at once.

**Copy.** The wallet never describes Yellowback as "trustless" (CI: `grep -rn 'trustless' src/`)
and never mentions a federation; what enforcement means is stated as in plan §8.1 — every Ycash
node enforces the lock height and that only the owner's key spends before the claim height; the
mining pools running the module enforce that collateral is released only against the burn of the
vault's debt. The Overview and every confirmation dialog are checked for this by the QTest
(`Harness::copyIsClean`).

## Testing

```bash
cd yecwallet-dd
cmake -S . -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build build
QT_QPA_PLATFORM=offscreen build/bin/yellowback_test        # offline cases; the devnet cases QSKIP
python3 tests/check-rpc-contract.py                        # yellowbackrpc.h vs docs/yellowback-rpc-contract.json
grep -rn 'trustless' src/ | { ! grep .; }
```

The offline cases (plan §4.8, N28) feed canned `yed_*` replies — the contract's example values —
through `YellowbackController::feed()` for the screens and through
`YellowbackController::setTransport()` (a fake `Connection`) for the dialogs, and capture the
confirmation and notice copy through `YellowbackTab::confirmFn` / `noticeFn`. They cover the
banner for every activation state, the Overview, the Vaults rows (ACTIVE, VOID, abandoned,
CLOSED, CLAIMED), the claim list, the transaction types, class derivation for lock lengths
47/48/96/97/144/145/240/241, and each dialog with its result and its error identifiers.

The two devnet cases (`devnetEndToEnd`: mint → send → redeem; `devnetClaimAndSweep`: sweep
under a forced abandonment, then claim when a claimable vault exists) run only with
`YELLOWBACK_DEVNET_DIR=<the devnet's --dir>` set; they read `rpcuser`, `rpcpassword` and
`rpcport` from `<dir>/node0/ycash.conf` (the same file the GUI reads through `--conf`) and post
JSON-RPC to node 0 directly, because the test has no `MainWindow` for `Connection::doRPCSafe`.
`devnetClaimAndSweep` also QSKIPs while the node lacks `yed_claim`/`yed_sweep`. Note that the
devnet's per-node conf uses the test framework's regtest credentials; nothing leaves 127.0.0.1.

## Build status

| Item | Phase 0 (first session) | Now |
|---|---|---|
| Host | macOS 26.0 (Darwin 25.0.0), arm64, Apple clang 17.0.0 | same |
| CMake | not installed | `/opt/homebrew/bin/cmake` (+ ninja) |
| Qt 6 (system) | not installed | Homebrew `qt` at `/opt/homebrew/opt/qt` (Qt 6, with `Qt6::Test`) |
| Qt 6 (static, `build.sh`) | not attempted | **done** 2026-09-05: `bash build.sh macos-arm64 --package --ycashd ../ycash-dd/src/ycashd` builds static Qt 6.5.8 and produces `artifacts/macos-arm64-yecwallet-v4.5.0.dmg` with `ycashd` inside the bundle (see "Release build on macOS" below) |
| Build of the fork | blocked | **compiles**: `build/bin/yecwallet.app` and `build/bin/yellowback_test` |
| `yellowback_test` (QTest, offscreen) | not built | **passes** (54 offline cases at Phase 7b-b; the two devnet cases QSKIP without `YELLOWBACK_DEVNET_DIR`) |

Nothing was `brew install`ed by an agent session; the tools appeared on the host between the
two sessions. The development configuration is built with:

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

## Release build on macOS

```bash
brew install cmake ninja                       # Xcode Command Line Tools required
cd yecwallet-dd
bash build.sh macos-arm64 --package --ycashd ../ycash-dd/src/ycashd
```

`build.sh` compiles a static Qt 6.5.8 into `deps/` the first time (about 20 minutes on an
Apple Silicon laptop; skipped afterwards), builds the wallet against it, verifies no Qt dylib
is referenced, copies the given `ycashd` into `yecwallet.app/Contents/MacOS/` (the wallet starts
the node it finds beside its own executable, so `--package` without `--ycashd` ships a wallet with
no node and warns), and writes `artifacts/macos-arm64-yecwallet-v<version>.dmg` with the app and
an Applications shortcut. The `--ycashd` binary must match the target architecture; the script
checks with `lipo`.

Two host facts found on 2026-09-05 (macOS 26, Command Line Tools 26.2):

- **Apple removed the AGL framework from the macOS 26 SDK, and Qt 6.5 links it.** Against the
  default SDK the Qt build fails with `ld: framework 'AGL' not found`. `build.sh` therefore picks
  the newest installed SDK that still ships `AGL.framework` (here `MacOSX15.4.sdk`, which the
  Command Line Tools keep alongside the current one) and exports `SDKROOT` for both the Qt and the
  wallet build; set `SDKROOT` yourself to override. The resulting binaries record SDK 15.4 and a
  minimum macOS of 11.0. Moving to a Qt release that no longer links AGL (6.8 or later) removes
  the need.
- The package is **ad-hoc signed** (`codesign` shows `Signature=adhoc`, no team). Gatekeeper
  treats it as from an unidentified developer: right-click → Open on first launch, or
  `xattr -d com.apple.quarantine` on the app. Developer ID signing and notarization are a
  separate step that needs an Apple Developer account.

## Trying it on one laptop: the devnet (v2)

`ycash-dd/contrib/yellowback/devnet/yellowback-devnet` builds a private Yellowback v2 network
on one machine and leaves it running: five regtest nodes (0 the user wallet, 1 a stock node,
2–4 pools with payout addresses and mock price quotes), a funded user wallet, and enough
signalling blocks that Yellowback is active and minting is open. Nothing leaves 127.0.0.1 and no
mainnet sync happens.

```bash
cd ycash-dd
PY=../.venv/bin/python                                  # the workspace venv has the test framework's deps
$PY contrib/yellowback/devnet/yellowback-devnet up      # about a minute; prints the wallet command
$PY contrib/yellowback/devnet/yellowback-devnet wallet  # launches the built wallet against node 0
```

Options of `up`: `--dir` (default `~/yb-devnet`), `--portseed` (default 7; pick another when
other regtest nodes run on the machine), `--bitcoind <path>` (default `src/ycashd`), `--price`,
`--force`. `up` prints the wallet command (`yecwallet --conf <dir>/node0/ycash.conf --no-embedded`).
From then on Mint, Send, Redeem and Release work end to end from the Yellowback tab.

| Command | What it does |
|---|---|
| `yellowback-devnet mine 10 [node]` | mines 10 blocks on node 0 (untagged) or on a pool node (tagged, carrying its quote) |
| `yellowback-devnet price 45` | changes the mock price the pools quote; mine pool blocks to publish it |
| `yellowback-devnet status` | nodes and ports, activation, minting state, each pool's registration and eligibility |
| `yellowback-devnet check` | exits 0 iff active, minting allowed, all pools eligible and node 0 funded |
| `yellowback-devnet cli -- yed_listpositions` | `ycash-cli` against node 0 (`--node 2` for a pool) |
| `yellowback-devnet down` | stops the nodes; `down --wipe` also deletes the directory |

A class-A vault unlocks 48 blocks after its mint on regtest (classes A 48–96, B 97–144,
C 145–240 blocks). To redeem: mint, `mine 48`, select the vault on the Vaults page, Redeem.
To see Sweep: mine untagged blocks on node 0 until fewer than half of a window signal and the
enforcement halt has held for `abandonBlocks` (128) — the banner then says "abandoned" and every
ACTIVE row offers Sweep. The wallet QTest's devnet cases do the same through the RPC.

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
testnet prefixes and pass `Settings::isTAddress`), but a Yellowback address check keyed on that flag
would pick the mainnet `ye` prefix. The Yellowback code therefore takes the network from
`yed_getinfo.network` (`main` / `test` / `regtest` → `ye` / `yt` / `yr`) and never reads
`Settings::isTestnet()` for anything Yellowback-specific (`YellowbackController::network()`).

## Verification status of the fork

The Yellowback code compiles cleanly against the system Qt 6 (`cmake --build build`, no warnings
in the Yellowback files) and the QTest target passes under `QT_QPA_PLATFORM=offscreen`. The
mint → send → redeem flow has been run against the v2 devnet through the QTest's devnet case and
the GUI attached with `--conf --no-embedded`; the claim and sweep dialogs are verified offline
against the contract's example values until the node ships `yed_claim` / `yed_sweep`.

## Conventions

- Naming: `Yellowback` for the system, `YED` for amounts, `yed_*` RPCs (workspace AGENTS.md rule 6); `DigiDollar` appears only in comments citing
  `ref/digibyte` files.
- Amounts: integer cents in code; `YellowbackFormat::cents` renders them.
- Heights: shown with an estimated date at 75 s per block, labelled as an estimate.
- Copy: the wallet never describes Yellowback as trustless or shielded (plan §4.8, §8.1; the CI
  grep `grep -rn 'trustless' src/`). It says "transparent" where a user might expect otherwise,
  and states what enforcement means as §8.1 does.
- Errors: node error strings are stable identifiers and are always shown verbatim.

## Where the code is

| File | What |
|---|---|
| `src/yellowbackrpc.h` | the RPC contract: every `yed_*` method name, result field, error identifier and displayed protocol constant; `RPC_VERSION = 2`; `tests/check-rpc-contract.py` parses its `// contract:` markers |
| `docs/yellowback-rpc-contract.json` | generated copy of the RPC contract (plan §4.5 / `ycash-dd/doc/yellowback-rpc.md`), written by the workspace `make spec`; the `wallet` CI job checks `yellowbackrpc.h` against it from Phase 7b |
| `src/yellowbackcontroller.{cpp,h}` | `YellowbackController`: all `yed_*` calls through `Connection::doRPCSafe`; availability (enabled, rpcversion, synced, healthy), cached info/stats/activation/balance, mint gate reasons, `classForLock`, `explainError`, `parseChangeFloor`, `setTransport` (the test's fake Connection). Driven from `Controller::setConnection`, the block-changed branch of `Controller::getInfoThenRefresh`, and `Controller::watchTxStatus` |
| `src/yellowbackmodels.{cpp,h}` | `YellowbackPosition` / `YellowbackClaimable` / `YellowbackTx` records, tolerant JSON readers, formatting helpers, the positions, claimable and transactions table models |
| `src/yellowbacktab.{cpp,h,ui}` + `src/yellowback{overview,receive,send,mint,positions,transactions,redeem,settings}.ui` | the Yellowback tab (index 4 of the main tab bar, after Transactions) and its nine sub-pages; the five action flows and their confirmation dialogs (`confirmFn`/`noticeFn` for the test) |
| `src/connection.{cpp,h}` | `createZcashConf` writes `experimentalfeatures=1` / `yellowback=1`; `Connection::offerYellowbackConfRepair` appends them to an existing conf |
| `src/settings.{cpp,h}` | `yellowback/unitcents`, `yellowback/advanced`, `yellowback/backuppending`; `getYellowbackRpcVersion()` (the prototype's `yellowback/endpoints` key is no longer read) |
| `src/controller.{cpp,h}`, `src/mainwindow.{cpp,h}` | creation and the three hooks; tab registration; `setEZcashd` now finds the console tab by `indexOf` because index 4 is taken |
| `CMakeLists.txt`, `tests/yellowbacktab_test.cpp`, `tests/check-rpc-contract.py` | source registration; the optional `yellowback_test` QTest target (`find_package(Qt6 OPTIONAL_COMPONENTS Test)`, skipped when `QT_STATIC`); the contract checker |
