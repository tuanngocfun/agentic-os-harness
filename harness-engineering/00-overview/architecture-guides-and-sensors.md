# Kiến trúc Harness: Guides & Sensors

## Guides/Sensors Taxonomy

Birgitta Bockeler trên martinfowler.com mô tả coding-agent harnesses bằng 2 lớp chính: guides và sensors. Trong tài liệu này, dùng framing đó như một practical taxonomy, không phải formal standard.

```
┌─────────────────────────────────────────────────┐
│                   HARNESS                        │
│                                                  │
│  ┌──────────────────┐  ┌──────────────────────┐ │
│  │     GUIDES        │  │      SENSORS         │ │
│  │  (Feedforward)    │  │    (Feedback)        │ │
│  │                   │  │                      │ │
│  │  Chạy TRƯỚC khi   │  │  Chạy SAU khi        │ │
│  │  agent hành động  │  │  agent hành động     │ │
│  │                   │  │                      │ │
│  │  • System Prompts │  │  • Evals             │ │
│  │  • AGENTS.md      │  │  • Validation Loops  │ │
│  │  • Constraints    │  │  • Output Parsers    │ │
│  │  • SKILL.md       │  │  • Drift Detectors   │ │
│  └──────────────────┘  └──────────────────────┘ │
└─────────────────────────────────────────────────┘
```

## Guides — Feedforward Controls

Guides encode những gì agent **biết** và **được phép làm**. Chúng chạy TRƯỚC khi agent hành động.

### System Prompts
- Instruction set nền: persona, scope, output format
- Ví dụ: "You are an OS developer. You write x86 assembly and C. You test on QEMU."

### AGENTS.md
- Bản đồ codebase: files được touch, conventions, tools available
- Ví dụ: "Build: make all | Test: make test | Required markers: BOOT_OK, KERNEL_INIT_OK"
- **Đây là file quan trọng nhất trong harness**

### Constraint Files
- "Tuyệt đối không được làm gì", "Cần approval khi nào"
- Ví dụ: "KHÔNG dùng `sudo` trong build commands", "KHÔNG modify boot.asm sau khi test pass"

### SKILL.md
- On-demand capability, chỉ load khi intent match
- Ví dụ: Skill "write-bootloader" chỉ activate khi user nói "write boot sector"
- Tránh context saturation: không load 50k tokens tools vào context ngay từ đầu

## Sensors — Feedback Controls

Sensors quan sát và **validate** behavior. Chúng chạy SAU khi agent hành động.

### Evals
- Automated test suites cho agent output
- Detect degradation: "Lần trước boot pass, lần này fail → regression"
- Ví dụ: Script chạy QEMU, parse exact required COM1 marker lines, exit code 0 = pass

### Validation Loops
- Real-time checks trước khi commit output
- Ví dụ: `make clean && make all` phải pass trước khi agent báo "done"
- Ví dụ: `make test` phải tạo `build/serial.log` và detect required markers trong timeout

### Output Parsers
- Chuyển LLM text → typed, verifiable data
- Ví dụ: Parse NASM compile output, extract error line numbers
- Ví dụ: Parse QEMU serial output, extract boot markers

### Drift Detectors
- Phát hiện khi behavior thay đổi đột ngột
- Ví dụ: "Agent suddenly using `gcc` instead of `i686-elf-gcc` → toolchain drift"
- Ví dụ: "Boot time increased from 2s to 30s → performance regression"

## Áp dụng cho OS Building

### Guides cho OS project

| Guide | Nội dung | File |
|---|---|---|
| System Prompt | "You are building x86 bare metal OS in C" | AGENTS.md |
| Tech Stack | i686-elf-gcc, nasm, ld, QEMU i386 | AGENTS.md |
| Build Commands | `make all`, `make clean`, `make run` | AGENTS.md |
| Architecture | Boot → GDT → IDT → Kernel → Shell | agent_docs/architecture.md |
| Constraints | KHÔNG dùng `gcc` thường, KHÔNG dùng `-m32` | AGENTS.md |
| Skills | write-bootloader, setup-gdt, compile-kernel | .agent/skills/ |

### Sensors cho OS project

| Sensor | Cơ chế | Script |
|---|---|---|
| Boot Test | QEMU chạy trong timeout, parse exact `BOOT_OK` + `KERNEL_INIT_OK` lines | scripts/boot_test.sh |
| Compile Check | `make all` exit code 0 | Makefile |
| Serial Validation | Parse serial output cho expected markers | scripts/parse_serial.py |
| Regression Test | Boot pass trước → boot pass sau | scripts/regression.sh |
| Drift Detection | Check toolchain version, QEMU version | scripts/check_env.sh |

## Flow: Guides → Agent → Sensors

```
1. GUIDES load vào context
   ├── AGENTS.md: "Project dùng i686-elf-gcc, target i386"
   ├── Skill menu: [write-bootloader, setup-gdt, compile-kernel]
   └── Constraints: "KHÔNG dùng gcc thường"

2. AGENT hành động
   ├── Select skill "setup-gdt" (intent match)
   ├── Write gdt.asm + gdt.c
   └── Run: make all

3. SENSORS validate
   ├── Git preflight: repo root, status/diff, tracked artifact check? ✓
   ├── Compile check: exit code 0? ✓
   ├── Boot test: QEMU timeout, exact "BOOT_OK" + "KERNEL_INIT_OK" lines? ✓
   ├── Regression: boot vẫn pass sau change? ✓
   └── Drift: toolchain version unchanged? ✓

4. Nếu sensor fail → Agent dừng, báo lỗi, không tiếp tục
```

## Engineering Rubric

Harness tốt không chỉ có guide, mà phải có evidence loop:

- **Episode:** task, context loaded, files touched, commands run, outputs, side effects.
- **Git state:** branch/status/diff and tracked-artifact verdict before handoff.
- **Risk gates:** Git status/diff -> build artifacts -> required boot markers -> regression -> safety.
- **Drift detection:** agent dùng `gcc`, `-m32`, `boot.o` sector write, hoặc VGA-only marker thì fail.

## Tài liệu tham khảo

- [What Is Harness Engineering AI? - Atlan](https://atlan.com/know/what-is-harness-engineering/)
- [The importance of Agent Harness in 2026 - Philschmid](https://www.philschmid.de/agent-harness-2026)
- [Your Repo Needs an Agent Harness](https://jovanipink.com/posts/repo-agent-harness-markdown-skills)
