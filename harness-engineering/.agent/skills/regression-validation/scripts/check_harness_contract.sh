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
require_file "../scripts/memory_test.sh"
require_file "../scripts/usermode_test.sh"
require_file "../scripts/timer_test.sh"
require_file "../scripts/timer_preemption_test.sh"
require_file "../scripts/allocator_test.sh"
require_file "../scripts/address_space_test.sh"
require_file "../scripts/syscall_negative_test.sh"
require_file "../scripts/e820_test.sh"
require_file "../scripts/ramdisk_test.sh"
require_file "../scripts/scheduler_safety_test.sh"
require_file "../scripts/shell_io_test.sh"
require_file "../boot/stage2.asm"

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
require_grep 'KERNEL_DEFINES_STAMP' "$REPO_ROOT/Makefile"
require_grep 'kernel_defines[.]stamp' "$REPO_ROOT/Makefile"
require_grep 'FORCE' "$REPO_ROOT/Makefile"
require_grep 'stage2_reserved_sectors:[[:space:]]*32' "harness_profile.yaml"
require_grep 'kernel_lba_start:[[:space:]]*33' "harness_profile.yaml"
require_grep 'loader:[[:space:]]*"stage2_lba_loader"' "harness_profile.yaml"
require_grep 'STAGE2_LOAD_SECTORS' "$REPO_ROOT/boot/boot.asm"
require_grep 'KERNEL_LBA_START' "$REPO_ROOT/boot/stage2.asm"
require_grep 'STAGE2_OK' "$REPO_ROOT/boot/stage2.asm"
require_grep 'project_phase:[[:space:]]*"boot_to_shell_proven_stage2_preemptive_memory_ramdisk_scaffold"' "harness_profile.yaml"
require_grep 'format_policy:' "harness_profile.yaml"
require_grep 'runtime_evidence:[[:space:]]*"jsonl"' "harness_profile.yaml"
require_grep 'claim_status:' "harness_profile.yaml"
require_grep 'bootloader:[[:space:]]*"claimable_with_stage2_make_test"' "harness_profile.yaml"
require_grep 'STAGE2_BIN' "$REPO_ROOT/Makefile"
require_grep 'STAGE2_RESERVED_SECTORS[[:space:]]*=[[:space:]]*32' "$REPO_ROOT/Makefile"
require_grep 'KERNEL_LBA_START[[:space:]]*=[[:space:]]*33' "$REPO_ROOT/Makefile"
require_grep 'stage2[.]bin' "$REPO_ROOT/scripts/boot_test.sh"
require_grep 'STAGE2_OK' "$REPO_ROOT/scripts/boot_test.sh"
if grep -q 'ENABLE_SYSCALL_ABI_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-syscall' "$REPO_ROOT/Makefile" && grep -q 'SYSCALL_ABI_OK:ARGS_OK' "$REPO_ROOT/scripts/syscall_test.sh"; then
  if grep -q 'ENABLE_SYSCALL_NEGATIVE_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'SYS_TEST_MARKER' "$REPO_ROOT/include/syscall.h" && grep -q 'test-syscall-negative' "$REPO_ROOT/Makefile" && grep -q 'SYSCALL_UNMAPPED_POINTER_OK' "$REPO_ROOT/scripts/syscall_negative_test.sh"; then
    require_grep 'syscall:[[:space:]]*"claimable_with_syscall_and_ring3_negative_path_tests"' "harness_profile.yaml"
    require_grep 'test-deep:.*test-syscall-negative' "$REPO_ROOT/Makefile"
  else
    require_grep 'syscall:[[:space:]]*"claimable_with_syscall_test"' "harness_profile.yaml"
  fi
else
  require_grep 'syscall:[[:space:]]*"not_claimable_syscall_abi_unproven"' "harness_profile.yaml"
fi
if grep -q 'ENABLE_EXCEPTION_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-exception' "$REPO_ROOT/Makefile" && grep -q 'KERNEL_PANIC:0x00000006:0x00000000' "$REPO_ROOT/scripts/exception_test.sh"; then
  require_grep 'exception_panic:[[:space:]]*"claimable_with_exception_test_suite"' "harness_profile.yaml"
  require_grep 'test-deep:.*test-exception-div0.*test-exception-gpf.*test-exception-pagefault' "$REPO_ROOT/Makefile"
