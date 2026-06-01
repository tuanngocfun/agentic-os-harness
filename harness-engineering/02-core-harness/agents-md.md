# AGENTS.md — File Quan Trọng Nhất trong Harness

## Tại sao AGENTS.md quan trọng?

- Đọc hoặc tham chiếu được bởi nhiều coding agents/tools như Claude Code, OpenAI Codex, Cursor, Gemini CLI, GitHub Copilot, Windsurf, Aider, Amazon Q
- Có vai trò như always-on project map: exact commands, architecture boundaries, anti-patterns, và pointers tới docs sâu hơn
- Với OS work, giá trị lớn nhất là làm cho agent luôn nhìn thấy build/test contract trước khi viết code

## Nguyên tắc viết

### NÊN có

- Tech stack với version pins cụ thể (Node 20.11, pnpm 9.x, Python 3.12)
- **Exact build/test/run commands** — đây là ROI cao nhất
- Architecture patterns và module boundaries
- Things to Avoid — anti-patterns, deprecated paths
- Pointer đến `agent_docs/` cho context sâu hơn

### KHÔNG nên có

- Directory listings dài dòng — agent tự khám phá được
- Code style guidelines — dùng linter/formatter thay thế
- Task-specific instructions chỉ dùng đôi khi — gây context dilution
- Auto-generated content (`/init`) giữ nguyên — dễ generic/stale; cần review và viết tay lại theo project thật

## Template AGENTS.md Hoàn Chỉnh

```markdown
# AGENTS.md

## Project Overview
<2-3 câu: codebase làm gì, ai dùng, đây là monorepo hay single service>

## Tech Stack
- Runtime: <ví dụ: i686-elf-gcc 13.2, NASM 2.16>
- Package manager: <nếu có>
- Build system: <GNU Make 4.3>
- Emulator: <QEMU 8.2, qemu-system-i386>
- Language: <C (freestanding), x86 Assembly (NASM syntax)>

## Setup Commands
```bash
make toolchain        # Build cross-compiler (nếu cần)
make all              # Compile kernel + bootloader
make run              # Chạy trên QEMU
make test             # Chạy boot test
make clean            # Xóa build artifacts
```

## Architecture Notes
- Pattern: <monolithic kernel / microkernel>
- Boot sequence: boot.asm → gdt → idt → kernel_main()
- Key modules: <mô tả ngắn từng module>
- Xem chi tiết: agent_docs/architecture.md

## Testing Instructions
- Xem chi tiết: agent_docs/running_tests.md
- Boot test: `make test` -> exit code 0 = pass
- Core required serial output markers: exact lines `BOOT_OK`, `KERNEL_INIT_OK`
- Current shell phase also requires `SHELL_READY` and a shell-runtime test when the shell is implemented.
- Optional serial output markers: TESTS_PASS

## Things to Avoid
- KHÔNG dùng `gcc` thường — dùng `i686-elf-gcc` thay thế
- KHÔNG dùng `-m32` — cross-compiler đã target đúng arch
- KHÔNG assemble boot sector bằng `nasm -f elf32` để ghi vào image
- KHÔNG link bootloader vào kernel
- KHÔNG expect VGA/BIOS output xuất hiện trong serial log
- KHÔNG thêm dependency mới mà không update file này
- KHÔNG modify boot.asm sau khi boot test pass

## Available Skills
- `write-bootloader` — khi cần viết/sửa boot sector
- `setup-gdt` — khi cần cấu hình Global Descriptor Table
- `compile-and-run` — khi cần build và test trên QEMU
```

## Checklist cho AGENTS.md

```
[ ] Tech stack với version pins
[ ] Exact build commands (make all, make run, make test)
[ ] Architecture notes (boot sequence, key modules)
[ ] Testing instructions (how to run, what to expect)
[ ] Things to Avoid (anti-patterns, deprecated paths)
[ ] Available Skills list
[ ] Pointer đến agent_docs/ cho chi tiết
[ ] < 300 dòng
```

## Anti-Patterns

| Anti-Pattern | Vấn đề | Fix |
|---|---|---|
| AGENTS.md > 500 dòng | Context dilution, agent confuse | Trim xuống < 300, move xuống `agent_docs/` |
| Auto-generate bằng `/init` rồi giữ nguyên | Dễ tạo hướng dẫn dài, generic, stale | Viết tay, deliberate, kiểm chứng bằng commands thật |
| Không có build commands | Agent không biết cách build/test | Thêm exact commands |
| Không có Things to Avoid | Agent dùng deprecated patterns | Thêm explicit constraints |

## Tài liệu tham khảo

- [Writing a Good AGENTS.md - Philschmid](https://www.philschmid.de/writing-good-agents)
- [AGENTS.md Complete Guide (2026)](https://blog.buildbetter.ai/agents-md-complete-guide-for-engineering-teams-in-2026/)
