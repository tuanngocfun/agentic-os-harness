#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
FINDINGS_DIR="$BUILD_DIR/red-team"
FINDINGS_LOG="$FINDINGS_DIR/tooling-findings.jsonl"
HOST_CC="${HOST_CC:-gcc}"

fail() {
  echo "RED_TOOLING_FAIL: $*" >&2
  exit 1
}

command -v "$HOST_CC" >/dev/null 2>&1 || fail "host compiler not found: $HOST_CC"
mkdir -p "$FINDINGS_DIR"

host_log="$FINDINGS_DIR/host-header-shadow.log"
if "$HOST_CC" -ffreestanding -fsyntax-only kernel/string.c >"$host_log" 2>&1; then
  fail "unsupported host-header compile unexpectedly succeeded"
fi
grep -Eq 'uint8_t|unknown type name' "$host_log" ||
  fail "host-header failure did not reproduce the reported type-resolution symptom"
echo "RED_TOOLING_HEADER_SHADOW_CONFIRMED"

bash scripts/static_analysis_test.sh >/dev/null
echo "RED_TOOLING_HEADER_SHADOW_BLOCKED"

weak_revision=""
while IFS= read -r revision; do
  if git show "$revision:kernel/kernel.c" 2>/dev/null |
      grep -Fq 'int map_ok = cr3_ok'; then
    weak_revision="$revision"
    break
  fi
done < <(git rev-list HEAD -- kernel/kernel.c)

[ -n "$weak_revision" ] ||
  fail "could not reproduce the historical address-space oracle weakness"
echo "RED_ADDRSPACE_ORACLE_WEAKNESS_CONFIRMED"

if grep -Fq 'int map_ok = cr3_ok' kernel/kernel.c; then
  fail "current address-space selftest still trusts allocation alone"
fi
grep -Fq 'paging_switch_directory(cr3_a)' kernel/kernel.c ||
  fail "current address-space selftest does not enter the first test address space"
grep -Fq 'mapped_a = paging_get_physical_address(ADDRSPACE_TEST_VA)' kernel/kernel.c ||
  fail "current address-space selftest does not resolve the first physical mapping"
grep -Fq 'paging_switch_directory(cr3_b)' kernel/kernel.c ||
  fail "current address-space selftest does not enter the second test address space"
grep -Fq 'mapped_b = paging_get_physical_address(ADDRSPACE_TEST_VA)' kernel/kernel.c ||
  fail "current address-space selftest does not resolve the second physical mapping"
grep -Fq 'map_ok = mapped_a == frame_a && mapped_b == frame_b' kernel/kernel.c ||
  fail "current address-space selftest does not compare resolved physical frames"
grep -Fq 'paging_destroy_address_space(cr3_a)' kernel/kernel.c ||
  fail "current address-space selftest lacks address-space cleanup"
echo "RED_ADDRSPACE_ORACLE_HARDENED"

printf '%s\n'   '{"id":"RT-TOOLING-001","subsystem":"editor-header-resolution","severity":"medium","status":"blocked_by_blue_team","evidence_marker":"RED_TOOLING_HEADER_SHADOW_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-tooling-001"}'   '{"id":"RT-HARNESS-002","subsystem":"address-space-test-oracle","severity":"high","status":"blocked_by_blue_team","evidence_marker":"RED_ADDRSPACE_ORACLE_HARDENED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-harness-002"}'   > "$FINDINGS_LOG"

[ -s "$FINDINGS_LOG" ] || fail "tooling findings were not written"
echo "RED_TOOLING_FINDINGS_OK"
