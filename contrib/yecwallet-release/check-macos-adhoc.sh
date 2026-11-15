#!/usr/bin/env bash
# Copyright (c) 2026 The Ycash developers
# Distributed under the MIT software license, see the accompanying file LICENSE.
#
# The macOS release-build check (docs/release.md): every Mach-O file in the app bundle carries a
# valid ad-hoc code signature, and the bundle's own signature verifies.
#
# YecWallet ships without an Apple Developer ID and without notarization (the Yellowback lead
# architect's decision, 2026-11-15). macOS on Apple Silicon still refuses to run an arm64 binary
# with no signature at all ("Killed: 9"), so each binary must carry the ad-hoc signature the
# linker adds, or the one build.sh applies with `codesign --sign -` after bundling the node (which
# copying, stripping and macdeployqt can invalidate). An ad-hoc signature needs no account and
# proves nothing about who built the file: the project's own SHA256SUMS signatures do that.
#
# Usage: contrib/yecwallet-release/check-macos-adhoc.sh <yecwallet.app | YecWallet.dmg> [--arch arm64|x86_64]
#   With a .dmg the image is mounted read-only, checked and detached. --arch also requires every
#   Mach-O to contain that architecture.
set -euo pipefail

TARGET="${1:?usage: check-macos-adhoc.sh <app|dmg> [--arch ARCH]}"; shift
ARCH=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) ARCH="$2"; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
[[ "$(uname)" == Darwin ]] || { echo "check-macos-adhoc.sh runs on macOS (codesign)" >&2; exit 2; }

MNT=""
cleanup() { if [[ -n "$MNT" ]]; then hdiutil detach -quiet "$MNT" 2>/dev/null || true; rmdir "$MNT" 2>/dev/null || true; fi; }
trap cleanup EXIT

APP="$TARGET"
if [[ "$TARGET" == *.dmg ]]; then
    MNT="$(mktemp -d "${TMPDIR:-/tmp}/yecwallet-dmg.XXXXXX")"
    hdiutil attach -quiet -readonly -nobrowse -mountpoint "$MNT" "$TARGET"
    APP="$(find "$MNT" -maxdepth 1 -name '*.app' -type d | head -n1)"
    [[ -n "$APP" ]] || { echo "FAIL: no .app in $TARGET" >&2; exit 1; }
fi
[[ -d "$APP/Contents/MacOS" ]] || { echo "FAIL: $APP is not an app bundle" >&2; exit 1; }

fail=0
n=0
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' || continue
    n=$((n + 1))
    rel="${f#"$APP"/}"
    info="$(codesign -dv "$f" 2>&1 || true)"
    if ! grep -q '^Signature=adhoc' <<<"$info"; then
        echo "FAIL: $rel is not ad-hoc signed: $(grep -E '^(Signature|Authority)=|not signed' <<<"$info" | head -n1)" >&2
        fail=1
    elif ! codesign --verify --strict "$f" 2>/dev/null; then
        echo "FAIL: $rel: its ad-hoc signature does not verify (modified after signing?)" >&2
        fail=1
    else
        echo "ok    $rel: Signature=adhoc, $(lipo -archs "$f" 2>/dev/null)"
    fi
    if [[ -n "$ARCH" ]] && ! lipo -archs "$f" 2>/dev/null | tr ' ' '\n' | grep -qx "$ARCH"; then
        echo "FAIL: $rel has no $ARCH slice ($(lipo -archs "$f" 2>/dev/null))" >&2
        fail=1
    fi
done < <(find "$APP/Contents" -type f -print0)

[[ $n -gt 0 ]] || { echo "FAIL: no Mach-O files under $APP/Contents" >&2; exit 1; }
for exe in yecwallet ycashd; do
    [[ -f "$APP/Contents/MacOS/$exe" ]] || { echo "FAIL: Contents/MacOS/$exe is missing" >&2; fail=1; }
done
if codesign --verify --strict --deep "$APP" 2>/dev/null; then
    echo "ok    $(basename "$APP"): bundle signature verifies ($(codesign -dv "$APP" 2>&1 | grep '^Signature=' || echo 'Signature=?'))"
else
    echo "FAIL: $(basename "$APP"): codesign --verify --strict --deep fails" >&2
    codesign --verify --strict --deep --verbose=2 "$APP" 2>&1 | head -n5 >&2 || true
    fail=1
fi

[[ $fail -eq 0 ]] || exit 1
echo "all $n Mach-O files ad-hoc signed; no Developer ID, not notarized (expected)"
