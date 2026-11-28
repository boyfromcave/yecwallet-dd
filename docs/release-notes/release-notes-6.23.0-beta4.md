# YecWallet v6.23.0-beta4 (Yugen)

The YecWallet release of the Yugen line: the desktop wallet of the Ycash vault network upgrade and
of Yugen, the post-quantum shielded pool. It bundles **ycashd v6.23.0-beta4** (commit `90fe33410`)
from [boyfromcave/ycash6](https://github.com/boyfromcave/ycash6/releases/tag/v6.23.0-beta4) and,
beside it, the three Yugen helpers from the same verified package (`yugen-leafverify`,
`yugen-sendbuild`, `yewd-yugen`), so it needs no separate node.

**Pre-release, for testing.** No activation height for Yugen or the vault upgrade is set on
mainnet, neither has been adopted by the Ycash Foundation, and there is no external audit. Use it
on regtest and the Ycash testnets; expect breaking changes between pre-releases.

## Before you start

- **The node is inside.** The package starts its bundled ycashd v6.23.0-beta4 with
  `-yugenverifier`, `-yugenbuilder` and `-yugenprover` pointing at the bundled helpers (a flag your
  `ycash.conf` already sets is left to it). The first start downloads and checks the chain, which
  takes a while.
- **Your own node instead** (`--no-embedded` with its `ycash.conf`): ycash6 6.23.x for Yugen, or
  a ycashd of the vault upgrade (ycash6 6.22.0-beta2 or later, ycash-dd's `upgrade/vault`) without
  Yugen. The Yellowback tab accepts Yellowback RPC version 9 to 12; this line's node says **12**
  (12 adds the Yugen RPCs and changes no `yed_*` shape).
- **Check the download first** (below), then open it.

## What is in it

Everything in v6.22.0-beta3 (the **Yellowback** tab for Ycash Yellowback (YED)), plus:

- **Yugen recipients in Send.** A Yugen address (`yqf1…`, `yqtf1…` on testnet, `yqrf1…` on
  regtest) is a valid recipient, as are post-quantum transparent addresses (`sqf`/`sqs`,
  `tqf`/`tqs`, `rqf`/`rqs`). The payment goes through the usual `z_sendmany` path; ycashd builds the
  Yugen deposit from a transparent or Sapling sender. The memo field stays available.
- **Pending operations in Send.** A `z_sendmany` operation that ycashd holds in its `pending`
  state stays watched, and its reason shows in the progress tooltip.

The [README](https://github.com/boyfromcave/yecwallet-dd/blob/upgrade/yugen/README.md) and
[docs/yugen.md](https://github.com/boyfromcave/yecwallet-dd/blob/upgrade/yugen/docs/yugen.md)
describe the wallet against a Yugen-activated ycashd.

## Downloads

| Platform | File |
|---|---|
| macOS, Apple silicon | `macos-arm64-yecwallet-v6.23.0-beta4.dmg` |
| macOS, Intel | `macos-x86_64-yecwallet-v6.23.0-beta4.dmg` (best effort: may be missing) |
| Linux x86_64 | `linux-x86_64-yecwallet-v6.23.0-beta4.tar.gz` (best effort: may be missing) |

There is no Windows package on this line: ycash6 publishes no Windows Yugen build.

This GitHub releases page is the only official channel. Check a download against `SHA256SUMS` and
its signatures before you run it:

    shasum -a 256 -c --ignore-missing SHA256SUMS      # Linux: sha256sum -c --ignore-missing SHA256SUMS
    minisign -Vm SHA256SUMS -P RWQYr5PFpiPJAigk86vf89bPYrJCXj5tskgRAPRYFsEXyciZo8dveOGQ
    cosign verify-blob --bundle SHA256SUMS.cosign.bundle \
      --certificate-identity https://github.com/boyfromcave/yecwallet-dd/.github/workflows/yecwallet-release.yml@refs/tags/v6.23.0-beta4 \
      --certificate-oidc-issuer https://token.actions.githubusercontent.com SHA256SUMS

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
[docs/install-macos.md](https://github.com/boyfromcave/yecwallet-dd/blob/upgrade/yugen/docs/install-macos.md).

## Known limitations

- **No public Yugen activation height.** Yugen is active only where you set it (regtest, devnets);
  no public network has a height, so Yugen sends fail there.
- **Yugen is send-only in the wallet** (docs/yugen.md, open UX items): ycashd holds no Yugen
  account yet, so these are not built:
  - U-1: no Yugen receive address in the Receive tab;
  - U-2: no Yugen balance or pending-Yugen line in Balances;
  - U-3: the confirm dialog does not show the Yugen transfer fee (or the Sapling hop fee);
  - U-4: a Yugen deposit is not labelled as one in Transactions;
  - U-5: `ycash:` URIs and the address book with `yq…` addresses are unchecked.
- **The node's known issues apply.** See the
  [ycash6 v6.23.0-beta4 release notes](https://github.com/boyfromcave/ycash6/releases/tag/v6.23.0-beta4).
- **No Apple Developer ID signature and no notarization.** Every macOS binary is ad-hoc signed,
  which Apple silicon requires and which says nothing about who built it; `SHA256SUMS` and its
  minisign and cosign signatures are the trust anchor.
- **Only the Apple silicon package is required for a release.** The Intel macOS and Linux
  packages are built on a best-effort basis and may be missing.
