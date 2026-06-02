#!/usr/bin/env bash
set -eu

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
cd "$ROOT"
REPO_ROOT="$(cd "$ROOT/.." && pwd)"

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
require_file "13-agent-routing-and-risk/README.md"
require_file "harness_profile.yaml"
require_file ".agent/skills/compile-and-run/SKILL.md"
require_file ".agent/skills/compile-and-run/scripts/boot_test.sh"
require_file ".agent/skills/regression-validation/SKILL.md"
require_file ".agent/skills/regression-validation/scripts/check_harness_contract.sh"
require_file ".agent/skills/git-change-management/SKILL.md"
require_file ".agent/skills/git-change-management/scripts/git_preflight.sh"
require_file "../scripts/boot_test.sh"
require_file "../scripts/shell_test.sh"
require_file "../scripts/syscall_test.sh"
require_file "../scripts/exception_test.sh"
require_file "../scripts/scheduler_test.sh"
require_file "../scripts/paging_test.sh"

require_grep 'AGENTS[.]md' "llms.txt"
require_grep '06-validation/README[.]md' "llms.txt"
require_grep '09-safety-and-security/README[.]md' "llms.txt"
require_grep '12-git-change-management/README[.]md' "llms.txt"
require_grep '13-agent-routing-and-risk/README[.]md' "llms.txt"
require_grep 'harness_profile[.]yaml' "llms.txt"
require_grep '[.]agent/skills/compile-and-run/SKILL[.]md' "llms.txt"
require_grep '[.]agent/skills/git-change-management/SKILL[.]md' "llms.txt"
require_grep 'git_preflight[.]sh' "06-validation/README.md"
require_grep 'explicit user request' "12-git-change-management/README.md"
require_grep 'Stage explicit file paths only|stage explicit file paths only' "12-git-change-management/README.md"
require_grep 'qemu exited before timeout' ".agent/skills/compile-and-run/scripts/boot_test.sh"
require_grep 'kernel_sector_limit:[[:space:]]*120' "harness_profile.yaml"
require_grep 'project_phase:[[:space:]]*"boot_to_shell_proven_advanced_core_scaffold"' "harness_profile.yaml"
require_grep 'format_policy:' "harness_profile.yaml"
require_grep 'runtime_evidence:[[:space:]]*"jsonl"' "harness_profile.yaml"
require_grep 'claim_status:' "harness_profile.yaml"
if grep -q 'ENABLE_SYSCALL_ABI_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-syscall' "$REPO_ROOT/Makefile" && grep -q 'SYSCALL_ABI_OK:ARGS_OK' "$REPO_ROOT/scripts/syscall_test.sh"; then
  require_grep 'syscall:[[:space:]]*"claimable_with_syscall_test"' "harness_profile.yaml"
else
  require_grep 'syscall:[[:space:]]*"not_claimable_syscall_abi_unproven"' "harness_profile.yaml"
fi
if grep -q 'ENABLE_EXCEPTION_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-exception' "$REPO_ROOT/Makefile" && grep -q 'KERNEL_PANIC:0x00000006:0x00000000' "$REPO_ROOT/scripts/exception_test.sh"; then
  require_grep 'exception_panic:[[:space:]]*"claimable_with_exception_test_suite"' "harness_profile.yaml"
  require_grep 'test-deep:.*test-exception-div0.*test-exception-gpf.*test-exception-pagefault' "$REPO_ROOT/Makefile"
fi
if grep -q 'ENABLE_SCHEDULER_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-scheduler' "$REPO_ROOT/Makefile" && grep -q 'SCHED_QUEUE_OK' "$REPO_ROOT/scripts/scheduler_test.sh"; then
  require_grep 'scheduler:[[:space:]]*"partial_claimable_queue_rotation_test"' "harness_profile.yaml"
else
  require_grep 'scheduler:[[:space:]]*"not_claimable_no_timer_driven_context_switch"' "harness_profile.yaml"
fi
if grep -q 'ENABLE_PAGING_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-paging' "$REPO_ROOT/Makefile" && grep -q 'PAGING_UNMAP_OK' "$REPO_ROOT/scripts/paging_test.sh"; then
  require_grep 'paging:[[:space:]]*"partial_claimable_map_unmap_bookkeeping_test"' "harness_profile.yaml"
