# Releasing YecWallet with Ycash Yellowback (YED)

How a tagged version of this fork (`boyfromcave/yecwallet-dd`) becomes a GitHub release: one
package per platform with the node inside, checksums, signatures and an SBOM per package. The
automation is [`.github/workflows/yecwallet-release.yml`](../.github/workflows/yecwallet-release.yml);
this file is the procedure around it. It follows ycash6's release (`doc/yellowback-release.md`
there): the same tag guards, the same signing, a draft that a person publishes.

## Versions and the node inside

The wallet carries the version of the release train it belongs to: YecWallet `v6.22.0-beta3`
bundles ycashd `v6.22.0-beta3`. `src/version.h` (`APP_VERSION`) is the only place the version is
written; CMake, `build.sh` and the workflow read it.

The bundled `ycashd` is never "latest". `contrib/yecwallet-release/bundled-ycashd.env` pins ycash6's release
by tag (`YCASH6_TAG`, which must be `v<APP_VERSION>`) and by the commit that tag names
(`YCASH6_COMMIT`), taken from the workspace train file `releases/v<version>.yaml`. The workflow
downloads that release's published packages and refuses to bundle them unless:

- the ycash6 tag still names the pinned commit (`git ls-remote`),
- the package is listed in, and matches, the ycash6 release's `SHA256SUMS`,
- that `SHA256SUMS` carries a keyless cosign signature whose identity is ycash6's
  `yellowback-release.yml` at that tag, and
- its `SHA256SUMS.minisig` verifies against the Ycash release key
  (`contrib/yecwallet-release/ycash-release.pub`, key id `02C923A6C593AF18`).

So the node release is published (and minisign-signed) before the wallet that bundles it is
tagged: a draft ycash6 release cannot be downloaded. `yellowback-attest`, the attestation
subscriber the Settings page launches, is built from ycash6's `contrib/yellowback/attest` at the
same pinned commit, with the toolchain that crate pins.

On the Yugen line (a bundled ycash6 6.23.x or later) the three Yugen helpers, `yugen-leafverify`,
`yugen-sendbuild` and `yewd-yugen`, are taken from the same verified ycash6 package as ycashd and put
beside it (`build.sh --yugen-helpers`); the workflow refuses a package without them. The wallet starts
the embedded ycashd with `-yugenverifier`, `-yugenbuilder` and `-yugenprover` pointing at them (unless
`ycash.conf` sets one). ycash6 publishes no Windows Yugen package, so that line has no Windows asset.

## What a release contains

| Asset | Built on | Tier |
|---|---|---|
| `macos-arm64-yecwallet-v<V>.dmg` | macos-15; ycash6's `macos_aarch64` ycashd | 1 |
| `macos-x86_64-yecwallet-v<V>.dmg` | macos-15-intel; ycash6's `macos_x86_64` ycashd | 2 |
| `linux-x86_64-yecwallet-v<V>.tar.gz` | ubuntu-22.04; ycash6's `linux_x86_64` ycashd | 2 |
| `windows-x86_64-yecwallet-v<V>.zip` | ubuntu-22.04, llvm-mingw cross build; ycash6's `windows` ycashd; no `yellowback-attest` | 2 |
| `<package>.spdx.json` | an SPDX SBOM of each package (syft) | |
| `SHA256SUMS` | the sha256 of every package and SBOM | |
| `SHA256SUMS.cosign.bundle` | a keyless Sigstore signature of `SHA256SUMS` | |
| `SHA256SUMS.minisig` | the Yellowback lead architect's minisign signature, added on the draft (below) | |

Every package is built by `build.sh <target> --package --ycashd <node> [--attest <bin>]` with a
static Qt (`QT_VERSION` 6.5.8) built from source on the runner; the Qt source tarball is checked
against its SHA-256 (`scripts/build-qt.sh`). No run restores a cache.
Tier 1 must pass for a release to be drafted; a tier-2 job may fail and its package is then
missing. Tier 2 is honest about what is proven: before beta3 only the macOS arm64 package had been
built by `build.sh` (on a developer Mac); the Linux and Windows packages had never been built in CI.

### macOS: ad-hoc signatures, no Apple identity

