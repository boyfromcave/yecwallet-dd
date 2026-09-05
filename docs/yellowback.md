# Yellowback in YecWallet — development notes

This file records the wallet-side baseline (plan §6 Phase 5b, "Phase 0 (wallet side)") and the
conventions the `feature/digidollar` fork follows. The node-side contract lives in
`ycash-dd/doc/yellowback-rpc.md`; every RPC method and result field this wallet depends on is
listed once in `src/yellowbackrpc.h`.

## Build status

| Item | Phase 0 (first session) | Now |
|---|---|---|
| Host | macOS 26.0 (Darwin 25.0.0), arm64, Apple clang 17.0.0 | same |
| CMake | not installed | `/opt/homebrew/bin/cmake` (+ ninja) |
| Qt 6 (system) | not installed | Homebrew `qt` at `/opt/homebrew/opt/qt` (Qt 6, with `Qt6::Test`) |
| Qt 6 (static, `build.sh`) | not attempted | still not attempted (release packaging only) |
| Build of the fork | blocked | **compiles**: `build/bin/yecwallet.app` and `build/bin/yellowback_test` |
| `yellowback_test` (QTest, offscreen) | not built | **passes** (5 cases, including the frozen-contract case) |

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

The Yellowback code was first written without a compiler or Qt on the host; it now **compiles
cleanly** against the system Qt 6 (`cmake --build build`, no warnings in the Yellowback files)
and the QTest target passes under `QT_QPA_PLATFORM=offscreen`. What has *not* been done: running
the GUI against a live regtest node (plan H2), so every `yed_*` reply shape is verified against
the contract document and the node's `src/rpc/yellowback*.cpp` by reading, not by a round trip.

## Reconciliation with the frozen contract (rpcversion 1)

`ycash-dd/doc/yellowback-rpc.md` was frozen after the wallet was first written. The wallet was
diffed against it method by method (and against `ycash-dd/src/rpc/yellowbackwallet.cpp` /
`yellowback.cpp` for exact strings) and changed on every mismatch; the node was not touched.

| Area | Wallet assumed | Contract says | Wallet change |
|---|---|---|---|
| `/cosign` success body | `{hex}` (+ optional `signatures`) | `{hex, quorumSignatures, k, complete}` | reads `quorumSignatures` to count progress (falls back to the hex growing), `complete` to stop early, `k` overrides the roster's k |
| transient co-signer refusal | error text contains `RED-0` or `RED-2` | error text **ends with `(transient)`**, or JSON `transient: true`; 429 rate-limit is transient | `isTransientRefusal` matches the suffix only; `transient` flag still honoured |
| submit deadline | `expiryHeight - 3`, computed in the wallet; expired when `height >= deadline` | `yed_redeem.deadlineHeight = expiryHeight - 3 - 1`, **inclusive** ("submit by this height") | pending map stores the node's `deadlineHeight`; expiry check is `height > deadline`; the wallet-side fallback is `expiry - EXPIRING_SOON - 1`; `REDEEM_DEADLINE` is 36 |
| `yed_redeem` result | `hex, vault, roster, requiredBurnCents, expiryHeight` | also `burnCents, changeCents, deadlineHeight`, `roster{index,k,n,pubkeys[]}` | shows `burnCents` (what the tx burns) rather than `requiredBurnCents` |
| `yed_submitredeem` result | `txid` | `txid, quorumSignatures` | shown in the final page |
| `yed_estimatecollateral` | always `requiredZat` | `requiredZat` **null** with `error: bad-oracle-price` (or `collateral-out-of-range`, node-only); `lockHeight` and `unlockHeight` both present | handles the null + `error` case as "no estimate", mint button stays disabled |
| `yed_getstats` | no `lastBreachHeight`; `supplyCapCents` optional | `supplyCapCents` always present (0 = none), `priceHeight`/`priceAge`/`lastBreachHeight`/`mintFrozenUntil` are `-1` when undefined | field added to the header; cap test uses `0 = none` |
| `yed_getprotectionstatus` | not used | exists: `mintingAllowed`, `dca.band`, `err.active/burnMultiplierBps`, `volatility.frozenUntil` | fetched on every refresh; `mintBlocker` and the Overview's health/ERR lines prefer it over `yed_getstats` |
| `yed_getinfo.params` | not used; limits compiled in | `minMintCents, maxMintCents, minOutputCents, mintWindow, mintEvalLag, tiers[]…` | `YellowbackController::minMintCents()` etc. read them, with the compiled-in values as fallback; the tier table (`YellowbackFormat::tierName/tierRatio`) is still compiled in |
| `yed_listpositions` | no `pending` | `pending`, `mintHeight`, `voidReason`, `closeHeight`, `closingTxid`, `burnedCents` | all read; `pending` from the node drives the Redeem/Abort buttons alongside the wallet's own map (survives a wallet restart) |
| `yed_listtransactions` expired rows | `expired: true` only | `height: -1`, `verdict: "expired"`, `expired: true`; and the node's expired rows carry the **payload** type name (`transfer`), which is not in the contract's `mint\|send\|receive\|burn\|redeem` list | `expired` also set from the verdict; `transfer` rendered as "Sent" |
| C20 change floor | matched the word `change` | the message carries `(C20)` and names the workable amounts | matches `C20`; the node's amounts are shown instead of recomputed |
| index unhealthy | only via `yed_getinfo.healthy` | every other command fails with `-1` "yellowback index unhealthy: …; restart with -reindex-yellowback" | any such error takes the tab down immediately |
| locked wallet | not handled | `walletpassphrase` in the message | a hint is appended on mint/send |
| `yed_mint` result | `txid, vault, lockHeight, collateralZat` | also `evalHeight, expiryHeight, ownerKeyId, warning` | `warning` and `expiryHeight` shown |
| addresses | `ye`/`yt`/`yr` by `yed_getinfo.network` | same | no change |
| `-32601` Method not found | matched | same | no change |

