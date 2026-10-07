# Yellowback in YecWallet — development notes

**Status (2026-10-06, the vault upgrade, upgrade plan P6; branch `upgrade/vault`):** the wallet
speaks `rpcversion` 5 only. Ycash Yellowback (YED) is the rule module of the Ycash vault primitive
(`UPGRADE_VAULT`, branch ID `6d5b7a31`): its rules are consensus, so the wallet shows the upgrade's
status (`yed_getinfo.upgrade`, `vault_getinfo`) where it showed activation, signalling and
enforcement, and the valve, sunset, abandonment and Sweep screens are gone with the fields they read.
No `ycash.conf` line enables Yellowback any more (the wallet writes none and offers no repair); a
`-32601` from `yed_getinfo` means the node has no vault upgrade or no YED attestor set. The rest of
this file describes the v2/v3 flows it grew from; where they differ, this section wins.

- **Claims are intents (U-23).** `yed_claim` burns the debt and moves the collateral, less the
  RED-5 residual, into a claimant intent; the fees come from the claimant's YEC, so the dialog's
  figure and `minOutZat` floor are `collateral - residual`. A claim must pay a transparent address
  (D-U1, refused locally). The new **Pending claims** page lists every CLAIMING vault's intents
  (`yed_listvaults CLAIMING`; `yed_listpositions` rows lack `intents` on both node lines, so the
  wallet merges them from there) with its release height; **Release** is `vault_release` (the
  claimant's intent with no recipient argument; the owner's residual with the owner's P2PKH script,
  `p2pkhScriptForKeyId(ownerKeyId)`).
