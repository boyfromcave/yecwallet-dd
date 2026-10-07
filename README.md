YecWallet is the official wallet for [Ycash](https://www.ycash.xyz) that runs on Linux, Windows and macOS.

## Ycash Yellowback (YED) on this branch

This branch (`upgrade/vault`) adds a **Yellowback** tab to YecWallet. It is the wallet side of a
proposed Ycash network upgrade, **the vault upgrade**, which adds **vaults** to Ycash: YEC locked on
chain under rules every node enforces. **Ycash Yellowback (YED)** is a dollar token built on vaults:
lock YEC in a vault to mint YED (`1 YED = 1 US dollar`), return the YED to get the YEC back. If a
vault's YEC becomes worth less than the YED it backs, others can claim it.

- **What the tab does:** mint, send and redeem YED; claim an under-collateralised vault and, on
  the **Pending claims** page, release the claimed YEC once its delay has passed. For
  **attestors** (members of Yellowback's signer set, who sign YEC/USD prices): join the set, send
  heartbeats, and cancel a claim made at a wrong price. The wallet does not touch the wYEC bridge.
- **Node it needs:** a ycashd built from the `upgrade/vault` branch of
  [ycash-dd](https://github.com/boyfromcave/ycash-dd) or
  [ycash6](https://github.com/boyfromcave/ycash6). A stock ycashd has no Yellowback, and the tab
  says so. Nothing in `ycash.conf` turns Yellowback on.
- **Status: proposed, not live.** It runs on a local test network (regtest) only. It has not been
  adopted by the Ycash Foundation, has not been audited, and has no activation height on mainnet
  or testnet. The releases linked below are upstream YecWallet and know nothing of Yellowback.
- **Try it:** in a ycash-dd checkout, `contrib/yellowback/devnet/yellowback-devnet up` starts a
  local test network and `contrib/yellowback/devnet/yellowback-devnet wallet` opens this wallet on
  it (the node's [devnet crash course](https://github.com/boyfromcave/ycash-dd/blob/upgrade/vault/doc/yellowback-devnet.md)
  has the details). By hand: `yecwallet --conf ~/yb-devnet/node0/ycash.conf --no-embedded`.
- **More:** [docs/yellowback.md](docs/yellowback.md), the developer notes for the tab.

# Installation

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