fi
if grep -q 'ENABLE_SCHEDULER_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-scheduler' "$REPO_ROOT/Makefile" && grep -q 'SCHED_QUEUE_OK' "$REPO_ROOT/scripts/scheduler_test.sh"; then
  if grep -q 'ENABLE_TIMER_PREEMPTION_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-timer-preemption' "$REPO_ROOT/Makefile" && grep -q 'PREEMPT_OK' "$REPO_ROOT/scripts/timer_preemption_test.sh"; then
    if grep -q 'ENABLE_SCHEDULER_SAFETY_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'scheduler_set_priority' "$REPO_ROOT/kernel/scheduler.c" && grep -q 'test-scheduler-safety' "$REPO_ROOT/Makefile" && grep -q 'SCHED_SAFETY_OK' "$REPO_ROOT/scripts/scheduler_safety_test.sh"; then
      require_grep 'scheduler:[[:space:]]*"claimable_with_scheduler_preemption_priority_fairness_tests"' "harness_profile.yaml"
      require_grep 'test-deep:.*test-scheduler.*test-timer-preemption.*test-scheduler-safety' "$REPO_ROOT/Makefile"
    else
      require_grep 'scheduler:[[:space:]]*"claimable_with_scheduler_explicit_context_switch_and_timer_preemption_tests"' "harness_profile.yaml"
      require_grep 'test-deep:.*test-scheduler.*test-timer-preemption' "$REPO_ROOT/Makefile"
    fi
  elif grep -q 'SCHED_A' "$REPO_ROOT/scripts/scheduler_test.sh" && grep -q 'SCHED_B' "$REPO_ROOT/scripts/scheduler_test.sh" && grep -q 'SCHED_CONTEXT_OK' "$REPO_ROOT/scripts/scheduler_test.sh"; then
    require_grep 'scheduler:[[:space:]]*"claimable_with_scheduler_explicit_context_switch_test"' "harness_profile.yaml"
    require_grep 'test-deep:.*test-scheduler' "$REPO_ROOT/Makefile"
  else
    require_grep 'scheduler:[[:space:]]*"partial_claimable_queue_rotation_test"' "harness_profile.yaml"
  fi
else
  require_grep 'scheduler:[[:space:]]*"not_claimable_no_timer_driven_context_switch"' "harness_profile.yaml"
fi
if grep -q 'ENABLE_PAGING_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-paging' "$REPO_ROOT/Makefile" && grep -q 'PAGING_UNMAP_OK' "$REPO_ROOT/scripts/paging_test.sh"; then
  if grep -q 'ENABLE_ADDRESS_SPACE_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-address-space' "$REPO_ROOT/Makefile" && grep -q 'ADDRSPACE_ISOLATION_OK' "$REPO_ROOT/scripts/address_space_test.sh"; then
    require_grep 'paging:[[:space:]]*"claimable_with_map_fault_user_supervisor_and_address_space_tests"' "harness_profile.yaml"
    require_grep 'test-deep:.*test-address-space' "$REPO_ROOT/Makefile"
  elif grep -q 'PAGING_USER_SUPERVISOR_FAULT_OK' "$REPO_ROOT/scripts/usermode_test.sh" && grep -q 'test-usermode' "$REPO_ROOT/Makefile"; then
    require_grep 'paging:[[:space:]]*"partial_claimable_map_unmap_permission_fault_invalidation_user_supervisor_test"' "harness_profile.yaml"
    require_grep 'test-deep:.*test-usermode' "$REPO_ROOT/Makefile"
  elif grep -q 'PAGING_WRITE_FAULT_OK' "$REPO_ROOT/scripts/paging_test.sh" && grep -q 'PAGING_UNMAP_FAULT_OK' "$REPO_ROOT/scripts/paging_test.sh" && grep -q '0x00010000' "$REPO_ROOT/kernel/paging.c"; then
    require_grep 'paging:[[:space:]]*"partial_claimable_map_unmap_permission_fault_invalidation_test"' "harness_profile.yaml"
  elif grep -q 'PAGING_PERM_OK' "$REPO_ROOT/scripts/paging_test.sh"; then
    require_grep 'paging:[[:space:]]*"partial_claimable_map_unmap_permission_bookkeeping_test"' "harness_profile.yaml"
  else
    require_grep 'paging:[[:space:]]*"partial_claimable_map_unmap_bookkeeping_test"' "harness_profile.yaml"
  fi
