# 11 — Reference

## Anti-Patterns

| Anti-pattern | Why it fails | Correct pattern |
|---|---|---|
| Assemble boot sector as an ELF32 object, then write it to disk sector 0 | BIOS sees ELF header, not boot code | `nasm -f bin boot/boot.asm -o build/boot.bin` |
| Link bootloader into kernel | Boot sector and kernel are separate artifacts | Link only kernel objects into `kernel.elf` |
| Write `kernel.elf` directly to sector 1 | Simple bootloader loads raw bytes, not ELF | `objcopy -O binary kernel.elf kernel.bin` |
| Print `BOOT_OK` with VGA/BIOS only | Headless test reads COM1 serial | Emit marker through COM1 |
| Treat `SHELL_READY` as required before shell exists | Fails early-stage kernels | Keep shell marker optional until implemented |
| Claim QEMU is "100% safe" | Safety depends on config | State concrete safe command and forbidden flags |
| Assume BIOS initialized `DS`/`ES`/`SS` | Memory/string/disk access becomes unstable | Initialize real-mode segments and stack first |
| Hardcode `KERNEL_SECTORS` forever | Kernel grows and only partially loads | Generate `boot_config.inc` from `kernel.bin` size |
| Use one CHS BIOS read for kernels larger than 17 sectors | Read crosses first floppy track boundary | Add track-rolling CHS, LBA, or 2-stage boot |
| Rely on object list order for `_start` at 0x1000 | Reordering can jump into wrong code | Put `_start` in `.entry` and `KEEP(*(.entry))` first in linker script |
| Parse markers with substring `grep` | `BOOT_OK_FAKE` can pass | Normalize CRLF and use exact whole-line matching |
| Use `-serial mon:stdio` as automated evidence | Monitor and COM1 share one stream | Use `-serial file:build/serial.log -monitor none` for tests |
| Let Makefile variables redirect `dd` or `clean` | Overrides can target unsafe paths | Guard `BUILD_DIR` and `OS_IMG` before write/delete commands |
| Force too much logic into sector 0 | Boot sector exceeds 512 bytes | Split into a 2-stage bootloader |

## Glossary

| Term | Meaning |
|---|---|
| Harness | Control layer around model/agent: guides, tools, memory, evals, guardrails |
| Guide | Feedforward context before action: AGENTS.md, skill docs, constraints |
| Sensor | Feedback after action: tests, parsers, evals, drift checks |
| Agent episode | One autonomous run with prompt, actions, evidence, and side effects |
| Boot marker | Exact serial string used to validate boot progress |
| Flat binary | Raw executable bytes without ELF headers |
| ELF | Link/debug artifact with headers and sections |
| COM1 | Serial port at `0x3F8`; capture to dedicated file for automated tests, or `mon:stdio` for human debug |

## Source Map

Primary tool/OS sources:
- [QEMU docs](https://www.qemu.org/docs/master/system/): serial/display behavior and QEMU runtime flags.
- [NASM docs](https://www.nasm.us/doc/): `bin` vs `elf32` output formats.
- [GNU Binutils objcopy](https://sourceware.org/binutils/docs/binutils/objcopy.html): `objcopy -O binary`.
- [OSDev wiki](https://wiki.osdev.org): BIOS boot sequence, boot sector signature, freestanding compiler guidance.
- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html): protected mode, segmentation, descriptor semantics.

Engineering sources:
- [Google Vertex AI agent evaluation](https://cloud.google.com/blog/products/ai-machine-learning/introducing-agent-evaluation-in-vertex-ai-gen-ai-evaluation-service): trajectory-level evals.
- [Google Agent Sandbox](https://cloud.google.com/blog/products/containers-kubernetes/agentic-ai-on-kubernetes-and-gke/): sandbox guardrails for tool/code execution.
- [Google Antigravity OS demo](https://antigravity.google/blog/google-antigravity-built-an-os): functional OS demo caveat, not production OS proof.
- [Oracle observability for agentic AI](https://blogs.oracle.com/ai-and-datascience/oci-observability-for-agentic-ai): agent episodes as traceable/evaluable evidence.
- [Oracle runtime governance](https://blogs.oracle.com/ai-and-datascience/runtime-governance-enterprise-agentic-ai): action authorization, eval gates, and observability.
- [Meta unified AI agents](https://engineering.fb.com/2026/04/16/developer-tools/capacity-efficiency-at-meta-how-unified-ai-agents-optimize-performance-at-hyperscale/): standardized tools and encoded domain expertise.
- [Meta AI tribal knowledge mapping](https://engineering.fb.com/2026/04/06/developer-tools/how-meta-used-ai-to-map-tribal-knowledge-in-large-scale-data-pipelines/): precomputed navigation/context guides.
- [Meta LLM-powered bug catchers](https://engineering.fb.com/2025/02/05/security/revolutionizing-software-testing-llm-powered-bug-catchers-meta-ach/): fault-targeted regression tests.
- [Meta Diff Risk Score](https://engineering.fb.com/2025/08/06/developer-tools/diff-risk-score-drs-ai-risk-aware-software-development-meta/): risk-aware change gates and mitigation workflows.
- [Birgitta Böckeler on martinfowler.com](https://martinfowler.com/articles/exploring-gen-ai/harness-engineering.html): guides/sensors framing for coding-agent harnesses.

## Public Contract Summary

Artifacts:
- `build/boot.bin`
- `build/boot_config.inc`
- `build/kernel.elf`
- `build/kernel.bin`
- `build/os.img`
- `build/serial.log`
- `build/qemu.log`

Commands:
- `make all`
- `make run`
- `make run-serial`
- `make test`
- `make clean`

Markers:
- Required: `BOOT_OK`, `KERNEL_INIT_OK`
- Optional: `SHELL_READY`, `TESTS_PASS`
- Failure: `BOOT_DISK_ERROR`, `KERNEL_PANIC`
