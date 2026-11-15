# Installing YecWallet on macOS

YecWallet releases are on one page only:
<https://github.com/boyfromcave/yecwallet-dd/releases>. A YecWallet download offered anywhere else
is not from this project.

## Which file

| Your Mac | File |
|---|---|
| Apple silicon (M1 and later) | `macos-arm64-yecwallet-v<version>.dmg` |
| Intel | `macos-x86_64-yecwallet-v<version>.dmg`, when the release has one (it is built on a best-effort basis and may be missing) |

Also download `SHA256SUMS` and `SHA256SUMS.minisig` from the same release. The wallet comes with
its node, `ycashd`, inside the app: you do not need to install anything else.

## 1. Check the download first

The files are not signed with an Apple Developer ID and are not notarized by Apple, so macOS
cannot tell you who made them. The project's own signatures can. Check them **before** you allow
the app to run. In Terminal, in the folder holding the downloads:

```sh
shasum -a 256 -c --ignore-missing SHA256SUMS
brew install minisign        # once; or see https://jedisct1.github.io/minisign/
minisign -Vm SHA256SUMS -P RWQYr5PFpiPJAigk86vf89bPYrJCXj5tskgRAPRYFsEXyciZo8dveOGQ
```

You should see `OK` next to the `.dmg` you downloaded, then `Signature and comment signature
verified` from minisign. Its trusted comment names `boyfromcave/yecwallet-dd` and the version. The
key `RWQYr5…` (key id `02C923A6C593AF18`) is the Ycash release key that also signs the ycashd
releases. If either check fails, delete the download and do not open it.

Each release also carries a keyless Sigstore signature (`SHA256SUMS.cosign.bundle`) and GitHub
build provenance for every file, with the commands to check them in the release notes. They show
that the files were built by this repository's release workflow from the tagged source.

## 2. Install

Double-click the `.dmg`, then drag `yecwallet` onto the `Applications` link in the window that
opens. Eject the disk image.

## 3. Open it the first time

The first time you open `yecwallet`, macOS stops it with a message like:

> **"yecwallet" Not Opened**
> Apple could not verify "yecwallet" is free of malware that may harm your Mac or compromise your
> privacy.

with the buttons **Done** and **Move to Trash**. Click **Done** (not Move to Trash). Then:

1. Open **System Settings → Privacy & Security**.
2. Scroll down to **Security**. You will see *"yecwallet" was blocked to protect your Mac.*
3. Click **Open Anyway**, and confirm with your password or Touch ID.
4. macOS asks once more: click **Open Anyway**.

From then on the wallet opens normally. The **Open Anyway** button only appears for about an hour
after the blocked attempt; if you do not see it, open the app again and go back to Privacy &
Security. On current macOS, right-clicking the app and choosing **Open** no longer skips this
step.

If macOS later blocks `ycashd` on its own (the node the wallet starts), allow it the same way.

### Or: clear the quarantine mark in Terminal

The warning comes from the quarantine mark your browser puts on downloaded files. If you have
checked the download as above, you can remove the mark instead of using System Settings:

```sh
xattr -dr com.apple.quarantine /Applications/yecwallet.app
```

Files fetched with `curl` or `gh release download` are not marked, so they open without the
warning; check them all the same.

## What this means, plainly

- The app is not notarized: Apple has not scanned it for malware. Your assurance is the checksum
  and the signatures in step 1, which tie the file to this project's release.
- Every binary in the app carries an *ad-hoc* signature. It only keeps the code from being changed
  after signing (Apple silicon Macs run nothing unsigned); it says nothing about who built it.
- Macs managed by an organization may block apps that are not from an identified developer, with
  no **Open Anyway** button. Ask your administrator, or run the wallet on another machine.

## First start

YecWallet starts its bundled `ycashd`, which downloads the Ycash block chain. The first sync takes
hours and tens of GB of disk space. If a `ycashd` is already running on this Mac, the wallet uses
that one instead. See [README.md](../README.md) for the node the Yellowback tab needs.
