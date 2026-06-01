# 02 — Core Harness Files

## Tổng quan

Đây là các file harness **cốt lõi** — nền tảng để AI agent hiểu và làm việc với OS project.

## Cấu trúc file trong Repository

```
project-root/
├── AGENTS.md              ← Universal guide (đọc bởi Claude, Codex, Gemini, Cursor...)
├── CLAUDE.md              ← Claude-specific overrides (optional)
├── GEMINI.md              ← Gemini-specific overrides (optional)
│
├── .agent/
│   ├── skills/            ← Project-scoped skills (Antigravity format)
│   │   └── <skill-name>/
│   │       ├── SKILL.md
│   │       ├── scripts/
│   │       └── references/
│   └── agents/            ← Sub-agent definitions
│
├── .claude/
│   ├── skills/            ← Claude Code skills format
│   └── rules/             ← Always-on guardrails
│
├── .codex/
│   └── agents/            ← Codex sub-agent definitions (.toml)
│
├── agent_docs/            ← Progressive disclosure docs
│   ├── architecture.md
│   ├── running_tests.md
│   └── known_issues.md
│
└── llms.txt               ← Root index tài liệu cho agent discovery
```

Trong harness documentation này, các section mở rộng nằm ở:

```
01-project-setup/         ← host/toolchain preflight
04-skills/                ← concrete OS workflow skills
05-sub-agents/            ← role contracts and permissions
06-validation/            ← boot-test protocol, drift checks
07-memory-and-state/      ← templates for progress/evidence
08-platform-guides/       ← Claude/Codex/Gemini mapping
09-safety-and-security/   ← QEMU host safety
10-implementation-checklist/
11-reference/
12-git-change-management/ ← Git workflow and handoff gates
.agent/skills/            ← executable project-scoped skills
```

## Quy tắc phân tầng

| File | Khi nào load | Kích thước | Mục đích |
|---|---|---|---|
| `AGENTS.md` | **Luôn luôn** | < 300 dòng | Tech stack, build commands, conventions |
| `SKILL.md` | **On-demand** (khi intent match) | Không giới hạn | Workflow chi tiết cho task cụ thể |
| `agent_docs/` | **Progressive** (khi cần context sâu) | Không giới hạn | Architecture, schema, known issues |
| `12-git-change-management/` | Khi repo state matters | Ngắn, policy-focused | Branch/status/diff/staging/commit/push gates |

## Các file trong section này

| File | Nội dung |
|---|---|
| `agents-md.md` | Template & rules cho AGENTS.md |
| `skill-md-format.md` | Format SKILL.md, 5 levels, trigger mechanism |
| `sub-agents.md` | Multi-agent orchestration, role definitions |
| `memory-files.md` | Persistent state: architecture.md, progress.md |
| `llms-txt.md` | Index file cho agent discovery |

Executable skills now include:
- `.agent/skills/compile-and-run/`
- `.agent/skills/regression-validation/`
- `.agent/skills/git-change-management/`

## Nguyên tắc quan trọng nhất

> **AGENTS.md < 300 dòng.** Nếu dài hơn, move content xuống `agent_docs/`.
>
> AGENTS.md phải ưu tiên commands và constraints thật. Claim không kiểm chứng hoặc số liệu marketing nên đi vào reference/citation audit, không đi vào core contract.
>
> Khi project nằm trong Git repo thật, Git status/diff là một sensor bắt buộc trước handoff. Staging/commit/push là write actions và cần explicit user request.
