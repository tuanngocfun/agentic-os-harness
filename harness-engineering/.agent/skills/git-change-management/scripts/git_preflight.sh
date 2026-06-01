#!/usr/bin/env bash
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HARNESS_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
REPO_ROOT="${REPO_ROOT:-}"

if [ -z "$REPO_ROOT" ]; then
  REPO_ROOT="$(git -C "$HARNESS_ROOT" rev-parse --show-toplevel 2>/dev/null || true)"
fi

if [ -z "$REPO_ROOT" ] || ! git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "[FAIL] not inside a Git repository"
  exit 1
fi

failures=0

fail() {
  echo "[FAIL] $*"
  failures=$((failures + 1))
}

pass() {
  echo "[PASS] $*"
}

section() {
  echo
  echo "== $* =="
}

section "Repository"
echo "repo_root: $REPO_ROOT"
echo "branch: $(git -C "$REPO_ROOT" branch --show-current || true)"
echo "head: $(git -C "$REPO_ROOT" rev-parse --short HEAD || true)"
upstream="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || true)"
echo "upstream: ${upstream:-<none>}"

section "Status"
git -C "$REPO_ROOT" status --short --branch

section "Diff summary"
git -C "$REPO_ROOT" diff --stat || true
git -C "$REPO_ROOT" diff --cached --stat || true

section "Untracked files"
untracked="$(git -C "$REPO_ROOT" ls-files --others --exclude-standard || true)"
if [ -n "$untracked" ]; then
  echo "$untracked"
else
  pass "no untracked files"
fi

section "Tracked generated artifacts"
tracked_artifacts="$(git -C "$REPO_ROOT" ls-files -- build '*.o' '*.elf' '*.bin' '*.img' '*.iso' '*.map' '*.log' || true)"
if [ -n "$tracked_artifacts" ]; then
  echo "$tracked_artifacts"
  fail "generated artifact path is tracked"
else
  pass "no generated build artifact path is tracked"
fi

section "Gitignore contract"
for pattern in 'build/' '*.o' '*.elf' '*.bin' '*.img' '*.iso' '*.map' '*.log'; do
  if grep -Fxq "$pattern" "$REPO_ROOT/.gitignore"; then
    pass ".gitignore contains $pattern"
  else
    fail ".gitignore missing $pattern"
  fi
done

section "Staged deletion check"
if git -C "$REPO_ROOT" diff --cached --name-status | grep -Eq '^D[[:space:]]'; then
  git -C "$REPO_ROOT" diff --cached --name-status
  fail "staged deletion requires explicit user confirmation"
else
  pass "no staged deletions"
fi

section "Summary"
if [ "$failures" -eq 0 ]; then
  echo "=== GIT PREFLIGHT PASSED ==="
  exit 0
fi

echo "=== GIT PREFLIGHT FAILED: $failures issue(s) ==="
exit 1
