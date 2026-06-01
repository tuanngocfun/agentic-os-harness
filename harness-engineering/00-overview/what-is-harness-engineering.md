# What is Harness Engineering?

## Định nghĩa nền tảng

**Agent = Model + Harness**

Đây là shorthand thực dụng: model cung cấp reasoning/coding capability, còn harness cung cấp context, tools, memory, guardrails, evals, và evidence loop. Treat it as a mental model, not a formal standard.

## Phân tầng kiến trúc

```
┌─────────────────────────────────────┐
│          Agent = Application        │  ← Logic cụ thể: build OS, review code
├─────────────────────────────────────┤
│          Harness = OS               │  ← Quản lý boot, context, routing, guardrails
├─────────────────────────────────────┤
│     Context Window = RAM            │  ← Bộ nhớ làm việc, dễ đầy, dễ mất
├─────────────────────────────────────┤
│          Model = CPU                │  ← Sức mạnh xử lý thô
└─────────────────────────────────────┘
```

## Harness không phải là agent

Harness là hạ tầng bao quanh model:
- **Govern** cách agent hoạt động
- **Ensure** reliability, efficiency
- **Steer** hành vi qua hàng trăm tool calls
- **Persist** state across sessions

## Phân biệt 3 lớp Engineering

| Lớp | Phạm vi | Vấn đề giải quyết | Ví dụ |
|---|---|---|---|
| **Prompt Engineering** | Một lượt tương tác | Hướng dẫn model trong 1 turn | "Write a bootloader in NASM" |
| **Context Engineering** | Cả session | Quản lý token, tránh context rot | Summarize conversation khi context đầy |
| **Harness Engineering** | Toàn bộ deployment | Govern agent ACROSS mọi turn, mọi task, mọi ngày | AGENTS.md + Skills + Sub-agents + Validation |

## Tại sao Harness quan trọng?

- Nhiều AI agent projects không tới production vì thiếu harness: không có goal đo được, eval loop, state, guardrail, hoặc regression gate.
- Google, Oracle, và Meta engineering posts đều converged vào cùng pattern: trajectory/eval evidence, runtime guardrails, observability, standardized tools, and compact high-signal context.

## Trong ngữ cảnh OS Building

Harness cho OS building bao gồm:

1. **AGENTS.md** — Bản đồ project: tech stack (i686-elf-gcc, nasm, QEMU), build commands, architecture
2. **Skills** — On-demand capabilities: "write bootloader", "setup GDT", "compile kernel"
3. **Sub-agents** — Phân vai: code-writer, code-reviewer, test-runner, debugger
4. **Validation** — Boot verification: QEMU -> dedicated COM1 serial file -> exact required marker lines (`BOOT_OK`, `KERNEL_INIT_OK`)
5. **Memory** — Progress tracking: kernel đã compile chưa? boot thành công chưa?

## Bằng chứng thực tế: Google Antigravity 2.0

Theo official Google Antigravity post ngày 2026-05-19, Google đã dùng Antigravity harness để build một functional OS demo từ single prompt:
- **93 sub-agents** chạy song song
- **15,314** model calls
- **~339M** input tokens
- **12 giờ** tính toán tích cực
- **$916.92** chi phí API
- OS boot thành công, chạy được FreeDoom

Đây là bằng chứng mạnh cho thấy harness engineering có thể điều phối một OS demo phức tạp. Google cũng caveat rõ rằng code chưa đạt chất lượng kỳ vọng từ veteran developers và thiếu nhiều thành phần của modern OS. Vì vậy harness này vẫn nhắm tới toy/teaching OS bootable trong QEMU.

## Engineering Rubric

- **Measurable success:** agent không báo "done" nếu build/test/marker verdict chưa có bằng chứng.
- **Episode evidence:** mỗi run cần log task, tool calls, files touched, marker results, blockers, and side effects.
- **Risk gates:** build correctness -> boot markers -> regression -> safety constraints.
- **Source authority:** tool behavior dựa trên QEMU/NASM/binutils docs; OSDev dùng cho boot-sector convention.

## Tài liệu tham khảo

- [What Is Harness Engineering AI? - Atlan](https://atlan.com/know/what-is-harness-engineering/)
- [The importance of Agent Harness in 2026 - Philschmid](https://www.philschmid.de/agent-harness-2026)
- [Google Antigravity Built an OS](https://antigravity.google/blog/google-antigravity-built-an-os)
- [Google Agent Evaluation](https://cloud.google.com/blog/products/ai-machine-learning/introducing-agent-evaluation-in-vertex-ai-gen-ai-evaluation-service)
- [Oracle Observability for Agentic AI](https://blogs.oracle.com/ai-and-datascience/oci-observability-for-agentic-ai)
- [Meta Tribal Knowledge Mapping](https://engineering.fb.com/2026/04/06/developer-tools/how-meta-used-ai-to-map-tribal-knowledge-in-large-scale-data-pipelines/)
- [Meta Diff Risk Score](https://engineering.fb.com/2025/08/06/developer-tools/diff-risk-score-drs-ai-risk-aware-software-development-meta/)