fi
require_grep 'user_mode:[[:space:]]*"not_claimable_no_ring3_transition_test"' "harness_profile.yaml"
require_grep 'syscall-abi-proof' "harness_profile.yaml"
require_grep 'exception-panic-path' "harness_profile.yaml"
require_grep 'Do not add filesystem' "13-agent-routing-and-risk/README.md"
require_grep 'Claim-aware routing matrix|Claim-aware MiMo routing|Routing Matrix' "13-agent-routing-and-risk/README.md"
require_grep 'Loop Traps Diagnosed' "13-agent-routing-and-risk/README.md"
require_grep 'Format Policy' "13-agent-routing-and-risk/README.md"
require_grep 'Default Gates vs Deep Gates' "06-validation/README.md"
require_grep 'Format Contract' "06-validation/README.md"
require_grep 'harness_profile[.]yaml' "AGENTS.md"
require_grep 'harness_profile[.]yaml' "06-validation/README.md"

if ! rg -q 'scheduler_tick[[:space:]]*[(]' "$REPO_ROOT/kernel/timer.c" "$REPO_ROOT/kernel/isr.asm" 2>/dev/null; then
  if grep -Eq 'scheduler:[[:space:]]*"claimable_with_scheduler_test"|scheduler:[[:space:]]*".*context' "harness_profile.yaml"; then
    fail "scheduler profile overclaims timer-driven context switching"
  else
    pass "scheduler profile does not overclaim timer-driven context switching"
  fi
fi

if ! rg -q 'switch_to_usermode' "$REPO_ROOT/kernel/kernel.c" "$REPO_ROOT/kernel/process.c" "$REPO_ROOT/kernel/scheduler.c" 2>/dev/null; then
  require_grep 'user_mode:[[:space:]]*"not_claimable_no_ring3_transition_test"' "harness_profile.yaml"
fi

if rg -q 'int [$]0x80.*"b"' "$REPO_ROOT/kernel/kernel.c" 2>/dev/null && ! grep -q 'ENABLE_SYSCALL_ABI_SELFTEST' "$REPO_ROOT/kernel/kernel.c"; then
  require_grep 'syscall:[[:space:]]*"not_claimable_syscall_abi_unproven"' "harness_profile.yaml"
fi

if command -v rg >/dev/null 2>&1; then
  rg -n 'nasm -f elf32[[:space:]]+boot/boot[.]asm' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "boot sector ELF stale pattern found" || pass "no boot sector ELF stale pattern"
  rg -n 'dd if=build/boot[.]o' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "boot.o dd stale pattern found" || pass "no boot.o dd stale pattern"
  rg -n 'OBJECTS[[:space:]]*=.*BOOT' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "bootloader object in kernel objects pattern found" || pass "no bootloader object pattern"
  rg -n 'an toàn 100[%]|isolated[[:space:]]+completely' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "absolute safety claim found" || pass "no absolute safety claim"
  rg -n 'stat -c%s|grep -q "\$marker"|> "\$SERIAL_LOG" 2>&1 \|\| true|-serial mon:stdio.*>.*build/serial[.]log' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "weak parser/QEMU evidence pattern found" || pass "no weak parser/QEMU evidence pattern"
  rg -n 'echo "\[PASS\].*exception.*"|EXCEPTION PANIC TEST PASSED' "$REPO_ROOT/scripts/exception_test.sh" | rg -v 'panic_present|structured|===' && fail "exception test can pass without proving a fault" || pass "exception test requires structured panic evidence"
  rg -n 'int [$]0x0D' "$REPO_ROOT/kernel/kernel.c" && fail "GPF selftest uses software interrupt instead of protection violation" || pass "GPF selftest does not use software interrupt"
  rg -n 'echo "\[PASS\].*echo|echo command not listed in help|echo ok command returned ok' "$REPO_ROOT/scripts/shell_test.sh" && fail "shell test claims echo proof through flaky monitor input" || pass "shell test does not overclaim echo"
  rg -n 'context switch verified|SCHED_A|SCHED_B' "$REPO_ROOT/scripts/scheduler_test.sh" && fail "scheduler test overclaims context switch or printed-task markers" || pass "scheduler test requires queue-rotation evidence"
  rg -n 'no paging test markers|paging semantics verified' "$REPO_ROOT/scripts/paging_test.sh" && fail "paging test can pass without paging proof" || pass "paging test requires map/unmap evidence"
  rg -n 'scheduler:[[:space:]]*"claimable_with_scheduler_test"|paging:[[:space:]]*"claimable_with_paging_test"' "harness_profile.yaml" && fail "harness profile has stale broad core claims" || pass "harness profile uses narrowed core claims"
else
  fail "rg not installed"
fi

if [ "$failures" -eq 0 ]; then
  echo "=== HARNESS CONTRACT PASSED ==="
  exit 0
fi

echo "=== HARNESS CONTRACT FAILED: $failures issue(s) ==="
exit 1
