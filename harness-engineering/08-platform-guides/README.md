# 08 — Platform Guides

## Mục tiêu

Keep the harness portable across Claude, Codex, and Gemini/Antigravity without inventing unsupported behavior. `AGENTS.md` remains the shared contract; platform-specific files should only add routing or tool restrictions.

## Shared Contract

All platforms should read:
- `AGENTS.md`
- `llms.txt`
- Relevant `.agent/skills/<name>/SKILL.md` executable skills
- Relevant `04-skills/` human index docs
- `06-validation/README.md`

All platforms must preserve:
- Artifact contract: `boot.bin`, `boot_config.inc`, `kernel.elf`, `kernel.bin`, `os.img`
- Marker contract: required `BOOT_OK`, `KERNEL_INIT_OK`
- Safety contract: no root QEMU, no host disk/device passthrough
- Evidence contract: automated tests use dedicated serial file, exact marker parsing, and machine-written evidence

## Claude Code Mapping

Recommended:
- `CLAUDE.md` imports or summarizes `AGENTS.md`.
- `.claude/agents/` contains read-only reviewer/tester/debugger role definitions.
- `.claude/skills/` mirrors high-value skills from `04-skills/`.

Do not duplicate long OS docs in `CLAUDE.md`; link to `agent_docs/` or this harness.

## Codex Mapping

Recommended:
- `AGENTS.md` is the main project guide.
- `.codex/agents/*.toml` can encode role-specific developer instructions.
- Skills should stay small and trigger-specific.

Codex agents should follow the same review posture: inspect first, edit narrowly, run verification when feasible.

## Gemini / Antigravity Mapping

Recommended:
- `GEMINI.md` can contain Gemini-specific overrides, but should not conflict with `AGENTS.md`.
- `.agent/skills/<name>/SKILL.md` can mirror the skill specs from `04-skills/`.
- `.agent/agents/` can define orchestrator/tester/debugger roles.

Do not claim a platform can build a production-grade OS from this harness. The target is a bootable teaching OS in QEMU.

## Cross-Platform Validation

For each platform:
1. Ask the agent to summarize artifact and marker contracts.
2. Ask it how to run boot validation.
3. Check it does not propose `boot.o` sector writes or VGA-only marker detection.
4. Check it preserves QEMU host safety rules.
5. Check it does not claim role restrictions are enforced unless platform role files/wrappers exist.