- **Cancelled claims (U-24, D-U3).** A claim of this wallet whose vault is gone (`yed_getvault`
  answers `vault-not-found` after an attestor cancel re-created it under the cancel's txid) is
  listed on Pending claims with the sentence that its burn is not refunded.
- **Attestors are set members (P4-b).** The Attestors page shows this wallet's member keys of the
  YED attestor set (`set_getinfo`: current or dormant, last act, bond frozen) and the table's
  `lastAct` / `bondFrozen`; **Heartbeat** (`set_heartbeat`) replaces Revive; joining is
  `yed_registerattestor` (= `set_join`, one key for hot, bond and fee, D-U4; no flags). On a node
  whose wallet holds a current member key, Pending claims offers **Cancel wrong-price claim**
  (`vault_buildcancel`, `set_signcancel`, `vault_send`) behind a warning naming the lost burn and
  the equivocation risk. A signed cancel is kept (`yellowback/signedcancel/<outpoint>`) and only
  ever re-sent: two different signed spends of one intent are a provable equivocation.
- The primitive's RPCs are named once in `src/vaultrpc.h`; `tests/check-rpc-contract.py` learned
  `// optional` for fields the contract marks optional and its example omits.
- Trust text (Overview): a summary of the upgrade plan's §10 statement for Yellowback.

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

This file records the wallet-side baseline and the conventions the `feature/yellowback-price-attest` (v3) fork
follows (`feature/yellowback-sf`, the superseded v2, and `feature/digidollar`, the retired prototype,
are kept as records only, never as comparison bases). The node-side contract
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
| **Sweep** (retired in rpcversion 5; kept here as the v2 record) (Vaults row, ACTIVE while `yed_getinfo.abandoned`) | the dialog carries `I understand this leaves YED unbacked` verbatim and names `sweepBefore` | `yed_sweep <vaultTxid> "I understand this leaves YED unbacked" [to]` (L10) | `txid`, `hex` (copied to the clipboard for submission elsewhere), `collateralOut`, `to`, `unbackedCents` |

Refresh reads `yed_getinfo`, `yed_getstats`, `yed_getactivation`, `yed_getbalance`,
`yed_listpositions`, `yed_listclaimable` and `yed_listtransactions 200 0` on the stock
`Controller`'s block-changed branch and after every successful action.

**Mint limits (W16, W20).** The Mint gate distinguishes *paused* from *limited*.
`yed_getstats.mintableClasses` lists the term classes that can mint now, and `mintingAllowed` is
false whenever any halt bit is set or the supply cap is reached, so the wallet reads the two
together. Under a lone `GLOBAL_RATIO` halt (W16) or, since plan v3 revision 4, once
`yed_getinfo.supplyCapReached` is true with no halt bit (W20: the cap is soft above
`params.recapRatioBps`, 500 %), or both, minting is *limited* while `mintableClasses` is
non-empty: `YellowbackController::mintLimit()` explains which restriction applies (the supply
in circulation, the cap and its share of issued YEC value; the global ratio and its floor), the
Overview says "limited to class A", the cap row reads "$2,600.00 of $2,500.00 cap — class A
only" instead of a negative headroom, and the lock lengths of the other classes are greyed out.
`mintBlocker(cents, class)` lets a qualifying class through even when `supplyCents + cents`
exceeds the cap, and refuses the others by name; while the cap still has room, an amount that
would cross it is checked against the classes whose minimum ratio after the volatility
multiplier (`baseRatioBps × sigmaMultBps / 10⁴`) reaches the floor — class A always, class B at
≥ 1.25×. An empty `mintableClasses` (or any other halt bit) is *paused*, as before. A node from
before W20 has no `supplyCapReached`: the cap stays a ceiling there and the wallet behaves
exactly as it did. The abandonment banner says "about 30 days" (W21: `ABANDON_BLOCKS` = `GRACE`
on mainnet and testnet; regtest keeps 128 blocks).

**Hardening H5-a (rpcversion 4, 2026-10-05).** The wallet accepts `rpcversion` 4 only.
*Deadlines (H-9.2):* every ACTIVE vault states its lock and claim heights with estimated dates
(the table and the action line); from `lockHeight` the Vaults page offers **Renew** beside
Redeem — one confirmation covering both legs: `yed_redeem` at once, then, when
`yed_listpositions` reads the vault CLOSED, `yed_mint` of the same debt with the vault's own lock
length (or the shortest enabled one), funded from the transparent balance the redeem paid back
into and held to the `maxCollateralZat` the dialog showed; a mint that cannot be built then
(minting paused, price past the cap, the vault claimed instead) stops the renewal with a notice
and sends nothing else. From `claimHeight − 1 day` (1,152 blocks) the banner warns persistently
about the vault (a VOID one: release). The sunset warning of H-9.2 is dropped (upgrade plan §7).
*Plausibility (H-9.3):* before any confirmation the wallet recomputes from `yed_getinfo.params`
FEE-1, AFEE-1, `requiredZat` (and MINT-5's `4 · FEE_MIN` floor, which is what the vault locks
and what `maxCollateralZat` caps), `refHeight = tip − REF_LAG` (one block of slack),
`lockHeight = refHeight + lockBlocks`, `claimHeight = lockHeight + GRACE`, the class and its
ratio, and refuses to send on any mismatch; `yed_claim` carries `minOutZat` and `maxBurnCents`
(the debt plus at most the H4 sub-dollar remainder). *Mint gate (H-1, H-5):* with
`mintRequiresArmed` and the layer not ARMED the Mint page and Renew are paused by name;
disabled classes (empty term ranges) are not offered and the page says only class A can be
minted; MAX_MINT is $2,500 off regtest (H-12; the contract does not report it).

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
QT_QPA_PLATFORM=offscreen build/bin/nodecompat_test        # v4.5.0 / 6.20.0 call shapes (loopback mock ycashd)
QT_QPA_PLATFORM=offscreen build/bin/nodedatacheck_test     # the data directory upgrade check
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

The four devnet cases run only with `YELLOWBACK_DEVNET_DIR=<the devnet's --dir>` set; they read
`rpcuser`, `rpcpassword` and `rpcport` from `<dir>/node0/ycash.conf` (the same file the GUI reads
through `--conf`) and post JSON-RPC to node 0 directly, because the test has no `MainWindow` for
`Connection::doRPCSafe`. They expect the default `yellowback-devnet up` (vault upgrade active,
ARMED, three attestors in the YED attestor set) built from an `upgrade/vault` tree:

- `devnetEndToEnd`: mint → send → redeem.
- `devnetMintAndRenew`: mint, wait for the lock height, Renew from the Vaults page.
- `devnetClaimReleaseAndCancel`: two vaults minted at $50, the market crashed; one claim is
  released after `CLAIM_DELAY` on the Pending claims page, the other is cancelled by node 5's
  attestor wallet before it matures (the vault is ACTIVE again, the claimant's burn is lost).
- `devnetAttestedMintNoticeAndEmergencyClaim`: mints while ARMED, crashes the price, posts a claim
  notice and claims by the emergency clause.

Every mint, notice and claim is the two-step: the reply names only the carrier, and the test mines
a block at a time (`DevnetTransport::awaitTwoStep`) until the wallet's follow-up posts the result.
Each case starts from $50 with full price windows and a fresh bundle (`restoreMarket`,
`waitFresh`); the crash moves the attestors' price files and the pools' quotes together
(`DevnetTransport::setMarketPrice`, as `yellowback-devnet price` does). Run them in order on a
fresh devnet. Nothing leaves 127.0.0.1.

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

## Trying it on one laptop: the devnet

`ycash-dd/contrib/yellowback/devnet/yellowback-devnet` (the same script ships in `ycash6`) builds a
private regtest network on one machine with the vault upgrade active and leaves it running: eight
nodes (0 the user wallet, 1 a stock node, 2–4 quoting pools, 5–7 attestors), a funded user
wallet, the YED attestor set created and the three attestors joined to it, and the price layer
**ARMED**, so minting is open. The node must be built from an `upgrade/vault` tree. Nothing
leaves 127.0.0.1 and no mainnet sync happens.

```bash
cd ycash-dd
PY=../.venv/bin/python                                  # the workspace venv has the test framework's deps
$PY contrib/yellowback/devnet/yellowback-devnet up      # about two minutes; prints the wallet command
$PY contrib/yellowback/devnet/yellowback-devnet wallet  # launches the built wallet against node 0
```

Options of `up`: `--dir` (default `~/yb-devnet`), `--portseed` (default 7; pick another when
other regtest nodes run on the machine), `--bitcoind <path>` (default `src/ycashd`), `--price`,
`--force`, `--role {user,attestor,pool}` (leave one seat for you; see the devnet's own
`contrib/yellowback/devnet/README.md`). `up` prints the wallet command
(`yecwallet --conf <dir>/node0/ycash.conf --no-embedded`). From then on Mint, Send, Redeem, Claim,
Release (Pending claims) and, on an attestor's node, Cancel and Heartbeat work from the
Yellowback tab.

| Command | What it does |
|---|---|
| `yellowback-devnet mine 10 [node]` | mines 10 blocks, round-robin on the quoting pools (or on one node) |
| `yellowback-devnet price 45` | moves the YEC/USD price for the pools and every attestor together |
| `yellowback-devnet status` | nodes and ports, the upgrade, minting state, pools and attestors |
| `yellowback-devnet check` | exits 0 iff the upgrade is active, minting is allowed, the pools are eligible and the layer is ARMED |
| `yellowback-devnet cli -- yed_listpositions` | `ycash-cli` against node 0 (`--node 5` for an attestor) |
| `yellowback-devnet wallet --node 5` | opens the wallet on an attestor's node (Attestors page, Cancel wrong-price claim) |
| `yellowback-devnet down` | stops the nodes; `down --wipe` also deletes the directory |

A class-A vault unlocks 48 blocks after its mint on regtest. To redeem: mint, `mine 48`, select
the vault on the Vaults page, Redeem. To see a claim: mint, crash the price
(`price --shock=-70%`, then mine), claim the vault on the Claim page; the claim waits
`CLAIM_DELAY` (10 blocks on regtest) on the Pending claims page, where an attestor's wallet can
cancel it and anyone can release it afterwards. The wallet QTest's devnet cases do the same.

## Attaching the GUI to a regtest playground node (plan H2)

No new options were added. The stock options already do it:

```bash
build/bin/yecwallet --conf <tmpdir>/node0/ycash.conf --no-embedded
```

`--conf` makes the wallet read `rpcuser`, `rpcpassword` and `rpcport` from that file
(`src/connection.cpp`, `ConnectionLoader::autoDetectZcashConf`), and `--no-embedded` stops it
from starting its own `ycashd` (`src/main.cpp`, `noembeddedOption`). The playground's per-node
conf (`test_framework/util.py`) writes exactly those keys. The QTest target attaches the same way.

## Two node lines: v4.5.0 and 6.20.0 (ycash6 plan Phase 7)

One wallet serves both ycashd lines. The node version is read once at connect and kept on the
`Connection` (`nodeVersion`): `ConnectionLoader::refreshZcashdState` first asks `getrescaninfo`;
a node that answers it is a v4.5.0-line node and the version stays 0, so **every call goes out
exactly as before**. A node that does not know it (6.20.0: "Method not found") is asked
`getinfo`, and `getinfo.version` (6200050 for 6.20.0; v4.5.0 reports 4050050) selects the
6.20.0 shapes when it is at least 6200000. `src/nodecompat.h` holds every difference:

| Call | v4.5.0 (unchanged) | 6.20.0 |
|---|---|---|
| `z_importivk` | `ivk, "yes"/"no", height, zaddr` | `ivk, zaddr, "yes"/"no", height` (`ref/ycash6/src/wallet/rpcdump.cpp:1180`) |
| `importprivkey` | `key, "", rescan, height` (Ycash-only 4th argument) | `key, "", rescan`. The server enforces 1-3 (`rpc/common.h:126`); a rescan always starts at genesis, so **the start-height optimisation is lost for WIF imports** (the shielded imports keep it) |
| `getrescaninfo` before every call (`Connection::doRPCSafe`) | yes; calls are held back while it reports a rescan | not sent (removed RPC); calls are held back while the wallet's own import is rescanning |
| Rescan progress after an import | polls `getrescaninfo` | the last import of a batch carries `rescan=true` and the node rescans before it answers: a busy dialog with the elapsed time is shown, the wallet's other calls are held back, and the reply (or its error) closes it (`MainWindow::doImportWithSyncRescan`, `Controller::beginSyncRescan`) |
| File → Rescan | `rescanblockchain <height>` | `rescanblockchain` is gone; with the embedded ycashd the menu offers to add `rescan=1` to `ycash.conf` and close (the stock `-rescan`, from genesis); `Controller::setConnection` removes the line at the next connect, as it already did. With an external ycashd it says to restart ycashd with `-rescan` |
| Fast sync (new `ycash.conf`) | written as `ibdskiptxverification=1` (was `fastsync=1`; v4.5.0 treats the two identically, `ref/ycash/src/init.cpp:1078-1079`). The conf is written before any node runs, so this is the one change that is not version-gated | `ibdskiptxverification=1` (the only name 6.20.0 knows). Either name is read back, and both are removed once synced |
| New Sprout address | allowed, but the UI never asks for one (`addNewZaddr` is only called for Sapling) | refused after Canopy; nothing to hide. `NodeCompat::canCreateSprout` is the guard should an option be added |

`build/bin/nodecompat_test` checks all of it offline: the v4.5.0 shapes against the literal JSON
the wallet sent before, the 6.20.0 shapes, and what actually goes over the wire on each line through
a real `Connection`/`ZcashdRPC` and a loopback mock ycashd. Its `devnetImports` case imports a WIF
and a Sapling ivk made on node 1 into node 0 of a running devnet (either line) when
`YELLOWBACK_DEVNET_DIR` is set.

### Packaging: the embedded node and an external one

There is no separate "noembed" build. `build.sh --package --ycashd PATH` copies one ycashd into
the package (macOS: `yecwallet.app/Contents/MacOS/ycashd`); without `--ycashd` the package ships no
node. At run time the wallet always first connects to the node its `ycash.conf` names
(`ConnectionLoader::doAutoConnect`); only when that connection is refused, and `--no-embedded`
was not given, does it start the ycashd beside its own executable, with no arguments, so on the
default datadir and conf (`startEmbeddedZcashd`). Whichever node answers, the version is read at
connect ("Two node lines" above), so a running v4.5.0 node is driven with the v4.5.0 shapes even by
a package that bundles 6.20.0.

The rule: **the package bundles the 6.20.0 ycashd (`ycash6`)**; with `--no-embedded`, or whenever a
node is already running on the conf's port, the wallet drives whichever line answers. Verified
headless (`QT_QPA_PLATFORM=offscreen`, 2026-10-01, the development build with Homebrew Qt: the
static release build links only the `cocoa` platform plugin, so the packaged binary cannot run
offscreen): embedded 6.20.0 on a fresh regtest datadir ("ycashd 6200050: using the Ycash 6.20.0
RPC shapes", "ycashd is online"); `--no-embedded --conf` against a 6.20.0 devnet node (the same
two lines) and against a v4.5.0 devnet node ("ycashd is online", v4.5.0 shapes).

Risks of an embedded 6.20.0 node:

- **An existing v4.5.0 datadir is upgraded one way.** 6.20.0 cannot read the v4.5.0 block index
  (`LoadBlockIndex() : failed to read value`, "Please restart with -reindex to recover"). After a
  6.20.0 reindex, v4.5.0 refuses the datadir ("Error loading block database", or `block index
  inconsistency detected (post-Heartwood; hashLightClientRoot ... != hashChainHistoryRoot ...)`)
  until it reindexes in turn; the wallet.dat keys survive both ways. On mainnet that is a full
  reindex each way. Verified on regtest with both binaries. The wallet now warns first (below).

- **`ycash.conf` differences.** `fastsync` is `ibdskiptxverification` on 6.20.0 (handled, see the
  table above); Yellowback needs no `ycash.conf` key on either line: it is on wherever
  the vault upgrade and the YED attestor set are configured (on regtest `nuparams=6d5b7a31:<h>`
  and `yellowbackattestorset=<setid>`, the same keys on both lines). 6.20.0 keeps every zcashd-deprecated RPC the
  wallet calls enabled by default (`ref/ycash6/src/deprecation.h`, `DEFAULT_DENY_DEPRECATED` empty)
  and has no end-of-service height.
- **The startup version check** (`Controller::checkForUpdate`) compares `APP_VERSION` with the
  release tags of `YcashFoundation/yecwallet` on GitHub; it is not a node check. With
  `APP_VERSION` at 6.20.0, no upstream 4.x/5.x release is ever offered as an update.

### The upgrade warning (owner decision 2026-10-01)

**What a failed 6.20.0 start writes** (regtest, v4.5.0 datadir from `wt/backport` with 30 blocks,
then `ycash6/src/ycashd` on it; sha256 + mtime of every file before and after): `wallet.dat`,
`peers.dat`, `banlist.dat`, `fee_estimates.dat` and `yellowback/` are untouched (6.20.0 stops
before it loads the wallet); `blocks/index` and `chainstate` are rewritten only by LevelDB's
open-time recovery (the `.log` folded into an `.ldb` table, new `MANIFEST`, `LOG` → `LOG.old`),
`database/log.0000000001` appears and `debug.log` grows. v4.5.0 then starts on that directory
without a reindex, same tip, same addresses. So the failed start is reversible; the reindex is
the point of no return. The wallet still asks before it starts the node at all, so that Quit
leaves the directory byte-identical.

**When.** `ConnectionLoader::doAutoConnect`, after the connection to the conf's node was
refused and before the first `startEmbeddedZcashd`: never with `--no-embedded`, never when a
node already answers, never when the bundled `ycashd --version` (which reads no conf and no
datadir) reports a version below 6.20.0.

**Detection** (`src/nodedatacheck.{h,cpp}`, no UI, `tests/nodedatacheck_test.cpp`). The network
data directory comes from the conf (`datadir=`, `testnet=1` → `testnet3/`, `regtest=1` →
`regtest/`, `wallet=`). Then, in order:

| State | Condition | Warn |
|---|---|---|
| `Fresh` | no `blocks/index` (or an empty one) | no |
| `Marked` | `<netdir>/yecwallet-node-version` names 6.20.0 or later | no |
| `Current` | `debug.log`: the last run that loaded the block index was 6.20.0 or later | no |
| `Older` | `debug.log`: the last run that loaded the block index was older (4.4.x, 4.5.0) | yes |
| `Unknown` | `blocks/index` present, no marker, `debug.log` says nothing | yes, once |

"Loaded" means a `Ycash version vX` line followed, in the same run, by the ` block index NNNms`
line both lines print only after `LoadBlockIndex` succeeded (`ref/ycash/src/init.cpp:916,1760`,
`ref/ycash6/src/init.cpp:1018,2013`), so a failed 6.20.0 start does not count; the last 16 MiB of
`debug.log` are read. A definitive check would read a `CDiskBlockIndex` record's leading client
version out of LevelDB, which the wallet cannot do without a LevelDB dependency. Limits: a
directory whose `debug.log` was deleted warns once even if it is already 6.20.0. The marker
records the acceptance, not the upgrade: a marker beside a `debug.log` whose last index loader is
still older than 6.20.0 (the user quit before the reindex started, or put an older `blocks/` back
that v4.5.0 then loaded) is `Older` again and the dialog returns with a line saying the upgrade
did not complete (audit F-2); a restored directory whose `debug.log` was not restored with it is
not caught. With `--conf PATH` naming a file other than the default `ycash.conf`, the embedded
node is started with `-conf=PATH`, so it reads the conf the check inspected and the `reindex=1`
the wallet appended (audit F-10); with the default conf it is started without arguments as before.

**The dialog** says the node data is upgraded, older YecWallet/ycashd versions need a full
reindex to use it afterwards, the reindex takes hours on mainnet, and `wallet.dat` keys are kept;
for `Unknown` it adds that nothing changes if the directory is already 6.20.0. The informative
text names the directory, its size and the free space on that disk and says a full copy is the
only way back without a reindex, which the wallet does not make. Buttons: **Back up wallet.dat
and continue** (copies it to `wallet.dat.yecwallet-backup-<yyyyMMdd-HHmmss>` next to it; a failed
copy starts nothing), **Continue without backup**, **Quit** (default Escape; starts nothing,
writes nothing). On continue the wallet writes the marker, and for `Older` also appends
`reindex=1` to `ycash.conf`, so the first start reindexes instead of failing
(`Controller::setConnection` removes it once the node answers). The old reaction to the
"-reindex" stderr line stays as the fallback (the `Unknown` case, or a marker write that failed).

**Test hooks.** `YECWALLET_UPGRADE_ANSWER=backup|continue|quit` answers the dialog without
showing it, but only when `YECWALLET_TEST_ISOLATE` is also set (audit F-5): in a normal run the
variable is ignored and logged. `YECWALLET_TEST_ISOLATE=1` keeps QSettings in an INI file under
`$HOME/.yecwallet-test-settings` and the log, labels, sent-tx store and Yellowback subscriber
conf under `$HOME/.yecwallet-test-appdata` (`Settings::appDataLocation`); without it, on macOS
both go to the real `~/Library` whatever `HOME` says.

**Verified** 2026-10-01, development build (Homebrew Qt 6.11), `QT_QPA_PLATFORM=offscreen
--headless`, scratch `HOME` holding a copy of the v4.5.0 regtest datadir, `ycash6/src/ycashd`
beside the binary: `quit` → the wallet exits 0, no ycashd started, the datadir byte-identical
(sha256, mtime, size, listing); `backup` → backup byte-identical to the pre-upgrade wallet.dat,
marker written, 6.20.0 reindexes on its first start ("Reindexing finished"), "ycashd is online",
height 30, the same t- and z-addresses, `reindex` gone from the conf, and a second start reads
the marker and does not warn; `continue` → the same without the backup; `--no-embedded` and an
already running v4.5.0 node → no check. v4.5.0 on the upgraded directory: "Error loading block
database", then with `-reindex` the same addresses.

## Network detection (plan H3)

`Controller::getInfoThenRefresh` sets testnet mode from `getinfo.testnet`, which a regtest node
reports as `false`. The stock tabs still work on regtest (regtest transparent addresses share the
testnet prefixes and pass `Settings::isTAddress`), but a Yellowback address check keyed on that flag
would pick the mainnet `ye` prefix. The Yellowback code therefore takes the network from
`yed_getinfo.network` (`main` / `test` / `regtest` → `ye` / `yt` / `yr`) and never reads
`Settings::isTestnet()` for anything Yellowback-specific (`YellowbackController::network()`).

## Verification status of the fork

The Yellowback code compiles cleanly against the system Qt 6 (`cmake --build build`, no warnings
in the Yellowback files) and the QTest target passes under `QT_QPA_PLATFORM=offscreen`. Every
action — mint, send, redeem, renew, claim, release, attestor cancel, heartbeat — is covered
offline against the contract's example values, and the devnet cases run mint → send → redeem,
mint → renew, and claim → release / claim → attestor cancel against a fresh vault-upgrade devnet
(105 offline and 4 devnet cases green); the GUI attaches with `--conf --no-embedded`.

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
| `src/yellowbackrpc.h` | the RPC contract: every `yed_*` method name, result field, error identifier and displayed protocol constant; `RPC_VERSION = 5`; `tests/check-rpc-contract.py` parses its `// contract:` markers |
| `docs/yellowback-rpc-contract.json` | generated copy of the RPC contract (plan §4.5 / `ycash-dd/doc/yellowback-rpc.md`), written by the workspace `make spec`; the `wallet` CI job checks `yellowbackrpc.h` against it from Phase 7b |
| `src/yellowbackcontroller.{cpp,h}` | `YellowbackController`: all `yed_*` calls through `Connection::doRPCSafe`; availability (enabled, rpcversion, synced, healthy), cached info/stats/activation/balance, mint gate reasons, `classForLock`, `explainError`, `parseChangeFloor`, `setTransport` (the test's fake Connection). Driven from `Controller::setConnection`, the block-changed branch of `Controller::getInfoThenRefresh`, and `Controller::watchTxStatus` |
| `src/yellowbackmodels.{cpp,h}` | `YellowbackPosition` / `YellowbackClaimable` / `YellowbackTx` records, tolerant JSON readers, formatting helpers, the positions, claimable and transactions table models |
| `src/yellowbacktab.{cpp,h,ui}` + `src/yellowback{overview,receive,send,mint,positions,claim,transactions,redeem,attestors,settings}.ui` | the Yellowback tab (index 4 of the main tab bar, after Transactions) and its eleven sub-pages (Overview, Receive, Send, Mint, Vaults, Claim, Pending claims, Transactions, Redeem, Attestors, Settings); the action flows and their confirmation dialogs (`confirmFn`/`noticeFn` for the test) |
| `src/vaultrpc.h` | rpcversion 5: the vault primitive RPCs the wallet calls (`set_getinfo`, `set_heartbeat`, `vault_release`, `vault_buildcancel`, `set_signcancel`, `vault_send`, `vault_getinfo`) |
| `src/connection.{cpp,h}` | rpcversion 5: `createZcashConf` no longer writes `experimentalfeatures=1` / `yellowback=1` and the conf repair is gone (no flag gates YED) |
| `src/settings.{cpp,h}` | `yellowback/unitcents`, `yellowback/advanced`, `yellowback/backuppending`; `getYellowbackRpcVersion()` (the prototype's `yellowback/endpoints` key is no longer read) |
| `src/controller.{cpp,h}`, `src/mainwindow.{cpp,h}` | creation and the three hooks; tab registration; `setEZcashd` now finds the console tab by `indexOf` because index 4 is taken |
| `src/nodedatacheck.{cpp,h}`, `tests/nodedatacheck_test.cpp` | the one-way data directory upgrade check and its QTest (see "The upgrade warning"); the dialog is `ConnectionLoader::confirmNodeDataUpgrade` in `src/connection.cpp` |
| `src/nodecompat.h`, `tests/nodecompat_test.cpp` | the v4.5.0 / 6.20.0 call shapes (see "Two node lines") and their QTest |
| `CMakeLists.txt`, `tests/yellowbacktab_test.cpp`, `tests/check-rpc-contract.py` | source registration; the optional `yellowback_test` QTest target (`find_package(Qt6 OPTIONAL_COMPONENTS Test)`, skipped when `QT_STATIC`); the contract checker |
