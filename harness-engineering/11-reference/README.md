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
| Use one CHS BIOS read for kernels larger than 17 sectors | Read crosses first track boundary and may assume the wrong disk geometry | Query BIOS geometry, roll CHS sectors safely, or switch to LBA/2-stage boot |
| Rely on object list order for `_start` at 0x1000 | Reordering can jump into wrong code | Put `_start` in `.entry` and `KEEP(*(.entry))` first in linker script |
| Parse markers with substring `grep` | `BOOT_OK_FAKE` can pass | Normalize CRLF and use exact whole-line matching |
| Use `-serial mon:stdio` as automated evidence | Monitor and COM1 share one stream | Use `-serial file:build/serial.log -monitor none` for tests |
| Let Makefile variables redirect `dd` or `clean` | Overrides can target unsafe paths | Guard `BUILD_DIR` and `OS_IMG` before write/delete commands |
| Treat `KERNEL_DEFINES` changes as if Make tracks them automatically | A selftest build can leave stale objects in the next normal image | Store build config in a stamp/config file and make C objects depend on it |
| Force too much logic into sector 0 | Boot sector exceeds 512 bytes | Split into a 2-stage bootloader |
| Reintroduce a sector-0-only kernel loader | Kernel growth is constrained by sector-0 code size and legacy CHS limits | Keep sector 0 minimal and load `stage2.bin`, then let stage 2 load the kernel |
| Claim a feature works because files exist or boot markers pass | Scaffolding can be linked but never executed | Add runtime gates and artifacts for each behavior |
| Run deep subsystem probes in the default boot path | Boot/shell debugging chases the wrong layer | Gate probes behind explicit selftest defines and `make test-deep` |
| Treat structured panic markers as non-failures | `KERNEL_PANIC:...` can be missed by exact matching | Match failure marker prefixes, not only exact lines |
| Let a fault test pass without triggering a fault | False confidence | Require exact structured panic evidence |
| Claim scheduler context switching from saved register state only | A saved `esp` can exist even when no task body executed | Require queue-rotation markers plus task-execution markers from at least two runnable contexts, and name the claim accordingly |
| Let a paging test pass without paging markers | Missing selftest output becomes a false pass | Require exact `PAGING_MAP_OK`, `PAGING_UNMAP_OK`, `PAGING_PERM_OK`, `PAGING_WRITE_FAULT_OK`, `PAGING_UNMAP_FAULT_OK`, and `PAGING_OK` markers for the current gate |
| Prove `echo ok` by grepping typed VGA input | The command line can appear even when command output did not | Use a separate shell I/O gate that requires both a serial marker and a distinct output line after the typed command |
| Use HTML as the primary harness instruction format | Agents need concise editable contracts, not rendered reports | Use Markdown for instructions, YAML for profile/config, JSONL for evidence, and HTML only for reports |
| Broad Git staging | Can include unrelated/user changes | Stage explicit paths only after status and diff review |
| Stage generated build artifacts | Makes repo stale and hard to review | Keep `build/` and binary outputs ignored/untracked |
| Mutate Git history or remote state casually | Can destroy collaboration state | Require explicit user request and current status/diff evidence |
| Claim CI/branch protection without checking | False confidence | Inspect platform settings or mark them unknown |

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
| Diff stat | Concise summary of changed files and line counts |
| Staged paths | Files currently prepared for commit |
| Short-lived branch | Temporary branch used to isolate one reviewable change |

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