There is no Apple Developer ID signature and no notarization (the Yellowback lead architect's
decision, 2026-11-15). macOS on Apple Silicon refuses to run an arm64 binary with no signature,
so `build.sh` ad-hoc signs (`codesign --sign -`, no identity) every Mach-O file in the app after
bundling the node, seals the bundle, and runs `contrib/yecwallet-release/check-macos-adhoc.sh` on the app and
again on the finished `.dmg`: every Mach-O must show `Signature=adhoc` in `codesign -dv`, verify
with `codesign --verify --strict`, and carry the target architecture; the bundle must verify with
`--deep`. The workflow then runs the bundled `ycashd --version` from the mounted image. Users see
a Gatekeeper warning on first run; [install-macos.md](install-macos.md) is what they are told.

Windows packages carry no Authenticode signature either: SmartScreen warns on first run.

## Procedure

1. **Version bump PR** (to `upgrade/vault`): `src/version.h` to the train's version, and
   `contrib/yecwallet-release/bundled-ycashd.env` to the train file's ycash6 tag and the commit it names.
   Add `docs/release-notes/release-notes-<V>.md` (the release job refuses a tag without it); it
   carries the first-run text from [install-macos.md](install-macos.md). The PR's `release-check`
   job runs the guards (`contrib/yecwallet-release/check-version.sh`) in warn mode.
2. **Wait for the node.** The ycash6 release `v<V>` is published with its `SHA256SUMS.minisig`.
3. **Proving run** (optional, recommended): see "Dry run" below, with `strict` true.
4. **Tag**: `git tag v<V> <commit> && git push origin v<V>` on the merged commit. The workflow
   builds, checks and signs, and creates a **draft** prerelease (`-beta`/`-rc`) or release.
5. **Sign**: the lead architect adds `SHA256SUMS.minisig` from the private ycash-ops
   `sign-release` workflow (`repo: yecwallet-dd`, the tag; it checks the provenance first) or by
   hand with its `scripts/sign-release.sh yecwallet-dd v<V>`.
6. **Try it**: download the draft's `.dmg` on an Apple silicon Mac through a browser (so it is
   quarantined), verify it as install-macos.md says, and walk the first-run steps.
7. **Publish** the draft by hand. Record the tag in the train file (`yecwallet-dd.tag`).

Nothing in the workflow creates a tag or publishes a release.

## Dry run

A `workflow_dispatch` of `yecwallet release` on a branch is a proving run: the same guards and
builds, the same checks, SBOMs and signatures (whose cosign identity names the branch, never a
tag), and a `release-dry-run` job that checks the signed set and the notes file and prints the
release a tag would create. It never creates a release; its packages are workflow artifacts.

- **Strict** (the default; what a tag would do): needs the pin set and the ycash6 release
  published and minisign-signed.

  ```sh
  gh workflow run yecwallet-release.yml -R boyfromcave/yecwallet-dd --ref upgrade/vault
  ```

- **Before the node release exists** (strict=false): bundle a published ycash6 release instead,
  and optionally build fewer targets (macOS arm64 alone is the cheapest proof of tier 1). The
  run warns about every rule a tag would enforce.

  ```sh
  gh workflow run yecwallet-release.yml -R boyfromcave/yecwallet-dd --ref upgrade/vault \
    -f strict=false -f ycash6_tag=v6.22.0-beta2 \
    -f ycash6_commit=fbc9f625c657eca50ddc09e5065e8ed0d3a33a48 -f targets=macos-arm64
  ```

Every run builds static Qt from source, with no cache (about an hour per target on a hosted
runner; the Windows job builds the Linux host Qt first).

## Locally

```sh
contrib/yecwallet-release/check-version.sh --remote          # the guards a tag runs, against the pin
bash build.sh macos-arm64 --package --ycashd <ycashd> --attest <yellowback-attest>
# Yugen line: <pkg> is the unpacked ycash6 package (ycashd and the three helpers)
bash build.sh macos-arm64 --package --ycashd <pkg>/ycashd --yugen-helpers <pkg> --attest <yellowback-attest>
contrib/yecwallet-release/check-macos-adhoc.sh artifacts/macos-arm64-yecwallet-v<V>.dmg --arch arm64
```
