#!/usr/bin/env bash
#
# release.sh — commit, tag and push a release, in that order, with the checks
# that catch the ways this goes wrong.
#
#   ./scripts/release.sh v1.3.1
#
# Why this exists: the sequence is commit → push → tag → push-tag, and every
# step depends on the one before it. Run by hand, a step that fails quietly —
# a stale index.lock, a typo, a closed terminal — leaves a tag pointing at the
# wrong commit, which then builds and publishes the wrong binary under the
# right name. That happened to v1.2.0 and v1.3.0.
#
# Everything here stops on the first failure. It would rather do nothing than
# do half of it.

set -euo pipefail

TAG="${1:-}"
if [[ -z "$TAG" ]]; then
    echo "usage: $0 vX.Y.Z" >&2
    exit 1
fi
if [[ ! "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "error: '$TAG' is not a vX.Y.Z tag" >&2
    exit 1
fi

cd "$(dirname "$0")/.."
VERSION_NUMBER="${TAG#v}"

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
fail() { printf '\n\033[31merror: %s\033[0m\n' "$*" >&2; exit 1; }

# --- 1. a stale lock blocks every write, and says so unhelpfully ------------
if [[ -f .git/index.lock ]]; then
    echo "Found a stale .git/index.lock — removing it."
    echo "(Git leaves this behind when a process that was reading the repo"
    echo " was killed, or could not clean up after itself. While it exists,"
    echo " every 'git add' and 'git commit' fails.)"
    rm -f .git/index.lock || fail "could not remove .git/index.lock — delete it yourself"
fi

# --- 2. the version has to match the tag ------------------------------------
say "Checking the version"
FILE_VERSION="$(tr -d '[:space:]' < VERSION)"
[[ "$FILE_VERSION" == "$VERSION_NUMBER" ]] \
    || fail "VERSION says $FILE_VERSION but you asked for $TAG. Bump VERSION, src/resource.h and src/SnipText.manifest first."

grep -q "\"$VERSION_NUMBER\.0\"" src/resource.h \
    || fail "src/resource.h does not carry $VERSION_NUMBER.0"
grep -q "version=\"$VERSION_NUMBER\.0\"" src/SnipText.manifest \
    || fail "src/SnipText.manifest does not carry $VERSION_NUMBER.0"

NOTES="docs/RELEASE-NOTES-$TAG.md"
[[ -f "$NOTES" ]] || fail "$NOTES is missing — the release body would fall back to the whole changelog"
echo "  VERSION, resource.h, manifest and $NOTES all agree on $VERSION_NUMBER."

# --- 3. the tag must not already exist --------------------------------------
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    fail "$TAG already exists locally. Remove it and its release first:
    gh release delete $TAG --cleanup-tag --yes
  or, if there is no release:
    git tag -d $TAG && git push origin :refs/tags/$TAG"
fi
if git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1; then
    fail "$TAG already exists on the remote. Remove it first:
    gh release delete $TAG --cleanup-tag --yes"
fi

# --- 4. commit whatever is outstanding --------------------------------------
say "Committing"
git add -A
if git diff --cached --quiet; then
    echo "  Nothing to commit; the working tree was already clean."
else
    git commit -m "Release $TAG"
    echo "  Committed."
fi

# The tag is about to point at HEAD, so HEAD had better be everything.
git diff --quiet && git diff --cached --quiet \
    || fail "the working tree is still dirty after committing — stop and look"

say "Pushing the branch"
git push

# --- 5. only now, tag ---------------------------------------------------------
say "Tagging $TAG at $(git rev-parse --short HEAD)"
git tag -a "$TAG" -m "$TAG"
git push origin "$TAG"

say "Done"
cat <<EOF
  $TAG -> $(git rev-parse --short HEAD) $(git log -1 --format=%s)

  CI is now building it. It will refuse the tag if the version or the
  release notes disagree with it, so a green run means the binary really
  does say $VERSION_NUMBER.

  Watch:   gh run watch
  Release: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/$TAG
EOF
