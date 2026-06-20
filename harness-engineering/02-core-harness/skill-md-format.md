# SKILL.md — On-Demand Capability Extension

## Tại sao cần Skills?

Skills giải quyết **Context Saturation**:

- Nếu load 40-50k tokens tools vào context ngay từ đầu → high latency + "context rot"
- Model bị confuse bởi irrelevant data

Thay vào đó:
1. Model được expose một **"menu" metadata nhẹ** (chỉ name + description)
2. Khi user intent match với description → full skill content inject vào context
3. Sau khi task xong → skill content release → context sạch lại

## Cơ chế hoạt động

```
┌─────────────────────────────────────────────┐
│              Context Window                  │
│                                              │
│  ┌──────────────────────────────────────┐   │
│  │ System Prompt + AGENTS.md (always)    │   │
│  ├──────────────────────────────────────┤   │
│  │ Skill Menu (lightweight metadata)     │   │
│  │  • write-bootloader: "Write boot..."  │   │
│  │  • setup-gdt: "Configure GDT..."      │   │
│  │  • compile-and-run: "Build and..."    │   │
│  ├──────────────────────────────────────┤   │
│  │ [ON-DEMAND] Full skill content        │   │  ← Inject khi intent match
│  │ [ON-DEMAND] References, scripts       │   │  ← Release sau khi xong
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

## Cấu trúc thư mục Skill

```
skill-name/
├── SKILL.md              ← Bắt buộc — brain của skill
├── scripts/
│   └── main.py           ← Tùy chọn — deterministic execution
├── references/
│   └── docs.md           ← Tùy chọn — static knowledge
└── examples/
    ├── input_example.json
    └── output_example.py
```

## Format SKILL.md

```markdown
---
name: <tên-kebab-case>
description: <CỰC KỲ QUAN TRỌNG — đây là trigger phrase. Phải đủ cụ thể để LLM
             semantic-match được. "Use this skill when user asks to..."
             Ví dụ tốt: "Writes x86 real-mode boot sector in NASM that loads
             a kernel at 0x1000. Use when user starts a new OS project or
             needs to modify the bootloader."
             Ví dụ kém: "Boot tools">
---

# <Skill Name>

## Goal
<Một câu rõ ràng về skill đạt được gì>

## Instructions
1. <Step rõ ràng, actionable>
2. <Nếu có script: "Run scripts/main.py <args>">
3. <Interpret output: exit code 0 = OK, exit code 1 = error>

## Examples
Input: <ví dụ>
Output: <ví dụ>

## Constraints
- KHÔNG bao giờ <hành động nguy hiểm>
- Nếu output > <N> rows, summarize thay vì list toàn bộ
```

## 5 Level Skill

| Level | Pattern | Skill chứa | Ví dụ Use Case |
|---|---|---|---|
| 1 | **Basic Router** | Chỉ `SKILL.md` | Git commit formatter theo Conventional Commits |
| 2 | **Asset Utilization** | `SKILL.md` + `references/` | License header adder (tránh hallucinate legal text) |
| 3 | **Few-Shot** | `SKILL.md` + `examples/` | JSON → Pydantic model với coding style nhất quán |
| 4 | **Tool Use** | `SKILL.md` + `scripts/` | Boot test runner (deterministic, không guess) |
| 5 | **Architect** | Tất cả trên | OS build pipeline: scaffold + template + example + script |

### Ví dụ cho OS project

| Skill | Level | Nội dung |
|---|---|---|
| `write-bootsector` | 3 (Few-Shot) | SKILL.md + examples/boot.asm |
| `setup-gdt` | 2 (Asset) | SKILL.md + references/gdt_format.md |
| `compile-and-run` | 4 (Tool) | SKILL.md + scripts/build_and_test.sh |
| `debug-kernel-panic` | 5 (Architect) | SKILL.md + scripts/parse_panic.py + references/panic_codes.md + examples/ |

## Trigger Description — Quan trọng nhất

Description quyết định skill có được activate hay không.

### Tốt (cụ thể, actionable)

```yaml
description: "Writes the minimal x86 real-mode stage-1 sector in NASM syntax,
loads the reserved stage-2 image, and preserves the stage-2 LBA kernel loader
contract. Use this
skill when user asks to create a bootloader, modify boot sector, or start
a new bare-metal OS project."
```

### Kém (mơ hồ, không trigger)

```yaml
description: "Boot tools"
```

### Quy tắc viết description

1. Bắt đầu bằng action verb: "Writes", "Configures", "Validates"
2. Nêu rõ technology and current loader profile: "x86 real-mode", "NASM syntax", "stage-2 LBA", kernel at LBA 33
3. Nêu rõ trigger: "Use this skill when user asks to..."
4. Ngắn gọn nhưng đủ cụ thể (2-3 câu)

## Verification Steps trong SKILL.md

Mỗi skill nên có section verification:

```markdown
## Verification Steps (sau mỗi action)
1. Run `make all` — phải pass với zero warnings
2. Run `make test` — boot test phải pass
3. Nếu thay đổi boot.asm: run `make boot-verify`
4. Nếu bất kỳ bước nào fail: DỪNG, report error, đừng tiếp tục
```

## Anti-Patterns

| Anti-Pattern | Vấn đề | Fix |
|---|---|---|
| Skill description mơ hồ ("Database tools") | Skill không bao giờ được trigger | Rewrite với "Use this when user asks to..." |
| SKILL.md không có Constraints section | Agent overcorrect hoặc làm quá scope | Thêm explicit "KHÔNG được làm..." |
| Load tất cả skills vào system prompt | Context saturation, chậm, tốn kém | Dùng on-demand loading |

## Tài liệu tham khảo

- [Authoring Google Antigravity Skills - Google Codelabs](https://codelabs.developers.google.com/getting-started-with-antigravity-skills)
- [Developer's Guide to Building ADK Agents with Skills](https://developers.googleblog.com/developers-guide-to-building-adk-agents-with-skills/)
