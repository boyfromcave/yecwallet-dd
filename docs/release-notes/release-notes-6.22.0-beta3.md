# YecWallet v6.22.0-beta3 (Yellowback)

The first release of this YecWallet fork: the desktop wallet of the Ycash vault network upgrade,
with the **Yellowback** tab for Ycash Yellowback (YED). It bundles **ycashd v6.22.0-beta3** from
[boyfromcave/ycash6](https://github.com/boyfromcave/ycash6/releases/tag/v6.22.0-beta3), so it
needs no separate node.

**Pre-release, for testing.** No activation height for the vault upgrade is set on mainnet, the
upgrade has not been adopted by the Ycash Foundation, and there is no external audit. Use it on the
Ycash testnets; expect breaking changes between pre-releases.

## Before you start

- **The node is inside.** The package starts its bundled ycashd v6.22.0-beta3; nothing else to
  install. The first start downloads and checks the chain, which takes a while.
- **Your own node instead** (`--no-embedded` with its `ycash.conf`): a ycashd of the vault upgrade,
  ycash6 6.22.0-beta2 or later or ycash-dd's `upgrade/vault`. The Yellowback tab needs the node's
  Yellowback RPC version 9 (beta2) or 10 (beta3); with any other version it is disabled and says
  which versions it accepts. A stock ycashd has no Yellowback, and the tab says so.
- **Check the download first** (below), then open it.

## What is in it

Stock YecWallet, plus a **Yellowback** tab: mint YED against YEC locked in a vault, send and
redeem it, claim an undercollateralized vault and release the claim, and the attestors' pages
(register, heartbeat, leave, withdraw the bond, report equivocation, cancel a claim made at a wrong
price). Fees are shown before you confirm. The
[README](https://github.com/boyfromcave/yecwallet-dd/blob/upgrade/vault/README.md) describes the tab.

## Downloads

| Platform | File |
|---|---|
| macOS, Apple silicon | `macos-arm64-yecwallet-v6.22.0-beta3.dmg` |
| macOS, Intel | `macos-x86_64-yecwallet-v6.22.0-beta3.dmg` (best effort: may be missing) |
| Linux x86_64 | `linux-x86_64-yecwallet-v6.22.0-beta3.tar.gz` (best effort: may be missing) |
| Windows x86_64 | `windows-x86_64-yecwallet-v6.22.0-beta3.zip` (best effort: may be missing) |

This GitHub releases page is the only official channel. Check a download against `SHA256SUMS` and
its signatures (below) before you run it.

## macOS: the first start shows a warning

The app is not signed with an Apple Developer ID and not notarized, so macOS warns on first open:
*"yecwallet" Not Opened — Apple could not verify "yecwallet" is free of malware…*. After checking
the download:

1. Click **Done** (not Move to Trash).
2. Open **System Settings → Privacy & Security**, scroll to **Security**, and click
   **Open Anyway** next to *"yecwallet" was blocked to protect your Mac*. Confirm with your
   password or Touch ID, then click **Open Anyway** once more.

Right-click → Open no longer skips this on current macOS. Alternatively, in Terminal:
`xattr -dr com.apple.quarantine /Applications/yecwallet.app`. Macs managed by an organization may
block the app with no override. The full guide is
[docs/install-macos.md](https://github.com/boyfromcave/yecwallet-dd/blob/upgrade/vault/docs/install-macos.md).

Windows shows a SmartScreen prompt for the same reason: **More info → Run anyway**.

## Known limitations

- **No Apple Developer ID signature and no notarization.** Every macOS binary is ad-hoc signed,
  which Apple silicon requires and which says nothing about who built it; `SHA256SUMS` and its
  minisign and cosign signatures are what do. Macs managed by an organization may refuse the app.
- **Only the Apple silicon package is required for a release.** The Intel macOS, Linux and Windows
  packages are built on a best-effort basis and may be missing.
- **No YED on the fast testnet.** The vault upgrade is active there, but no YED attestor set is
  configured, so the tab cannot mint. YEC works as usual.
- **Not live on mainnet.** No activation height for the vault upgrade is set there, so the
  Yellowback tab has nothing to work with on mainnet.
