#!/usr/bin/env bash
# Works out the version of the next release. Used by .github/workflows/create_release.yml
# and runnable locally from inside the git checkout (it only reads tags).
#
# Usage: tools/next_version.sh <patch|minor|major|beta>
# Prints KEY=VALUE lines: LATEST_TAG (last stable tag, pre-release tags ignored),
# NEW_VERSION (plain MAJOR.MINOR.PATCH, what build.sh takes) and NEW_TAG
# (vNEW_VERSION, or vNEW_VERSION-beta.N for a beta; SemVer 2.0.0 section 9).
set -euo pipefail

VERSION_TYPE=${1:?usage: $0 <patch|minor|major|beta>}

# Last stable tag: a tag with a '-' suffix is a pre-release and never the base of a bump.
LATEST_TAG=$(git describe --tags --abbrev=0 --match "v[0-9]*" --exclude "*-*" 2>/dev/null || echo "v0.0.0")

IFS='.' read -r MAJOR MINOR PATCH <<< "${LATEST_TAG#v}"

case $VERSION_TYPE in
  major) MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0 ;;
  minor) MINOR=$((MINOR + 1)); PATCH=0 ;;
  patch | beta) PATCH=$((PATCH + 1)) ;;
  *) echo "error: unknown version type '$VERSION_TYPE'" >&2; exit 2 ;;
esac

NEW_VERSION="$MAJOR.$MINOR.$PATCH"
NEW_TAG="v$NEW_VERSION"

if [ "$VERSION_TYPE" = beta ]; then
  # Highest existing beta number for this version, compared as a number (beta.10 > beta.9).
  LAST_BETA=$(git tag --list "v${NEW_VERSION}-beta.*" | sed 's/.*-beta\.//' | grep -E '^[0-9]+$' | sort -n | tail -1 || true)
  NEW_TAG="v${NEW_VERSION}-beta.$(( ${LAST_BETA:-0} + 1 ))"
fi

echo "LATEST_TAG=$LATEST_TAG"
echo "NEW_VERSION=$NEW_VERSION"
echo "NEW_TAG=$NEW_TAG"
