#!/usr/bin/env bash
set -eu

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
cd "$ROOT"

failures=0

fail() {
  echo "[FAIL] $*"
  failures=$((failures + 1))
}

pass() {
  echo "[PASS] $*"
}

require_file() {
  [ -f "$1" ] && pass "$1 exists" || fail "$1 missing"
}

require_grep() {
  pattern="$1"
  file="$2"
  if grep -Eq "$pattern" "$file"; then
    pass "$file contains $pattern"
  else
    fail "$file missing $pattern"
  fi
}

require_file "AGENTS.md"
require_file "llms.txt"
require_file "06-validation/README.md"
require_file "09-safety-and-security/README.md"
require_file "12-git-change-management/README.md"
require_file ".agent/skills/compile-and-run/SKILL.md"
require_file ".agent/skills/compile-and-run/scripts/boot_test.sh"
require_file ".agent/skills/regression-validation/SKILL.md"
require_file ".agent/skills/regression-validation/scripts/check_harness_contract.sh"
require_file ".agent/skills/git-change-management/SKILL.md"
require_file ".agent/skills/git-change-management/scripts/git_preflight.sh"

require_grep 'AGENTS[.]md' "llms.txt"
require_grep '06-validation/README[.]md' "llms.txt"
require_grep '09-safety-and-security/README[.]md' "llms.txt"
require_grep '12-git-change-management/README[.]md' "llms.txt"
require_grep '[.]agent/skills/compile-and-run/SKILL[.]md' "llms.txt"
require_grep '[.]agent/skills/git-change-management/SKILL[.]md' "llms.txt"
require_grep 'git_preflight[.]sh' "06-validation/README.md"
require_grep 'explicit user request' "12-git-change-management/README.md"
require_grep 'Stage explicit file paths only|stage explicit file paths only' "12-git-change-management/README.md"
require_grep 'qemu exited before timeout' ".agent/skills/compile-and-run/scripts/boot_test.sh"

if command -v rg >/dev/null 2>&1; then
  rg -n 'nasm -f elf32[[:space:]]+boot/boot[.]asm' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "boot sector ELF stale pattern found" || pass "no boot sector ELF stale pattern"
  rg -n 'dd if=build/boot[.]o' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "boot.o dd stale pattern found" || pass "no boot.o dd stale pattern"
  rg -n 'OBJECTS[[:space:]]*=.*BOOT' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "bootloader object in kernel objects pattern found" || pass "no bootloader object pattern"
  rg -n 'an toàn 100[%]|isolated[[:space:]]+completely' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "absolute safety claim found" || pass "no absolute safety claim"
  rg -n 'stat -c%s|grep -q "\$marker"|> "\$SERIAL_LOG" 2>&1 \|\| true|-serial mon:stdio.*>.*build/serial[.]log' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "weak parser/QEMU evidence pattern found" || pass "no weak parser/QEMU evidence pattern"
else
  fail "rg not installed"
fi

if [ "$failures" -eq 0 ]; then
  echo "=== HARNESS CONTRACT PASSED ==="
  exit 0
fi

echo "=== HARNESS CONTRACT FAILED: $failures issue(s) ==="
exit 1