fi
if grep -q 'ENABLE_TIMER_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-timer' "$REPO_ROOT/Makefile" && grep -q 'TIMER_TICKS_OK' "$REPO_ROOT/scripts/timer_test.sh"; then
  require_grep 'timer_ticks:[[:space:]]*"claimable_with_timer_test"' "harness_profile.yaml"
  require_grep 'test-deep:.*test-timer' "$REPO_ROOT/Makefile"
fi
if grep -q 'ENABLE_MEMORY_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-memory' "$REPO_ROOT/Makefile" && grep -q 'MEMORY_DETECT_OK' "$REPO_ROOT/scripts/memory_test.sh"; then
  if grep -q 'ENABLE_E820_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-e820-frame' "$REPO_ROOT/Makefile" && grep -q 'E820_FRAME_OK' "$REPO_ROOT/scripts/e820_test.sh"; then
    require_grep 'memory_info:[[:space:]]*"claimable_with_e820_memory_detection_test"' "harness_profile.yaml"
    require_grep 'frame_allocator:[[:space:]]*"claimable_with_e820_frame_lifecycle_test"' "harness_profile.yaml"
    require_grep 'test-deep:.*test-e820-frame' "$REPO_ROOT/Makefile"
  else
    require_grep 'memory_info:[[:space:]]*"claimable_with_memory_detection_test"' "harness_profile.yaml"
  fi
  require_grep 'test-deep:.*test-memory' "$REPO_ROOT/Makefile"
else
  require_grep 'memory_info:[[:space:]]*"stubbed_not_claimable"' "harness_profile.yaml"
fi
if grep -q 'ENABLE_ALLOCATOR_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-allocator' "$REPO_ROOT/Makefile" && grep -q 'ALLOCATOR_OK' "$REPO_ROOT/scripts/allocator_test.sh"; then
  require_grep 'allocator:[[:space:]]*"claimable_with_allocator_runtime_test"' "harness_profile.yaml"
  require_grep 'test-deep:.*test-allocator' "$REPO_ROOT/Makefile"
else
  require_grep 'allocator:[[:space:]]*"not_claimable_allocator_unproven"' "harness_profile.yaml"
fi
if grep -q 'ENABLE_RAMDISK_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-ramdisk' "$REPO_ROOT/Makefile" && grep -q 'RAMDISK_OK' "$REPO_ROOT/scripts/ramdisk_test.sh"; then
  require_grep 'block_device:[[:space:]]*"claimable_with_ramdisk_runtime_test"' "harness_profile.yaml"
  require_grep 'test-deep:.*test-ramdisk' "$REPO_ROOT/Makefile"
else
  require_grep 'block_device:[[:space:]]*"not_started"' "harness_profile.yaml"
fi
if grep -q 'ENABLE_USERMODE_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-usermode' "$REPO_ROOT/Makefile" && grep -q 'USERMODE_RING3_OK' "$REPO_ROOT/scripts/usermode_test.sh" && rg -q 'enter_user_mode|switch_to_usermode' "$REPO_ROOT/kernel" "$REPO_ROOT/include" 2>/dev/null; then
  require_grep 'user_mode:[[:space:]]*"claimable_with_usermode_ring3_test"' "harness_profile.yaml"
  if grep -q 'ENABLE_ADDRESS_SPACE_SELFTEST' "$REPO_ROOT/kernel/kernel.c" && grep -q 'test-address-space' "$REPO_ROOT/Makefile" && grep -q 'ADDRSPACE_ISOLATION_OK' "$REPO_ROOT/scripts/address_space_test.sh"; then
    require_grep 'process:[[:space:]]*"claimable_with_user_process_record_ring3_entry_and_address_space_isolation_tests"' "harness_profile.yaml"
  else
    require_grep 'process:[[:space:]]*"partial_claimable_user_process_record_ring3_entry_test"' "harness_profile.yaml"
  fi
  require_grep 'test-deep:.*test-usermode' "$REPO_ROOT/Makefile"
