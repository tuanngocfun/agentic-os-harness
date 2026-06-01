# 00 — Overview: Harness Engineering cho OS Building

## Mục tiêu dự án

Xây dựng một hệ điều hành **x86 bare metal** viết bằng **C**, chạy trên **QEMU**, với mục tiêu đầu tiên là:

> **"OS boot lên thành công"** — bootloader emit `BOOT_OK`, kernel emit `KERNEL_INIT_OK`, rồi mới phát triển tiếp shell.

## Harness Engineering là gì?

**Agent = Model + Harness**

- **Model** = CPU — sức mạnh xử lý thô của model đang dùng
- **Harness** = Hệ điều hành — quản lý boot sequence, context, tool routing, guardrails
- **Agent** = Application — logic cụ thể chạy trên OS

Harness **không phải** là agent. Nó là hạ tầng bao quanh model: governs cách agent hoạt động, đảm bảo reliability, efficiency, và steerable hành vi qua hàng trăm tool calls.

## Tại sao cần Harness?

| Lớp | Phạm vi | Vấn đề giải quyết |
|---|---|---|
| **Prompt Engineering** | Một lượt tương tác | Hướng dẫn model trong 1 turn |
| **Context Engineering** | Cả session | Quản lý token, tránh context rot |
| **Harness Engineering** | Toàn bộ deployment | Govern agent ACROSS mọi turn, mọi task |

Nhiều AI agent projects fail không phải vì model yếu, mà vì thiếu harness: không có goal đo được, không có guardrails, không có eval, không có memory, và không có rollback/regression discipline.

## Scope: 3 mức OS

| Mức | Ý nghĩa thực tế | Dự án này nhắm tới |
|---|---|---|
| **Toy / Teaching OS** | Kernel tối thiểu: boot, scheduler, memory, syscall, FS, chạy trong QEMU | **Đây** |
| **Assembled OS** | Lắp ghép kernel + bootloader + userland + build system (kiểu Buildroot) | Tương lai |
| **Production-grade OS** | Driver coverage, ABI ổn định, security, CI/CD, compliance | Chưa khả thi |

## Stack kỹ thuật

```
Ubuntu Server (host, chạy liên tục)
└── AI Agent + Harness (Gemini/Claude/Codex + AGENTS.md + Skills)
    └── Cross-compiler toolchain (i686-elf-gcc, nasm, ld)
        └── QEMU (qemu-system-i386, unprivileged userspace process)
            └── OS tự build (chạy biệt lập bên trong QEMU)
```

QEMU tương đối an toàn khi chạy bằng user thường với image file riêng và không passthrough host disk/device thật. Không claim tuyệt đối; safety phụ thuộc vào cách chạy.

## Engineering Rubric

| Rule | Áp dụng vào harness này |
|---|---|
| Measurable success | `build/boot.bin`, `build/boot_config.inc`, `build/kernel.elf`, `build/kernel.bin`, `build/os.img` tồn tại; `make test` pass với exact serial marker lines |
| Agent episode evidence | Mỗi vòng agent ghi lại prompt/task, files touched, Git status/diff, commands, QEMU status, artifact hashes, marker verdicts, blockers |
| Risk gates | Git status/diff -> build correctness -> serial markers -> regression checks -> safety constraints |
| Source authority | QEMU/NASM/binutils docs cho tool behavior; OSDev cho boot-sector convention |

## Cấu trúc tài liệu

```
00-overview/           ← Bạn đang ở đây
01-project-setup/      ← Host/toolchain preflight
02-core-harness/       ← AGENTS.md, SKILL.md, Sub-Agents, Memory
03-os-harness-config/  ← OS-specific: boot sequence, build, QEMU, boot markers
12-git-change-management/ ← Git workflow and handoff gates
```

## Tài liệu tham khảo chính

1. [What Is Harness Engineering AI? - Atlan](https://atlan.com/know/what-is-harness-engineering/)
2. [The importance of Agent Harness in 2026 - Philschmid](https://www.philschmid.de/agent-harness-2026)
3. [Writing a Good AGENTS.md - Philschmid](https://www.philschmid.de/writing-good-agents)
4. [Google Antigravity Built an OS](https://antigravity.google/blog/google-antigravity-built-an-os)
5. [Quality-Assured Fuzz Harness Generation - arXiv](https://arxiv.org/html/2605.21824v1)
6. [QEMU Documentation](https://www.qemu.org/docs/master/system/)
7. [NASM Documentation](https://www.nasm.us/doc/)
8. [GNU Binutils objcopy](https://sourceware.org/binutils/docs/binutils/objcopy.html)
9. [Google Vertex AI Agent Evaluation](https://cloud.google.com/blog/products/ai-machine-learning/introducing-agent-evaluation-in-vertex-ai-gen-ai-evaluation-service)
10. [Oracle OCI Observability for Agentic AI](https://blogs.oracle.com/ai-and-datascience/oci-observability-for-agentic-ai)
11. [Meta Unified AI Agents](https://engineering.fb.com/2026/04/16/developer-tools/capacity-efficiency-at-meta-how-unified-ai-agents-optimize-performance-at-hyperscale/)
12. [Meta Diff Risk Score](https://engineering.fb.com/2025/08/06/developer-tools/diff-risk-score-drs-ai-risk-aware-software-development-meta/)
13. [Birgitta Böckeler - Harness Engineering for Coding Agent Users](https://martinfowler.com/articles/exploring-gen-ai/harness-engineering.html)
14. [Google Engineering Practices - Small CLs](https://google.github.io/eng-practices/review/developer/small-cls.html)
15. [Microsoft Azure Repos Branch Policies](https://learn.microsoft.com/en-us/azure/devops/repos/git/branch-policies?view=azure-devops)
16. [Swift.org Contributing](https://www.swift.org/contributing/)
17. [Netflix TechBlog - Improving Pull Request Confidence](https://netflixtechblog.medium.com/improving-pull-request-confidence-for-the-netflix-tv-app-b85edb05eb65)
18. [Tesla Fleet Telemetry Contributing](https://github.com/teslamotors/fleet-telemetry/blob/main/CONTRIBUTING.md)
