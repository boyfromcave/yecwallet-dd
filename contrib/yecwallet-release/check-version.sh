#!/usr/bin/env bash
# Copyright (c) 2026 The Ycash developers
# Distributed under the MIT software license, see the accompanying file LICENSE.
#
# The release guards of a YecWallet release (docs/release.md), shared by the release workflow and
# the cheap pull-request check: the version src/version.h names, the tag that releases it, and the
# ycashd it bundles (contrib/yecwallet-release/bundled-ycashd.env).
#
# Usage: contrib/yecwallet-release/check-version.sh [--tag TAG] [--warn] [--remote]
#                                         [--ycash6-tag TAG --ycash6-commit SHA]
#   --tag TAG          the pushed tag: must be v<APP_VERSION> (always fatal)
#   --warn             report the pin rules as warnings instead of refusing (a proving run's
#                      strict=false); a tag never runs with --warn
#   --remote           also check, with git ls-remote, that the ycash6 tag names the pinned commit
#                      (the release workflow; without it, --warn accepts YCASH6_COMMIT=unset, as the
#                      pull-request check does between releases)
#   --ycash6-tag/--ycash6-commit
#                      bundle another ycash6 release than the pin (a proving run before the
#                      pinned node release exists); refused unless --warn
#
# Prints KEY=VALUE lines on stdout (version, prerelease, stage, ycash6_repo, ycash6_tag,
# ycash6_commit, ycash6_version); messages go to stderr. Exit 1 on a refusal.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TAG=""; WARN=false; REMOTE=false; O_TAG=""; O_COMMIT=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --tag)            TAG="$2"; shift ;;
        --warn)           WARN=true ;;
        --remote)         REMOTE=true ;;
        --ycash6-tag)     O_TAG="$2"; shift ;;
        --ycash6-commit)  O_COMMIT="$2"; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

fail=0
refuse() {
    if $WARN; then echo "::warning::$* (warn only: continuing)" >&2
    else echo "refused: $*" >&2; fail=1; fi
}

version="$(sed -n 's/^#define APP_VERSION "\([^"]*\)".*/\1/p' "$ROOT/src/version.h" | head -n1)"
[[ -n "$version" ]] || { echo "refused: src/version.h has no APP_VERSION" >&2; exit 1; }
# The train's version form (ycash6 configure.ac renders the same): X.Y.Z, X.Y.Z-betaN, X.Y.Z-rcN.
if ! [[ "$version" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(-(beta|rc)[0-9]+)?$ ]]; then
    echo "refused: APP_VERSION \"$version\" is not X.Y.Z, X.Y.Z-betaN or X.Y.Z-rcN" >&2; exit 1
fi
case "$version" in
    *-beta*) stage=beta; pre=true ;;
    *-rc*)   stage=rc;   pre=true ;;
    *)       stage=final; pre=false ;;
esac

# A tag releases exactly the version the source names; never relaxed.
if [[ -n "$TAG" && "$TAG" != "v$version" ]]; then
    echo "refused: tag $TAG != v$version (src/version.h)" >&2; exit 1
fi

pin() { sed -n "s/^$1=\(.*\)$/\1/p" "$ROOT/contrib/yecwallet-release/bundled-ycashd.env" | tail -n1; }
repo="$(pin YCASH6_REPO)"; ntag="$(pin YCASH6_TAG)"; ncommit="$(pin YCASH6_COMMIT)"
[[ "$repo" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || { echo "refused: YCASH6_REPO \"$repo\" is not owner/name" >&2; exit 1; }

if [[ -n "$O_TAG" || -n "$O_COMMIT" ]]; then
    [[ -n "$O_TAG" && -n "$O_COMMIT" ]] || { echo "refused: --ycash6-tag and --ycash6-commit go together" >&2; exit 1; }
    if ! $WARN || [[ -n "$TAG" ]]; then
        echo "refused: overriding the bundled ycashd ($ntag -> $O_TAG) is for a non-strict proving run only" >&2; exit 1
    fi
    echo "::warning::bundling $repo $O_TAG ($O_COMMIT) instead of the pin $ntag: a proving run only" >&2
    ntag="$O_TAG"; ncommit="$O_COMMIT"
fi

[[ "$ntag" =~ ^v[0-9A-Za-z.+-]+$ ]] || { echo "refused: ycash6 tag \"$ntag\" is not a version tag" >&2; exit 1; }
nversion="${ntag#v}"
[[ "$ntag" == "v$version" ]] || refuse "the bundled ycashd is $ntag, but a v$version wallet bundles the node release v$version (contrib/yecwallet-release/bundled-ycashd.env)"
if ! [[ "$ncommit" =~ ^[0-9a-f]{40}$ ]]; then
    refuse "YCASH6_COMMIT is \"$ncommit\": set it to the commit $ntag names (the train file releases/v$version.yaml)"
    # A commit that is not a full SHA cannot be checked or bundled: with --remote (the workflow,
    # which then bundles it) that is a refusal even when only warning.
    if $REMOTE; then exit 1; fi
fi

if $REMOTE && [[ "$ncommit" =~ ^[0-9a-f]{40}$ ]]; then
    url="https://github.com/$repo.git"
    # The peeled commit of an annotated tag, else the tag's own object (a lightweight tag).
    got="$(git ls-remote "$url" "refs/tags/$ntag^{}" | awk 'NR==1{print $1}')"
    [[ -n "$got" ]] || got="$(git ls-remote "$url" "refs/tags/$ntag" | awk 'NR==1{print $1}')"
    if [[ -z "$got" ]]; then
        echo "refused: $repo has no tag $ntag" >&2; exit 1
    elif [[ "$got" != "$ncommit" ]]; then
        echo "refused: $repo tag $ntag names $got, not the pinned $ncommit" >&2; exit 1
    fi
    echo "$repo $ntag = $got" >&2
fi

[[ $fail -eq 0 ]] || exit 1
printf 'version=%s\nprerelease=%s\nstage=%s\nycash6_repo=%s\nycash6_tag=%s\nycash6_commit=%s\nycash6_version=%s\n' \
    "$version" "$pre" "$stage" "$repo" "$ntag" "$ncommit" "$nversion"