Git/change-management sources:
- [Google Engineering Practices: Small CLs](https://google.github.io/eng-practices/review/developer/small-cls.html): small, self-contained changes with related tests and build health.
- [Google Engineering Practices: Code Review Standard](https://google.github.io/eng-practices/review/reviewer/standard.html): improve code health using data, style consistency, and continuous improvement rather than perfectionism.
- [Anthropic: Building Effective Agents](https://www.anthropic.com/engineering/building-effective-agents): simple tested tool interfaces, evaluator/optimizer loops, and clear agent-computer interfaces.
- [Anthropic: Demystifying evals for AI agents](https://www.anthropic.com/engineering/demystifying-evals-for-ai-agents): multi-turn agents need trajectory-level evals over tool calls, state changes, and environment updates.
- [OpenAI Agent evals](https://platform.openai.com/docs/guides/agent-evals): workflow-level agent errors need trace grading and evals, not only final-output checks.
- [OpenAI Agents SDK](https://platform.openai.com/docs/guides/agents-sdk/): agents need traces, tool handoffs, and guardrail-aware workflows.
- [Microsoft Azure Repos branch policies](https://learn.microsoft.com/en-us/azure/devops/repos/git/branch-policies?view=azure-devops): protected branches, PR requirements, reviewers, build validation, and status checks.
- [Microsoft Security Development Lifecycle](https://www.microsoft.com/en-us/securityengineering/sdl): secure design, threat modeling, and security validation practices apply across software including operating systems and firmware.
- [Microsoft Azure Pipelines PR triggers](https://learn.microsoft.com/en-us/azure/devops/pipelines/repos/azure-repos-git?view=azure-devops): PR validation through branch policy and build validation.
- [Swift.org contributing](https://www.swift.org/contributing/): incremental development, PR/release branch approvals, authorship and code-owner review boundaries.
- [Netflix TechBlog: Improving Pull Request Confidence](https://netflixtechblog.medium.com/improving-pull-request-confidence-for-the-netflix-tv-app-b85edb05eb65): PR confidence through test evidence, stability pipelines, and explicit merge safety judgement.
- [Tesla Fleet Telemetry contributing](https://github.com/teslamotors/fleet-telemetry/blob/main/CONTRIBUTING.md): public open-source issue/branch/PR review workflow.
- [Tesla Fleet Telemetry PR template](https://github.com/teslamotors/fleet-telemetry/blob/main/PULL_REQUEST_TEMPLATE.md): summary, change type, self-review, docs, unit test, and integration test checklist.

## Public Contract Summary

Artifacts:
- `build/boot.bin`
- `build/stage2.bin`
- `build/boot_config.inc`
- `build/kernel.elf`
- `build/kernel.bin`
- `build/os.img`
- `build/serial.log`
- `build/qemu.log`
- `build/serial.shell.log`
- `build/vga.shell.txt`

Commands:
- `make all`
- `make run`
- `make run-serial`
- `make test`
- `make test-deep`
- `make test-boot`
- `make test-shell`
- `make test-syscall`
- `make test-exception`
- `make test-scheduler`
- `make test-paging`
- `make test-memory`
- `make test-usermode`
- `make test-timer`
- `make test-timer-preemption`
- `make test-allocator`
- `make test-address-space`
- `make test-syscall-negative`
- `make test-syscall-file`
- `make test-elf-loader`
- `make test-process-syscall`
- `make test-process-lifecycle`
- `make test-process-fd`
- `make test-e820-frame`
- `make test-ramdisk`
- `make test-vfs`
- `make test-scheduler-safety`
- `make test-red-team`
- `make test-blue-team`
- `make test-shell-io`
- `make clean`
- `.agent/skills/git-change-management/scripts/git_preflight.sh`

Markers:
- Required in the current shell phase: `STAGE2_OK`, `BOOT_OK`, `KERNEL_INIT_OK`, `SHELL_READY`
- Optional: `TESTS_PASS`
- Failure: `BOOT_DISK_ERROR`, `KERNEL_PANIC`

Runtime evidence:
- Bootloader: `scripts/boot_test.sh` proves sector 0 loaded stage 2 through `STAGE2_OK`, then protected-mode loader progress through `BOOT_OK`.
- Shell: `scripts/shell_test.sh` must prove keyboard input, command dispatch, and VGA output for `help`. `scripts/shell_io_test.sh` separately proves `echo ok` with `SHELL_ECHO_OK` and a distinct VGA output line.
- Syscall: `scripts/syscall_test.sh` proves the current `int 0x80` ABI contract. `scripts/syscall_negative_test.sh` proves ring-3 valid syscall execution plus controlled `ENOSYS`/`EFAULT` negative paths for invalid syscall numbers, kernel pointers, and unmapped user pointers.
- Exception panic: `scripts/exception_test.sh` proves divide-by-zero, invalid-opcode, GPF, and page-fault structured panic paths via `KERNEL_PANIC:<vector>:<code>` markers.
- Scheduler: `scripts/scheduler_test.sh` proves ready-queue rotation and explicit cooperative context execution through `SCHED_A`, `SCHED_B`, and `SCHED_CONTEXT_OK`. `scripts/timer_preemption_test.sh` proves IRQ0-driven preemption by running two non-yielding tasks and requiring `PREEMPT_A`, `PREEMPT_B`, and `PREEMPT_OK`. `scripts/scheduler_safety_test.sh` proves the current priority/fairness/critical-section safety markers.
- Timer: `scripts/timer_test.sh` proves only that PIT-backed ticks increment during the targeted selftest.
- Paging: `scripts/paging_test.sh` proves map/unmap, permission-bit bookkeeping, CR0.WP-backed supervisor write fault, and unmap+invlpg fault evidence. `scripts/usermode_test.sh` adds user/supervisor page-fault proof from ring 3. `scripts/address_space_test.sh` proves separate CR3 values and isolated physical frames behind the same virtual address.
- Memory: `scripts/memory_test.sh` proves usable-memory detection for the configured 512 MiB run. `scripts/e820_test.sh` proves E820 map handoff plus physical frame allocation/free/reuse/exhaustion markers. `scripts/allocator_test.sh` proves the fixed heap allocator's allocation, reuse, free/coalescing accounting, and exhaustion handling.
- Process/user mode: `scripts/usermode_test.sh` proves process-record setup for a ring-3 entry and controlled user/supervisor page fault. `scripts/address_space_test.sh` proves per-process address-space switching and isolation. `scripts/process_lifecycle_test.sh` proves fork, blocking wait, exit/reap, and exec replacement. `scripts/process_fd_test.sh` proves process-local descriptors, real-fork inheritance with shared offsets, retained-parent behavior after child close, selective CLOEXEC, exit cleanup, and EBADF paths. These gates do not prove COW, argv/envp, signals, or SMP safety.
