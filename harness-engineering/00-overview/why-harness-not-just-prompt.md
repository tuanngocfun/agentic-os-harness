# Tại sao cần Harness — Không chỉ Prompt Engineering?

## Vấn đề với Prompt Engineering thuần

Prompt Engineering chỉ giải quyết **một lượt tương tác**. Khi build OS, bạn cần:

- Hàng trăm lần compile → test → fix loop
- Quản lý context window qua nhiều giờ làm việc
- Đảm bảo agent không lặp lại lỗi đã sửa
- Phối hợp nhiều agent (writer, reviewer, tester)
- Persist state giữa các session

Prompt đơn lẻ **không thể** làm được điều này.

## So sánh 3 lớp Engineering

### Prompt Engineering — "Nói gì trong 1 turn"

```
User: "Write a boot sector that loads a kernel at 0x1000"
Agent: [writes boot sector code]
→ Hết. Không có validation, không có follow-up.
```

### Context Engineering — "Quản lý cả session"

```
Session start:
  - Load system prompt (persona, rules)
  - Load conversation history
  - Monitor token usage
  - Summarize khi context gần đầy

→ Giữ session mượt mà, nhưng không govern behavior across sessions.
```

### Harness Engineering — "Govern toàn bộ deployment"

```
Harness:
  - AGENTS.md: "Project dùng i686-elf-gcc, nasm, QEMU i386"
  - Skill "compile-and-run": Tự động make -> qemu -> parse exact required serial marker lines
  - Sub-agent "debugger": Phân tích kernel panic log, đề xuất fix
  - Memory: "Boot sector đã viết ở commit abc123, đang work on GDT"
  - Validation: Chạy regression test sau mỗi change

→ Govern behavior từ đầu đến cuối, across mọi session, mọi task.
```

## Bảng so sánh chi tiết

| Tiêu chí | Prompt | Context | Harness |
|---|---|---|---|
| **Scope** | 1 turn | 1 session | Mọi session, mọi ngày |
| **State persistence** | Không | Trong session | Across sessions (memory files) |
| **Validation** | Không | Không | Automated (eval scripts, boot tests) |
| **Multi-agent** | Không | Không | Có (sub-agents với tool restrictions) |
| **Skill loading** | Không | Không | On-demand (tránh context saturation) |
| **Guardrails** | Trong prompt | Trong system prompt | Enforcement layer (rules, constraints) |
| **Cost control** | Không | Token counting | Budget per task, tool restrictions |

## Case study: Build OS không có Harness

```
Turn 1: Agent viết boot sector → compile fail (sai syntax NASM)
Turn 2: User paste error → Agent fix → compile pass, boot fail
Turn 3: User paste boot log → Agent fix GDT → boot pass, kernel panic
Turn 4: User paste panic log → Agent quên mất boot sector đã viết ở Turn 1
        → Viết lại từ đầu, khác version
Turn 5: Context đầy → Agent quên toàn bộ → User bắt đầu lại
```

## Case study: Build OS CÓ Harness

```
Harness:
  - AGENTS.md: Build commands, architecture, boot sequence
  - Skill "debug-kernel-panic": Tự parse serial log, identify root cause
  - Memory "progress.md": "Boot sector OK, GDT OK, kernel entry đang fix"
  - Sub-agent "test-runner": Sau mỗi change, chạy QEMU boot test

Turn 1: Agent dùng skill "write-bootsector" -> compile -> QEMU test -> BOOT_OK
Turn 2: Agent dùng skill "setup-gdt" -> compile -> QEMU test -> KERNEL_INIT_OK
Turn 3: Agent dùng skill "kernel-entry" → compile → QEMU test → kernel panic
Turn 4: Agent dùng skill "debug-kernel-panic" → auto fix → QEMU test → PASS
→ Không cần user can thiệp. Memory giữ state. Validation chạy tự động.
```

## Google Antigravity: Bằng chứng thực tế

Google Antigravity post ngày 2026-05-19 là một public case study về multi-agent harness ở cấp OS demo:

| Chỉ số | Giá trị | Ý nghĩa |
|---|---|---|
| Sub-agents | 93 tác tử song song | Phân rã nhiệm vụ chuyên biệt hóa sâu |
| Model calls | 15,314 lượt | Vòng lặp compile → test → fix liên tục |
| Input tokens | ~339M | Toàn bộ codebase + context được đọc hiểu |
| Thời gian | ~12 giờ | Chạy song song, không cần human can thiệp |
| Chi phí | $916.92 | API pricing reported by Google |
| Output | OS chạy FreeDoom | Boot thành công, driver graphics + keyboard hoạt động |

Google cũng caveat rằng output chưa phải modern/production OS và code chưa đạt chất lượng veteran developers. **Kết luận:** Harness không phải nice-to-have. Với OS work, nó là cách biến task mơ hồ thành run có bằng chứng: build artifacts, serial markers, logs, memory, và regression verdicts.

## Engineering Rubric

| Gate | Agent phải chứng minh |
|---|---|
| Build correctness | `boot.bin` flat, `boot_config.inc` generated from `kernel.bin`, `kernel.elf` linked, `kernel.bin` raw, `os.img` created |
| Boot markers | Required COM1 marker lines `BOOT_OK`, `KERNEL_INIT_OK` xuất hiện chính xác |
| Regression | Thay đổi mới không phá marker đã pass trước đó |
| Safety | QEMU chạy user thường, không host disk/device passthrough |

## Tài liệu tham khảo

- [Writing a Good AGENTS.md - Philschmid](https://www.philschmid.de/writing-good-agents)
- [AGENTS.md Complete Guide (2026)](https://blog.buildbetter.ai/agents-md-complete-guide-for-engineering-teams-in-2026/)
- [Google Antigravity Built an OS](https://antigravity.google/blog/google-antigravity-built-an-os)
