#!/usr/bin/env bash
set -euo pipefail

BASE_REF="${1:-remotes/vitaki/ywnico}"
TOP_N="${2:-25}"

if ! git rev-parse --verify --quiet "$BASE_REF" >/dev/null; then
  echo "error: base ref '$BASE_REF' not found"
  echo "hint: run 'git fetch vitaki' or pass a valid ref"
  exit 1
fi

HEAD_FILES="$(mktemp)"
BASE_FILES="$(mktemp)"
COMMON_FILES="$(mktemp)"
trap 'rm -f "$HEAD_FILES" "$BASE_FILES" "$COMMON_FILES"' EXIT

git ls-tree -r --name-only HEAD vita/src vita/include | sort > "$HEAD_FILES"
git ls-tree -r --name-only "$BASE_REF" vita/src vita/include | sort > "$BASE_FILES"
comm -12 "$HEAD_FILES" "$BASE_FILES" > "$COMMON_FILES"

echo "== Vita file inflation report (HEAD vs $BASE_REF) =="
echo "delta\thead\tbase\tpath"
while IFS= read -r path; do
  head_lines="$(git show "HEAD:$path" | wc -l | tr -d ' ')"
  base_lines="$(git show "$BASE_REF:$path" | wc -l | tr -d ' ')"
  delta=$((head_lines - base_lines))
  if [ "$delta" -gt 0 ]; then
    printf "%5d\t%5d\t%5d\t%s\n" "$delta" "$head_lines" "$base_lines" "$path"
  fi
done < "$COMMON_FILES" | sort -nr | head -n "$TOP_N"

echo
HEAD_TOTAL="$(find vita/src vita/include -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 wc -l | tail -n1 | awk '{print $1}')"
BASE_TOTAL=0
while IFS= read -r path; do
  lines="$(git show "$BASE_REF:$path" 2>/dev/null | wc -l | tr -d ' ')"
  BASE_TOTAL=$((BASE_TOTAL + lines))
done < "$BASE_FILES"

echo "== Totals =="
echo "head_total=$HEAD_TOTAL"
echo "base_total=$BASE_TOTAL"
echo "delta_total=$((HEAD_TOTAL - BASE_TOTAL))"

echo
if command -v rg >/dev/null 2>&1; then
  echo "== Largest current Vita files =="
  find vita/src vita/include -type f \( -name '*.c' -o -name '*.h' \) -print0 \
    | xargs -0 wc -l | sort -nr | head -n "$TOP_N"
fi