else
  require_grep 'user_mode:[[:space:]]*"not_claimable_no_ring3_transition_test"' "harness_profile.yaml"
fi
if grep -q 'test-shell-io' "$REPO_ROOT/Makefile" && grep -q 'SHELL_ECHO_OK' "$REPO_ROOT/scripts/shell_io_test.sh"; then
  require_grep 'shell_echo:[[:space:]]*"claimable_with_shell_io_test"' "harness_profile.yaml"
  require_grep 'test-deep:.*test-shell-io' "$REPO_ROOT/Makefile"
fi
require_grep 'pending_deep_gates:[[:space:]]*\[\]' "harness_profile.yaml"
if grep -Eq 'scheduler-timer-preemption-proof|process-address-space-isolation-proof|memory-allocator-proof|paging-per-process-address-space-proof|syscall-user-mode-negative-path-proof|memory-map-and-frame-lifecycle-proof|scheduler-safety-proof' "harness_profile.yaml"; then
  fail "completed P0/P1/P2 proof tasks are still listed as next work"
else
  pass "completed core proof tasks are removed from next work"
fi
require_grep 'Do not add filesystem' "13-agent-routing-and-risk/README.md"
require_grep 'Claim-aware routing matrix|Claim-aware MiMo routing|Routing Matrix' "13-agent-routing-and-risk/README.md"
require_grep 'Loop Traps Diagnosed' "13-agent-routing-and-risk/README.md"
require_grep 'Format Policy' "13-agent-routing-and-risk/README.md"
require_grep 'Default Gates vs Deep Gates' "06-validation/README.md"
require_grep 'Format Contract' "06-validation/README.md"
require_grep 'harness_profile[.]yaml' "AGENTS.md"
require_grep 'harness_profile[.]yaml' "06-validation/README.md"

if grep -Eq 'scheduler:[[:space:]]*".*(timer|preempt)' "harness_profile.yaml"; then
  if grep -q 'timer_interrupt' "$REPO_ROOT/kernel/timer.c" && grep -q 'scheduler_preempt' "$REPO_ROOT/kernel/scheduler.c" && grep -q 'PREEMPT_OK' "$REPO_ROOT/scripts/timer_preemption_test.sh"; then
    pass "scheduler preemption claim is marker-backed"
  else
    fail "scheduler profile overclaims timer-driven preemption"
  fi
fi

if ! rg -q 'enter_user_mode|switch_to_usermode' "$REPO_ROOT/kernel" "$REPO_ROOT/include" 2>/dev/null; then
  require_grep 'user_mode:[[:space:]]*"not_claimable_no_ring3_transition_test"' "harness_profile.yaml"
fi

if rg -q 'int [$]0x80.*"b"' "$REPO_ROOT/kernel/kernel.c" 2>/dev/null && ! grep -q 'ENABLE_SYSCALL_ABI_SELFTEST' "$REPO_ROOT/kernel/kernel.c"; then
  require_grep 'syscall:[[:space:]]*"not_claimable_syscall_abi_unproven"' "harness_profile.yaml"
fi