Points the wallet **cannot** use exactly as specified, for the node side to consider:

- The contract lists `yed_listtransactions.type` as `mint|send|receive|burn|redeem`, but an
  expired transfer row is emitted with `type: "transfer"` (`yellowbackwallet.cpp`, the
  `PayloadTypeName` branch). The wallet tolerates it; the contract or the node should pick one.
- `yed_estimatecollateral` can return `error: "collateral-out-of-range"`, which the contract
  does not list (only `bad-oracle-price`). The wallet shows either verbatim.
- The wallet's tier names and ratios (1 h / 30 d / 90 d / 180 d / 1 y at 1000–300 %) are
  compiled in. `yed_getinfo.params.tiers[{tier,blocks,ratioPct}]` carries the ratios and lock
  lengths, so a future revision should render from it; nothing in the contract blocks that.

## Conventions

- Naming: `Yellowback` for the system, `YED` for amounts, `yed_*` RPCs (workspace AGENTS.md rule 6); `DigiDollar` appears only in comments citing
  `ref/digibyte` files.
- Amounts: integer cents in code; `YellowbackController::formatCents` renders them.
- Heights: shown with an estimated date at 75 s per block, labelled as an estimate.
- Copy: the wallet never describes Yellowback as trustless or shielded (plan §8.1). It says
  "transparent" and "federated" where a user might expect otherwise.
- Errors: node error strings are stable identifiers and are always shown verbatim.

## Where the code is

| File | What |
|---|---|
| `src/yellowbackrpc.h` | the RPC contract: every `yed_*` method name, result field, error identifier, `/cosign` shape and displayed protocol constant; `RPC_VERSION = 1` |
| `src/yellowbackcontroller.{cpp,h}` | `YellowbackController`: all `yed_*` calls through `Connection::doRPCSafe`; availability (enabled, rpcversion, synced, healthy), cached info/stats/balance, mint gate reasons, pending redemptions. Driven from `Controller::setConnection`, the block-changed branch of `Controller::getInfoThenRefresh`, and `Controller::watchTxStatus` |
| `src/yellowbackmodels.{cpp,h}` | `YellowbackPosition` / `YellowbackTx` records, tolerant JSON readers, formatting helpers, `YellowbackPositionsModel`, `YellowbackTxModel` |
| `src/yellowbacktab.{cpp,h,ui}` + `src/yellowback{overview,receive,send,mint,positions,transactions,redeem,settings}.ui` | the Yellowback tab (index 4 of the main tab bar, after Transactions) and its eight sub-pages |
| `src/yellowbackredeemwizard.{cpp,h}` | the redemption wizard: `yed_redeem` → operator `/cosign` POSTs → `yed_submitredeem`, with retry, countdown and abort |
| `src/connection.{cpp,h}` | `createZcashConf` writes `experimentalfeatures=1` / `yellowback=1`; `Connection::offerYellowbackConfRepair` appends them to an existing conf |
| `src/settings.{cpp,h}` | `yellowback/endpoints`, `yellowback/unitcents`, `yellowback/advanced`, `yellowback/backuppending`; `getYellowbackRpcVersion()` |
| `src/controller.{cpp,h}`, `src/mainwindow.{cpp,h}` | creation and the three hooks; tab registration; `setEZcashd` now finds the console tab by `indexOf` because index 4 is taken |
| `CMakeLists.txt`, `tests/yellowbacktab_test.cpp` | source registration; the optional `yellowback_test` QTest target (`find_package(Qt6 OPTIONAL_COMPONENTS Test)`, skipped when `QT_STATIC`) |

## Operator `/cosign` request shape used by the wizard

As the coordinator (`ycash-dd/contrib/yellowback/yellowback_fed.py`) implements it:
`POST <endpoint>/cosign`, body `{"hex": "<owner-signed or partially co-signed hex>"}`,
`Content-Type: application/json`, 30-second transfer timeout. A 2xx reply is
`{"hex", "quorumSignatures", "k", "complete"}` (a plain-text hex body is still accepted).
A refusal is 409 `{"error": "<RED-n: ... [(transient)]>", "transient": bool}`, a rate limit is
429 with `transient: true`; a missing `transient` falls back to the `(transient)` suffix, and a
network-level failure with no HTTP status is treated as transient too. Operators are contacted
one at a time in the configured order, each receiving the hex the previous one returned, until
`quorumSignatures >= k` or `complete`.
