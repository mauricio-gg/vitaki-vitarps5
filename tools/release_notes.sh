#!/usr/bin/env bash
# Builds the full GitHub release body (markdown) for a VitaRPS5 release.
# Used by .github/workflows/create_release.yml and runnable locally as a dry run.
#
# Usage: tools/release_notes.sh <prev_tag> <new_tag> <vpk_name> <commit_sha> <build_date> <repo> [out_file]
#   repo is owner/name, used for PR links. Writes to out_file, or stdout if omitted.
# Run from inside the git checkout. Summarises `git log <prev_tag>..HEAD --no-merges`.
set -euo pipefail

if [ $# -lt 6 ] || [ $# -gt 7 ]; then
  echo "usage: $0 <prev_tag> <new_tag> <vpk_name> <commit_sha> <build_date> <repo> [out_file]" >&2
  exit 2
fi
PREV_TAG=$1 NEW_TAG=$2 VPK_NAME=$3 COMMIT_SHA=$4 BUILD_DATE=$5 REPO=$6 OUT=${7:-}

if ! git rev-parse -q --verify "refs/tags/${PREV_TAG}^{commit}" >/dev/null; then
  echo "error: previous tag '$PREV_TAG' does not exist" >&2
  exit 1
fi

FEATURES="" FIXES="" PERF="" OTHER=""
NL=$'\n'
TYPE_RE='^([A-Za-z]+)(\(([^)]*)\))?(!)?:[[:space:]]*(.*)$'
PR_RE='^(.*[^[:space:]])[[:space:]]*\(#([0-9]+)\)$'

while IFS=$'\t' read -r sha subject; do
  [ -n "$sha" ] || continue
  # Version bumps are noise whatever their prefix.
  shopt -s nocasematch
  if [[ $subject =~ (bump(ed)?[[:space:]]+(the[[:space:]]+)?version|version[[:space:]]+bump) ]]; then
    shopt -u nocasematch
    continue
  fi
  shopt -u nocasematch

  type="" scope="" desc=$subject
  if [[ $subject =~ $TYPE_RE ]]; then
    type=$(printf '%s' "${BASH_REMATCH[1]}" | tr '[:upper:]' '[:lower:]')
    scope=${BASH_REMATCH[3]}
    desc=${BASH_REMATCH[5]}
  fi

  case "$type" in
    chore | docs | ci | test | refactor | build | style) continue ;;
  esac

  # The PR number is the last (#n) on the line; without one, link nothing and show the short SHA.
  ref="$sha"
  if [[ $desc =~ $PR_RE ]]; then
    desc=${BASH_REMATCH[1]}
    ref="[#${BASH_REMATCH[2]}](https://github.com/${REPO}/pull/${BASH_REMATCH[2]})"
  fi

  prefix=""
  [ -n "$scope" ] && prefix="**${scope}:** "
  line="- ${prefix}${desc} (${ref})"

  case "$type" in
    feat) FEATURES+="${line}${NL}" ;;
    fix) FIXES+="${line}${NL}" ;;
    perf) PERF+="${line}${NL}" ;;
    *) OTHER+="- ${type:+${type}: }${prefix}${desc} (${ref})${NL}" ;;
  esac
done < <(git log "${PREV_TAG}..HEAD" --no-merges --format='%h%x09%s')

section() { # title, body
  [ -n "$2" ] && printf '### %s\n\n%s\n' "$1" "$2"
  return 0
}

changes() {
  printf '## What'"'"'s Changed\n\n'
  if [ -z "${FEATURES}${FIXES}${PERF}${OTHER}" ]; then
    printf 'No user-visible changes since %s.\n' "$PREV_TAG"
    return
  fi
  section "Features" "$FEATURES"
  section "Fixes" "$FIXES"
  section "Performance" "$PERF"
  section "Other changes" "$OTHER"
}

body() {
  cat <<EOT
# VitaRPS5 ${NEW_TAG}

**Enhanced PlayStation 5 Remote Play for PS Vita**

Built with modern UI, optimized performance, and quality-of-life improvements.

## 📦 Installation

1. Download \`${VPK_NAME}\` below
2. Transfer to your PS Vita (via USB or FTP)
3. Install using VitaShell (press X on the VPK file)
4. Launch from LiveArea

EOT
  changes
  cat <<EOT
## 🛠️ Build Information

- **Build System:** Docker + VitaSDK
- **Version:** ${NEW_TAG}
- **Commit:** ${COMMIT_SHA}
- **Build Date:** ${BUILD_DATE}

---

Built with ❤️ for the PS Vita community over 3+ months of design and development.

☕ **Support development:** [Buy Me a Coffee](https://buymeacoffee.com/solidem)
EOT
}

if [ -n "$OUT" ]; then body >"$OUT"; else body; fi
