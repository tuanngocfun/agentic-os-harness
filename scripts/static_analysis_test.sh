#!/usr/bin/env bash
set -euo pipefail

CC="${CC:-/home/ngocnt/opt/cross/bin/i686-elf-gcc}"
CPPCHECK="${CPPCHECK:-cppcheck}"

fail() {
  echo "STATIC_ANALYSIS_FAIL: $*" >&2
  exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail "cross compiler not found: $CC"
command -v "$CPPCHECK" >/dev/null 2>&1 || fail "cppcheck not found"
command -v jq >/dev/null 2>&1 || fail "jq not found"

common_flags=(
  -ffreestanding
  -fno-builtin
  -fno-stack-protector
  -fno-pic
  -fno-pie
  -std=gnu11
  -Wall
  -Wextra
  -Werror
  -Wshadow
  -Wundef
  -Wformat=2
  -Wstrict-prototypes
  -Wmissing-prototypes
  -Wcast-align
  -Wwrite-strings
  -Wswitch-enum
  -Wpointer-arith
  -Wvla
  -Iinclude
  -fsyntax-only
)

selftest_defines=(
  ENABLE_ADDRESS_SPACE_SELFTEST
  ENABLE_ALLOCATOR_SELFTEST
  ENABLE_E820_SELFTEST
  ENABLE_EXEC_ARGS_SELFTEST
  ENABLE_ELF_LOADER_SELFTEST
  ENABLE_EXCEPTION_DIV0_SELFTEST
  ENABLE_EXCEPTION_GPF_SELFTEST
  ENABLE_EXCEPTION_PAGEFAULT_SELFTEST
  ENABLE_EXCEPTION_SELFTEST
  ENABLE_MEMORY_SELFTEST
  ENABLE_MARKER_SURFACE_SELFTEST
  ENABLE_PAGING_SELFTEST
  ENABLE_PROCESS_FD_SELFTEST
  ENABLE_PROCESS_LIFECYCLE_SELFTEST
  ENABLE_PROCESS_SYSCALL_SELFTEST
  ENABLE_RAMDISK_SELFTEST
  ENABLE_REDTEAM_SELFTEST
  ENABLE_SCHEDULER_SAFETY_SELFTEST
  ENABLE_SCHEDULER_SELFTEST
  ENABLE_SYSCALL_ABI_SELFTEST
  ENABLE_SYSCALL_FILE_SELFTEST
  ENABLE_SYSCALL_NEGATIVE_SELFTEST
  ENABLE_TIMER_PREEMPTION_SELFTEST
  ENABLE_TIMER_SELFTEST
  ENABLE_USERMODE_SELFTEST
  ENABLE_VFS_SELFTEST
  ENABLE_VM_SELFTEST
)

all_define_flags=()
cppcheck_defines=()
for define in "${selftest_defines[@]}"; do
  all_define_flags+=("-D$define")
  cppcheck_defines+=("-D$define")
done

"$CC" "${common_flags[@]}" kernel/*.c
echo "STATIC_SYNTAX_DEFAULT_OK"

"$CC" "${common_flags[@]}" "${all_define_flags[@]}" kernel/*.c
echo "STATIC_SYNTAX_ALL_FEATURES_OK"

"$CPPCHECK"   --quiet   --error-exitcode=1   --enable=warning,performance,portability   --std=c11   --platform=unix32   --suppress=missingIncludeSystem   -Iinclude   "${cppcheck_defines[@]}"   kernel include
echo "STATIC_CPPCHECK_OK"

while IFS= read -r script; do
  bash -n "$script"
done < <(find scripts harness-engineering -type f -name '*.sh' -print)
echo "STATIC_SHELL_SYNTAX_OK"

jq -e '
  .version == 4 and
  (.configurations | length) == 1 and
  .configurations[0].compilerPath == "/home/ngocnt/opt/cross/bin/i686-elf-gcc" and
  (.configurations[0].compilerArgs | index("-ffreestanding")) != null and
  (.configurations[0].includePath | index("${workspaceFolder}/include")) != null
' .vscode/c_cpp_properties.json >/dev/null

jq -e '
  .folders[0].path == "." and
  .settings["C_Cpp.default.compilerPath"] == "/home/ngocnt/opt/cross/bin/i686-elf-gcc" and
  (.settings["C_Cpp.default.compilerArgs"] | index("-ffreestanding")) != null and
  (.settings["C_Cpp.default.includePath"] | index("${workspaceFolder}/include")) != null
' agentic-os.code-workspace >/dev/null
echo "STATIC_EDITOR_CONFIG_OK"

for source_header in   "kernel/string.c:include/string.h"   "kernel/elf.c:include/elf.h"   "kernel/syscall.c:include/syscall.h"; do
  source="${source_header%%:*}"
  expected_header="${source_header#*:}"
  trace="$("$CC" -ffreestanding -Iinclude -H -E "$source" -o /dev/null 2>&1)"
  grep -Fq "$expected_header" <<<"$trace" ||
    fail "$source did not resolve $expected_header"
  grep -Fq "stdint-gcc.h" <<<"$trace" ||
    fail "$source did not resolve the cross compiler stdint implementation"
done
echo "STATIC_HEADER_PROVENANCE_OK"

echo "STATIC_ANALYSIS_OK"
