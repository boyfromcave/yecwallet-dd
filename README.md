YecWallet is the official wallet for [Ycash](https://www.ycash.xyz) that runs on Linux, Windows and macOS.

## Ycash vaults and Yellowback on this branch

This fork of YecWallet is the desktop wallet of the
[Ycash vaults and Yellowback](https://github.com/boyfromcave/yellowback) project: a proposed Ycash
network upgrade that adds **post-quantum vaults** (YEC locked under rules every node checks), with
**Ycash Yellowback (YED)**, a dollar backed by YEC in vaults (`1 YED = $1`), as the first
application. The wallet adds a **Yellowback** tab; everything else is stock YecWallet.

> **Status: beta (pre-releases for the Ycash testnets).** No activation height is set on mainnet,
> the upgrade has not been adopted by the Ycash Foundation, and there is no external audit. On the
> project's fast testnet the vault upgrade is active but YED cannot be minted (no attestor set is
> configured there); the Yellowback tab works with YED on regtest and the one-laptop devnet. This
> fork's own releases are under *Download* below; the releases linked under *Installation
> (upstream YecWallet)* know nothing of vaults or YED.

### What the Yellowback tab does

- **Mint, send and redeem YED.** Lock YEC in a vault to mint YED; burn the YED to get the YEC back.
  The mint and redeem screens show the fees before you confirm (see below).
- **Claim** an undercollateralized vault, and release the claimed YEC from the **Pending claims**
  page once its delay has passed.
- **Attestors** (the bonded signer set that signs the YEC/USD price) can register with a bond,
  send heartbeats, leave the set, withdraw their bond after the exit delay, report equivocation,
  and cancel a claim made at a wrong price. The page shows the current bond minimum.
- The wallet does not touch the wYEC or nYEC bridges.

### Post-quantum keys

- **Vault owners** sign with SLH-DSA-SHA2-128s. The node creates the owner key and keeps it in
  `wallet.dat`; the wallet itself holds no post-quantum code.
- **YED holders** use Falcon-512 addresses: `ybf…` on mainnet, `ytf…` on testnet, `yrf…` on
  regtest (53 characters; an ordinary YED address is `yb…`/`yt…`/`yr…`, 35 characters). Falcon is active from the upgrade's first block on mainnet; Receive hands out a
  Falcon address once it is, and Send refuses a vault owner's address or, after Falcon, an
  ordinary one.
- **Plain YEC** post-quantum addresses (`sqf…`/`sqs…` on mainnet, `tqf…`/`tqs…` on testnet,
  `rqf…`/`rqs…` on regtest; the third letter names the scheme, FN-DSA-512 or SLH-DSA) hold YEC,
  never YED; the YED Send page refuses them.
- Shielded (Sapling) YEC, the attestors' keys and the pools' keys are not post-quantum. No
  hardware wallet, watch-only wallet or xpub supports these keys.

### Fees

- **Protocol fee:** charged at the mint and again at the redeem, in dollars of the debt like tax
  brackets (1.0 % of the first $1,000, 0.5 % up to $10,000, 0.1 % above, at least $1), paid in YEC
  at the vault's mint price. Redeeming before the end of the term adds an early-redeem fee of
  5 / 2.5 / 1 % of the debt, depending on the vault's class. The largest mint is $100,000.
- **Network fee:** priced by size, in dollars per kilobyte at the attested price, with a floor and
  a cap. Post-quantum signatures are large (a vault owner's spend is about 8 KB), so this matters.

### The node it needs

The release packages bundle the matching ycashd (YecWallet `v6.22.0-beta3` carries ycashd
`v6.22.0-beta3` from [ycash6](https://github.com/boyfromcave/ycash6)). With your own node
(`--no-embedded`), use a ycashd of the vault upgrade: ycash6 6.22.0-beta2 or later, or the
`upgrade/vault` branch of [ycash-dd](https://github.com/boyfromcave/ycash-dd). The wallet requires Yellowback RPC version
(`rpcversion`) **9** (the scheme-visible post-quantum address prefixes, decision D-9: ycash-dd and
ycash6 6.22.x from that change on; beta2), **10** (beta3) or **11** (beta4: tag v2, a post-quantum payout key;
a post-quantum fee payee is shown with its scheme); with any other node the tab is disabled and names
the numbers. A
stock `ycashd` has no Yellowback, and the tab says so. Nothing in `ycash.conf` turns Yellowback on.

### Download

Releases of this fork, with `ycashd` inside, are at
<https://github.com/boyfromcave/yecwallet-dd/releases> (from 6.22.0-beta3 on; pre-releases, for
the Ycash testnets). They are not signed with an Apple Developer ID or notarized: check the
download against `SHA256SUMS` and its minisign signature, then allow the app once in System
Settings → Privacy & Security. [docs/install-macos.md](docs/install-macos.md) walks through both;
[docs/release.md](docs/release.md) is how a release is made.

### Build and test

With CMake, Ninja and Qt 6 (the CI uses Qt 6.5 with testlib):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # add -DCMAKE_PREFIX_PATH=$(brew --prefix qt) on macOS
ninja -C build yecwallet yellowback_test nodecompat_test nodedatacheck_test
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
python3 tests/check-rpc-contract.py   # the RPC fields the wallet reads vs docs/yellowback-rpc-contract.json
```

A release package with the node inside is built by `build.sh`, for example
`bash build.sh macos-arm64 --package --ycashd ../ycash6/src/ycashd`; the release workflow does
the same with the pinned ycash6 release ([docs/release.md](docs/release.md)).

### Try it on one laptop

In a ycash-dd checkout, `contrib/yellowback/devnet/yellowback-devnet up` starts a local test
network with the upgrade active, and `contrib/yellowback/devnet/yellowback-devnet wallet` opens
this wallet on it. By hand: `yecwallet --conf ~/yb-devnet/node0/ycash.conf --no-embedded`. See the
node's [devnet guide](https://github.com/boyfromcave/ycash-dd/blob/upgrade/vault/doc/yellowback-devnet.md).

### More

- [docs/yellowback.md](docs/yellowback.md): developer notes for the tab, screen by screen.
- [The parent project](https://github.com/boyfromcave/yellowback): plans, specs and every component.

# Installation (upstream YecWallet)

Head over to the releases page and grab the latest installers or binary. https://github.com/ycashfoundation/yecwallet/releases

### Linux

If you are on Debian/Ubuntu, please download the binaries
```
sudo dpkg -i linux-deb-yecwallet-v0.7.9.deb
sudo apt install -f
```

Or you can download and run the binaries directly.
```
tar -xvf yecwallet-v0.7.9.tar.gz
./yecwallet-v0.7.9/yecwallet
```

### Windows
Download the release binary, unzip it and double click on `yecwallet.exe` to start.

### macOS
Double-click on the `.dmg` file to open it, and drag `yecwallet` on to the Applications link to install.

## ycashd
YecWallet needs a Ycash node running ycashd. If you already have a ycashd node running, YecWallet will connect to it. 

If you don't have one, YecWallet will start its embedded ycashd node. 
