# Sub-Agents — Multi-Agent Orchestration

## Khi nào cần Sub-Agents?

Dùng sub-agents khi task có thể **parallelized** hoặc cần **role separation**:

- **Code Writer Agent** — chỉ viết code, không review
- **Code Reviewer Agent** — chỉ đọc, không write
- **Test Runner Agent** — chỉ chạy tests, không modify
- **Debugger Agent** — phân tích logs, đề xuất fix
- **Documentation Agent** — chỉ viết docs

## Kiến trúc cho OS Building

```
┌─────────────────────────────────────┐
│         Orchestrator Agent          │  ← Phân công tasks, merge results
├─────────┬─────────┬─────────┬───────┤
│ Writer  │ Reviewer│ Tester  │Debug  │
│ Agent   │ Agent   │ Agent   │Agent  │
│         │         │         │       │
│ Write   │ Read    │ Run QEMU│Parse  │
│ .asm    │ code    │ grep    │panic  │
│ .c      │ review  │ markers │log    │
└─────────┴─────────┴─────────┴───────┘
```

## Vai trò cụ thể cho OS Project

### Orchestrator
- Phân tích task lớn → chia thành sub-tasks
- Giao sub-tasks cho đúng agent
- Merge kết quả, resolve conflicts
- Có quyền truy cập tất cả tools

### Code Writer
- Viết code .asm, .c theo spec
- **Tools:** Read, Write, Edit, Bash(build only)
- **KHÔNG có:** quyền chạy QEMU, quyền modify test files

### Code Reviewer
- Đọc code, check logic errors, security issues
- **Tools:** Read, Grep, Glob
- **KHÔNG có:** Write, Edit, Bash

### Test Runner
- Chạy `make test`, QEMU boot test
- Parse serial output, report results
- **Tools:** Read, Bash(test commands only)
- **KHÔNG có:** Write, Edit

### Debugger
- Phân tích kernel panic logs
- Identify root cause từ serial output
- Đề xuất fix (nhưng không tự fix)
- **Tools:** Read, Grep, Bash(read-only)
- **KHÔNG có:** Write, Edit

## Format Sub-Agent cho Claude Code

File `.claude/agents/<agent-name>.md`:

```markdown
---
name: code-reviewer
description: Triggered when user asks for code review, PR review, or quality
             check. Reviews code for bugs, security issues, and style violations.
tools: Read, Grep, Bash(read-only)
---

# Code Reviewer Agent

## Role
You are a senior code reviewer. You ONLY read and analyze. You do NOT modify files.

## Review Checklist
1. Security vulnerabilities (buffer overflow, null pointer dereference)
2. Logic errors và edge cases
3. Performance bottlenecks
4. Convention violations (cross-reference AGENTS.md)
5. x86-specific issues (segmentation, privilege levels, I/O permissions)

## Output Format
Trả về structured report:
- **Critical**: Bugs phải fix trước merge
- **Warning**: Issues nên address
- **Suggestion**: Nice-to-have improvements

## Constraints
- KHÔNG sử dụng Write, Edit, hoặc bất kỳ tool nào thay đổi files
- Nếu cần context về architecture, đọc agent_docs/architecture.md
```

## Format Sub-Agent cho Codex (TOML)

File `.codex/agents/<agent-name>.toml`:

```toml
name = "test-runner"
description = "Runs boot tests on QEMU and validates serial output."

developer_instructions = """
You are a test runner for an x86 bare metal OS project.

Your job:
1. Run `make clean && make all` to compile
2. Run `make test` which executes QEMU boot test
3. Parse serial output for exact required boot marker lines: BOOT_OK, KERNEL_INIT_OK
4. Report pass/fail with details

Output format:
- PASS: All boot markers found
- FAIL: Missing markers, with serial log excerpt

NEVER use write or edit tools. Only run test commands.
"""
```

## Format Sub-Agent cho Gemini Antigravity

File `.agent/agents/<agent-name>/SKILL.md`:

```markdown
---
name: debugger
description: "Analyzes kernel panic logs and serial output to identify root
cause of boot failures. Use when boot test fails or kernel panic occurs."
---

# Debugger Agent

## Goal
Identify root cause of kernel boot failure from serial output and logs.

## Instructions
1. Read the serial output log
2. Search for panic messages, error codes, stack traces
3. Cross-reference with known issues in agent_docs/known_issues.md
4. Provide structured diagnosis:
   - **Error Type**: (kernel panic / triple fault / hang / reboot loop)
   - **Location**: (file:line nếu có)
   - **Root Cause**: (phân tích)
   - **Suggested Fix**: (code change đề xuất)

## Constraints
- KHÔNG tự ý modify code — chỉ đề xuất
- Nếu không đủ thông tin, yêu cầu thêm log
```

## Tool Restrictions — Bắt buộc Khi Materialize

Mỗi sub-agent PHẢI có explicit tool restrictions trong platform-specific role file hoặc wrapper trước khi claim enforcement. Table này là contract thiết kế; nó không tự enforce nếu chỉ nằm trong Markdown.

| Agent | Read | Write | Edit | Bash | Grep |
|---|---|---|---|---|---|
| Orchestrator | ✓ | ✓ | ✓ | ✓ | ✓ |
| Code Writer | ✓ | ✓ | ✓ | build only | ✓ |
| Code Reviewer | ✓ | ✗ | ✗ | read-only | ✓ |
| Test Runner | ✓ | ✗ | ✗ | test only | ✓ |
| Debugger | ✓ | ✗ | ✗ | read-only | ✓ |
| Doc Writer | ✓ | ✓ | ✓ | ✗ | ✓ |

## Tài liệu tham khảo

- [Claude Code Harness - GitHub](https://github.com/Chachamaru127/claude-code-harness)
- [Claude Code 기반 Harness를 Codex 구조로 전환하기](https://redtrain.tistory.com/933)
