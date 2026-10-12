YecWallet is the official wallet for [Ycash](https://www.ycash.xyz) that runs on Linux, Windows and macOS.

## Ycash vaults and Yellowback on this branch

This fork of YecWallet is the desktop wallet of the
[Ycash vaults and Yellowback](https://github.com/boyfromcave/yellowback) project: a proposed Ycash
network upgrade that adds **post-quantum vaults** (YEC locked under rules every node checks), with
**Ycash Yellowback (YED)**, a dollar backed by YEC in vaults (`1 YED = $1`), as the first
application. The wallet adds a **Yellowback** tab; everything else is stock YecWallet.

> **Status: in development.** It runs on regtest and on a one-laptop devnet only. No activation
> height is set on mainnet or testnet, the upgrade has not been adopted by the Ycash Foundation,
> and there is no external audit. The releases linked under Installation are upstream YecWallet
> and know nothing of vaults or YED.

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
- **YED holders** use Falcon-512 addresses: `yb…` on mainnet, `yt…` on testnet, `yr…` on regtest
  (53 characters). Falcon is active from the upgrade's first block on mainnet; Receive hands out a
  Falcon address once it is, and Send refuses a vault owner's address or, after Falcon, an
  ordinary one.
- **Plain YEC** post-quantum addresses (`sq…` / `tq…` / `rq…`) hold YEC, never YED; the YED Send
  page refuses them.
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

A `ycashd` built from the `upgrade/quantum-vaults` branch of
[ycash-dd](https://github.com/boyfromcave/ycash-dd) or [ycash6](https://github.com/boyfromcave/ycash6)
(this line joins `upgrade/vault` at release). The wallet requires Yellowback RPC version
(`rpcversion`) **8** exactly; with any other node the tab is disabled and names both numbers. A
stock `ycashd` has no Yellowback, and the tab says so. Nothing in `ycash.conf` turns Yellowback on.

### Build and test

With CMake, Ninja and Qt 6 (the CI uses Qt 6.5 with testlib):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # add -DCMAKE_PREFIX_PATH=$(brew --prefix qt) on macOS
ninja -C build yecwallet yellowback_test nodecompat_test nodedatacheck_test
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
python3 tests/check-rpc-contract.py   # the RPC fields the wallet reads vs docs/yellowback-rpc-contract.json
```

A release package with the node inside is built by `build.sh`, for example
`bash build.sh macos-arm64 --package --ycashd ../ycash-dd/src/ycashd`.

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
