# llms.txt — Agent Discovery Index

## Mục đích

`llms.txt` là file index giúp AI agent **khám phá** tài liệu trong project. Agent đọc file này để biết có những tài liệu nào, ở đâu, và khi nào cần đọc.

Trong harness này, root `llms.txt` đã được materialize tại `harness-engineering/llms.txt`; file hiện tại giải thích format và freshness rules.

## Format

```markdown
# Project Documentation Index

## Core Documents
- [AGENTS.md](./AGENTS.md) — Project overview, tech stack, build commands, conventions
- [Project Setup](./01-project-setup/README.md) — Host/toolchain preflight and initial layout
- [Architecture](./agent_docs/architecture.md) — System design, memory map, boot sequence
- [Running Tests](./agent_docs/running_tests.md) — How to run tests, expected output
- [Known Issues](./agent_docs/known_issues.md) — Known bugs and workarounds
- [Progress](./agent_docs/progress.md) — Current task state
- [OS Build Commands](./03-os-harness-config/build-commands.md) — Artifact contract and Makefile flow
- [Boot Markers](./03-os-harness-config/boot-markers.md) — COM1 serial marker protocol
- [Validation](./06-validation/README.md) — Boot-test protocol, drift checks, regression gates
- [Safety](./09-safety-and-security/README.md) — QEMU host safety rules

## Skills
- [Skill Specs](./04-skills/README.md) — write-bootloader, setup-gdt, kernel-entry, serial-driver, compile-and-run, debug-boot-failure
- [Executable compile-and-run](./.agent/skills/compile-and-run/SKILL.md) — Build and QEMU marker test with machine evidence
- [Executable regression-validation](./.agent/skills/regression-validation/SKILL.md) — Contract drift and materialization checks
- [write-bootloader](./04-skills/README.md#1-write-bootloader) — Write flat x86 boot sector in NASM
- [setup-gdt](./04-skills/README.md#2-setup-gdt) — Configure Global Descriptor Table
- [compile-and-run](./04-skills/README.md#5-compile-and-run) — Build and test on QEMU
- [debug-boot-failure](./04-skills/README.md#6-debug-boot-failure) — Diagnose boot failures

## Sub-Agents
- [Sub-Agent Specs](./05-sub-agents/README.md) — Orchestrator, writer, reviewer, tester, debugger, safety reviewer, doc maintainer
- [test-runner](./05-sub-agents/README.md#test-runner) — QEMU boot test execution
- [debugger](./05-sub-agents/README.md#debugger) — Serial log analysis

## External References
- [QEMU Documentation](https://www.qemu.org/docs/master/)
- [OSDev Wiki](https://wiki.osdev.org)
- [Intel x86 Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [NASM Documentation](https://www.nasm.us/doc/)
- [GNU Binutils objcopy](https://sourceware.org/binutils/docs/binutils/objcopy.html)
```

## Quy tắc

1. **Mỗi entry** có: tên, link relative, mô tả ngắn (1 câu)
2. **Group by category**: Core, Skills, Agents, External
3. **Update** khi thêm/xóa file
4. **Agent đọc file này đầu tiên** khi bắt đầu session mới
5. **Freshness check** phải verify root `llms.txt` references `AGENTS.md`, validation, safety, and executable skills

## Vị trí

```
project-root/
├── llms.txt          ← Ở root, dễ tìm
├── AGENTS.md
├── agent_docs/
└── .agent/
```

## Agent Discovery Flow

```
1. Agent start session
2. Đọc llms.txt
3. Thấy user hỏi về boot → load skill "write-bootloader"
4. Thấy boot test fail → load agent "debugger"
5. Cần context về architecture → đọc agent_docs/architecture.md
```
