YecWallet is the official wallet for [Ycash](https://www.ycash.xyz) that runs on Linux, Windows and macOS.

## This branch: Ycash Yellowback (YED) on the vault network upgrade

This fork's `upgrade/vault` branch adds a Yellowback tab for **Ycash Yellowback (YED)**, a dollar
unit backed by YEC locked in vaults. It drives a node that carries the **vault network upgrade**
(`UPGRADE_VAULT`, consensus branch ID `0x6d5b7a31`): a coordinated hard fork that adds a generic
lock/unlock primitive (signer sets, vault templates, intents) with Yellowback as its one rule
module, so YED's rules are consensus on every upgraded node. The wallet speaks the node's
`yed_*` RPCs at `rpcversion` 5 and the primitive's `set_*` / `vault_*` RPCs: mint, send, redeem,
claim, **Pending claims** (release after the claim delay; a member of the YED attestor set may
cancel a wrong-price claim), and attestor set membership with heartbeats. Nothing in `ycash.conf`
enables Yellowback; it is on wherever the node has the upgrade and the network's YED attestor set
configured.

**Status:** implemented and tested on regtest and the one-laptop devnet only. **Mainnet and testnet
have no activation height and no attestor set**, and the upgrade is not adopted by the Ycash
Foundation, not audited and not activated on any public network. The releases linked below are
upstream YecWallet and know nothing of Yellowback.

- **Node:** a ycashd built from `upgrade/vault` of
  [ycash-dd](https://github.com/boyfromcave/ycash-dd) (v4.5.0 line) or
  [ycash6](https://github.com/boyfromcave/ycash6) (6.20.0 line); the wallet reads the node version
  at connect and drives either. A stock ycashd has no `yed_*` RPCs, and the tab says so. On regtest
  the node takes `-nuparams=6d5b7a31:<height> -yellowbackattestorset=<setid>`.
- **Try it:** start the devnet from the node tree (`contrib/yellowback/devnet/yellowback-devnet up`)
  and attach the wallet with `yecwallet --conf <dir>/node0/ycash.conf --no-embedded`.
- **Details:** [docs/yellowback.md](docs/yellowback.md) (its opening status section is the
  `upgrade/vault` one; the rest records the earlier flows). The `harden/yellowback` branch is the
  no-upgrade fallback line, which used the `experimentalfeatures` / `yellowback` conf keys.

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
