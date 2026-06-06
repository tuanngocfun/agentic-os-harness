# E820 Memory Map & Frame Allocator Status

## Status

Implemented and runtime-tested on 2026-06-06.

This file is now a status note, not a future plan. The implemented gate is:

```bash
make test-e820-frame
```

The full deep suite also runs it through `make test-deep`.

## Implemented Design

- `boot/boot.asm` calls BIOS `INT 0x15, EAX=0xE820` before switching to protected mode.
- The E820 handoff buffer lives at physical `0x00080000`, not `0x8000`, so it does not collide with the real-mode boot sector area.
- The handoff header is compact to keep sector 0 at exactly 512 bytes:
  - `uint32_t magic`
  - `uint16_t count`
  - `uint16_t reserved`
- E820 entries are 24 bytes and begin immediately after the header.
- `kernel/e820.c` validates the magic, copies bounded entries, reports the map, and exposes usable-region queries.
- `kernel/memory.c` prefers E820 usable memory when present and falls back to CMOS memory detection if E820 is unavailable.
- `kernel/frame.c` implements a bitmap physical frame allocator:
  - 4 KiB frames
  - allocation
  - free accounting
  - reuse
  - low-frame exhaustion proof for identity-mapped page-table allocations
- `kernel/paging.c` now obtains page-table frames from the frame allocator.

## Runtime Evidence

`scripts/e820_test.sh` requires:

- `E820_TEST`
- `E820_DETECT_OK`
- `E820_USABLE_MEMORY_OK`
- `FRAME_ALLOC_OK`
- `FRAME_FREE_OK`
- `FRAME_REUSE_OK`
- `FRAME_EXHAUST_OK`
- `E820_FRAME_OK`

The current QEMU 512 MiB run reports E820 usable memory below the raw physical size because firmware-reserved regions are excluded. `scripts/memory_test.sh` therefore validates a numeric usable-memory range rather than the old exact CMOS total.

## Remaining Limits

This is a credible teaching-kernel frame allocator, not a production memory manager.

Still not claimed:

- dynamic heap growth from arbitrary frame runs;
- memory zones, DMA policy, NUMA, or high-memory policy;
- large-page support;
- reclamation of ACPI reclaimable regions;
- stress/fuzz coverage beyond the targeted lifecycle gate.