if command -v rg >/dev/null 2>&1; then
  rg -n 'nasm -f elf32[[:space:]]+boot/boot[.]asm' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "boot sector ELF stale pattern found" || pass "no boot sector ELF stale pattern"
  rg -n 'dd if=build/boot[.]o' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "boot.o dd stale pattern found" || pass "no boot.o dd stale pattern"
  rg -n 'OBJECTS[[:space:]]*=.*BOOT' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "bootloader object in kernel objects pattern found" || pass "no bootloader object pattern"
  rg -n 'KERNEL_MAX_CHS_SECTORS|kernel_sector_limit:[[:space:]]*120|phase1_bios_geometry_chs' "$REPO_ROOT/Makefile" "harness_profile.yaml" && fail "stale phase-1 CHS loader profile found" || pass "no stale phase-1 CHS loader profile"
  rg -n 'an toàn 100[%]|isolated[[:space:]]+completely' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "absolute safety claim found" || pass "no absolute safety claim"
  rg -n 'stat -c%s|grep -q "\$marker"|> "\$SERIAL_LOG" 2>&1 \|\| true|-serial mon:stdio.*>.*build/serial[.]log' . --glob '!**/06-validation/**' --glob '!**/11-reference/**' && fail "weak parser/QEMU evidence pattern found" || pass "no weak parser/QEMU evidence pattern"
  rg -n 'echo "\[PASS\].*exception.*"|EXCEPTION PANIC TEST PASSED' "$REPO_ROOT/scripts/exception_test.sh" | rg -v 'panic_present|structured|===' && fail "exception test can pass without proving a fault" || pass "exception test requires structured panic evidence"
  rg -n 'int [$]0x0D' "$REPO_ROOT/kernel/kernel.c" && fail "GPF selftest uses software interrupt instead of protection violation" || pass "GPF selftest does not use software interrupt"
  rg -n 'echo_rendered|send_line[[:space:]]+e[[:space:]]+c[[:space:]]+h[[:space:]]+o|grep .*echo ok|grep .*\\^ok|echo "\[PASS\].*echo|echo command not listed in help|echo ok command returned ok' "$REPO_ROOT/scripts/shell_test.sh" && fail "shell test claims or probes echo through flaky default shell route" || pass "shell test does not overclaim echo"
  if grep -q 'SCHED_A' "$REPO_ROOT/scripts/scheduler_test.sh" && grep -q 'SCHED_B' "$REPO_ROOT/scripts/scheduler_test.sh" && grep -q 'SCHED_CONTEXT_OK' "$REPO_ROOT/scripts/scheduler_test.sh"; then
    pass "scheduler test requires task execution context markers"
  else
    fail "scheduler test lacks task execution context markers"
  fi
  if grep -q 'context switch verified' "$REPO_ROOT/scripts/scheduler_test.sh" && { ! grep -q 'SCHED_A' "$REPO_ROOT/scripts/scheduler_test.sh" || ! grep -q 'SCHED_B' "$REPO_ROOT/scripts/scheduler_test.sh" || ! grep -q 'SCHED_CONTEXT_OK' "$REPO_ROOT/scripts/scheduler_test.sh"; }; then
    fail "scheduler test overclaims context switch without task markers"
  else
    pass "scheduler context claim is marker-backed"
  fi
  rg -n 'no paging test markers|paging semantics verified' "$REPO_ROOT/scripts/paging_test.sh" && fail "paging test can pass without paging proof" || pass "paging test requires map/unmap evidence"
  rg -n 'CONFIG_PAGING_SELFTEST' "$REPO_ROOT/Makefile" "$REPO_ROOT/kernel" "$REPO_ROOT/include" "$REPO_ROOT/scripts" && fail "stale CONFIG_PAGING_SELFTEST flag name found in implementation" || pass "no stale CONFIG_PAGING_SELFTEST implementation flag"
  rg -n 'scheduler:[[:space:]]*"claimable_with_scheduler_test"|paging:[[:space:]]*"claimable_with_paging_test"|paging:[[:space:]]*"partial_claimable_map_unmap_permission_bookkeeping_test"|memory_info:[[:space:]]*"claimable_with_memory_detection_test"|syscall:[[:space:]]*"claimable_with_syscall_test"' "harness_profile.yaml" && fail "harness profile has stale broad or downgraded core claims" || pass "harness profile uses narrowed core claims"
else
  fail "rg not installed"
fi

if [ "$failures" -eq 0 ]; then
  echo "=== HARNESS CONTRACT PASSED ==="
  exit 0
fi

echo "=== HARNESS CONTRACT FAILED: $failures issue(s) ==="
exit 1
