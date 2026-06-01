# AGENTS.md — Harness Engineering for OS Building

## Project Overview

Harness engineering documentation for building an x86 bare metal operating system in C. This project provides the complete harness framework (AGENTS.md, Skills, Sub-agents, Validation) needed to guide AI agents through end-to-end OS development on Ubuntu Server using QEMU.

## Goal

> **"OS boot lên thành công"** — bootloader emits `BOOT_OK`, kernel emits `KERNEL_INIT_OK`, then shell work can begin.

## Documentation Structure

```
00-overview/              ← Foundation: what is harness engineering, why it matters
01-project-setup/         ← Host/toolchain preflight and initial OS layout
02-core-harness/          ← Core files: AGENTS.md, SKILL.md, Sub-agents, Memory
03-os-harness-config/     ← OS-specific: boot sequence, build, QEMU, boot markers
04-skills/                ← Concrete OS workflow skill specs
05-sub-agents/            ← Agent roles and intended permission boundaries
06-validation/            ← Boot tests, marker parser, drift checks
07-memory-and-state/      ← Progress, decisions, evidence templates
08-platform-guides/       ← Claude/Codex/Gemini mapping
09-safety-and-security/   ← QEMU and host safety rules
10-implementation-checklist/ ← Phase checklist for buildout
11-reference/             ← Anti-patterns, glossary, source map
llms.txt                  ← Root discovery index for agents
.agent/skills/            ← Executable project-scoped skills
```

## Tech Stack (Target OS)

- Architecture: x86 (i386, 32-bit protected mode)
- Language: C (freestanding, no libc) + x86 Assembly (NASM syntax)
- Cross-compiler: i686-elf-gcc 13.2+
- Assembler: NASM 2.16+
- Build: GNU Make 4.3+
- Emulator: QEMU 8.2+ (qemu-system-i386)
- Host: Ubuntu Server

## Reading Order

1. `00-overview/README.md` — Project goals and scope
2. `01-project-setup/README.md` — Host/toolchain preflight
3. `00-overview/what-is-harness-engineering.md` — Core concepts
4. `02-core-harness/agents-md.md` — The most important file
5. `02-core-harness/skill-md-format.md` — On-demand capabilities
6. `03-os-harness-config/os-boot-sequence.md` — x86 boot chain
7. `03-os-harness-config/build-commands.md` — Cross-compile and build
8. `03-os-harness-config/qemu-test-loop.md` — Automated boot testing
9. `03-os-harness-config/boot-markers.md` — Serial output protocol
10. `04-skills/README.md` — Concrete skill specs
11. `06-validation/README.md` — Regression and drift gates
12. `09-safety-and-security/README.md` — Host/QEMU safety rules
13. `10-implementation-checklist/README.md` — Implementation checklist

## Key Concepts

- **Agent = Model + Harness** — Harness is the infrastructure around the model
- **AGENTS.md** — Universal guide read by all AI coding agents (< 300 lines)
- **SKILL.md** — On-demand capability, loaded only when intent matches
- **Sub-agents** — Role-separated contracts; explicit tool restrictions require platform materialization before enforcement claims
- **Boot markers** — Required: `BOOT_OK`, `KERNEL_INIT_OK`; optional: `SHELL_READY`, `TESTS_PASS`
- **QEMU safety** — Runs as unprivileged userspace process; safe when no host disks/devices are passed through
- **Artifact contract** — `boot.bin` flat boot sector, `boot_config.inc` generated sector count, `kernel.elf` link artifact, `kernel.bin` raw boot artifact, `os.img` boot image, `serial.log`/`qemu.log` test evidence
- **Evidence contract** — Automated test uses dedicated serial file, exact marker parser, QEMU status capture, and machine-written evidence

## Engineering Rubric

- Success criteria must be measurable: build artifacts exist, commands exit correctly, and required serial markers appear.
- Every autonomous run should leave evidence: prompt, files touched, tool calls, memory updates, guardrails, eval verdicts, and side effects.
- Changes pass risk gates in order: build correctness, boot markers, regression checks, and safety constraints.

## Sources

- Harness Engineering guide (research/Harness Engineering...)
- Google Antigravity 2.0 OS demo (research/Gemini Auto-Coding...)
- Gemini 3.5 Flash feasibility analysis (research/Gemini 3.5 Flash...)
- QEMU safety clarification (ideas/clarify-from-gpt55.md)
- Stack model clarification (ideas/clarify-from-gemini35flash.md)
- External engineering rubric: Google evals/sandboxing, Oracle observability/governance, Meta standardized agent tooling/regression gates/DRS-style risk gates

## Status

- [x] Phase 1: Core documentation (00-overview, 02-core-harness, 03-os-harness-config)
- [x] Phase 2: Skills (04-skills/) — Core skill specs for OS build loops
- [x] Phase 3: Sub-agents (05-sub-agents/) — Role definitions and materialization gate
- [x] Phase 4: Validation (06-validation/) — Boot verification and drift checks
- [x] Phase 5: Memory (07-memory-and-state/) — Progress/evidence templates
- [x] Phase 6: Platform guides (08-platform-guides/) — Claude/Codex/Gemini mappings
- [x] Phase 7: Safety (09-safety-and-security/) — QEMU and host safety rules
- [x] Phase 8: Checklists (10-implementation-checklist/) — Deployment checklist
- [x] Phase 9: Reference (11-reference/) — Anti-patterns, glossary, source map
