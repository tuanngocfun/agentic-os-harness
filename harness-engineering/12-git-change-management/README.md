# 12 — Git Change Management

## Mục tiêu

Repo này đã có Git source of truth, nên harness phải quản lý change lifecycle chứ không chỉ quản lý build/test. Mục tiêu là: agent biết mình đang ở repo nào, thay đổi có scope gì, evidence nào cần có trước khi stage/commit/push, và tuyệt đối không phá worktree của người dùng.

## Decision

Có, nên update `harness-engineering` về Git. Lý do: tài liệu hiện tại đã khóa build artifacts, serial markers, QEMU safety, memory/evidence, nhưng chưa khóa Git workflow. Khi chuyển sang repo thật, Git trở thành sensor và guardrail cho toàn bộ harness:
- Sensor: `git status`, `git diff`, tracked artifact checks, branch/upstream state.
- Guardrail: cấm broad staging, cấm destructive reset/checkout/clean, cấm stage deletion khi chưa có xác nhận.
- Evidence: mỗi handoff phải có branch, diff summary, command evidence, risk tier, rollback note.

## Local Repo Contract

Current repo discovered during review:

```text
repo root: /home/ngocnt/operating_system/os
harness path: /home/ngocnt/operating_system/os/harness-engineering
default branch: main
remote: origin https://github.com/tuanngocfun/agentic-os-harness.git
```

Do not assume `/home/ngocnt/operating_system` is itself a Git repo. Use `git rev-parse --show-toplevel` from the file you are editing and operate on that repo root.

## Public Git Commands

Read-only preflight:

```bash
.agent/skills/git-change-management/scripts/git_preflight.sh
```

Safe inspection commands:

```bash
git status --short --branch
git diff --stat
git diff --cached --stat
git diff --name-status
git ls-files build '*.o' '*.elf' '*.bin' '*.img' '*.iso' '*.map' '*.log'
```

Agent must not run write Git commands unless an explicit user request for that operation is present in the current turn.

## Hard Rules

- Always inspect `git status --short --branch` before editing and before final handoff.
- Before any `git add`, commit, or push, show `git status --short` and a concise diff summary.
- Stage explicit file paths only. Do not use broad staging.
- Never stage deletions unless the user explicitly confirms those deletions.
- Never run destructive Git cleanup, history rewrite, reset, path checkout, or force-push commands unless the user explicitly requests that operation in the current turn.
- Never hide a dirty worktree. Classify changes as ours, user, or unknown when it matters.
- If unknown changes touch the same file you need to edit, read carefully and work with them. Ask only if the conflict blocks safe progress.
- Do not commit generated artifacts: `build/`, `*.o`, `*.elf`, `*.bin`, `*.img`, `*.iso`, `*.map`, `*.log`.
- Do not direct-push to protected/shared branches. Use short-lived branches/PRs when publishing changes.
- Do not invent CI/branch-protection state; inspect it or state that it is unknown.

## Change Size Policy

Follow the small-change principle:
- One change should have one reason.
- Include related tests or validation evidence with the change.
- Separate unrelated refactors from feature/doc changes.
- Split high-risk work by boundary: bootloader, linker, QEMU runner, marker parser, docs, safety.
- Keep each intermediate state buildable or clearly mark it as a docs-only draft.

For this OS harness, a good split is usually:
- `docs:` harness contract/index updates.
- `build:` Makefile/toolchain/build artifact changes.
- `boot:` bootloader/GDT/entry changes.
- `test:` marker parser, QEMU loop, regression fixtures.
- `safety:` host/QEMU/pass-through constraints.

## Branch And PR Policy

Default behavior:
1. Work on a short-lived branch for non-trivial implementation.
2. Keep `main` green and reviewable.
3. Use a PR or handoff summary with:
   - summary,
   - affected files,
   - risk tier,
   - validation commands and exit statuses,
   - marker/evidence verdict,
   - rollback plan,
   - unresolved questions.
4. If direct local commits are requested, still keep commits small and evidence-backed.

Release-like or shared branches need stronger gates: reviewer approval, full boot validation, drift checks, safety checks, and explicit rollback notes.

## Risk Gates

| Risk | Git examples | Required gates |
|---|---|---|
| High | bootloader, linker script, marker parser, QEMU safety, Git staging/commit/push policy | status, diff summary, `make all`, `make test`, drift checks, safety check, reviewer/auditor pass |
| Medium | skill docs, memory/evidence schema, platform mappings | status, diff summary, contract script, targeted doc consistency checks |
| Low | glossary wording, index link, typo | status, diff summary, link/path check |

Git operations are high-risk when they can change repository history, remote state, or another person’s worktree. Treat staging/commit/push as gated actions, not casual cleanup.

## Handoff Template

```markdown
## Git Handoff

Repo:
Branch:
Upstream:
Worktree status:
Files changed:
Diff stat:
Risk tier:
Validation:
- <command> -> <exit status>
Evidence:
- <path/log/verdict>
Not staged:
Staged:
Rollback plan:
Open questions:
```

## Evidence JSON Fields

Machine evidence should include Git state alongside build/test state:

```json
{
  "git": {
    "repo_root": "/home/ngocnt/operating_system/os",
    "branch": "main",
    "commit": "<hash>",
    "status_short": "clean|dirty",
    "diff_stat": "<summary>",
    "tracked_build_artifacts": false,
    "staged_paths": [],
    "deleted_paths_staged": []
  }
}
```

## Role Boundaries

- `git-steward`: may inspect status/diff, prepare a staging plan, and run Git preflight. It cannot stage, commit, push, rewrite history, or delete files without explicit user approval.
- `code-writer`: may edit assigned files, but should not stage or commit.
- `test-runner`: may report whether generated artifacts are ignored/tracked.
- `auditor`: checks that the final diff matches the user request and no generated artifact slipped into tracking.

## Source-Derived Calibration

- Google Engineering Practices emphasize small, self-contained CLs, related tests, keeping the build unbroken, and review based on improving code health.
- Meta Diff Risk Score is adapted here as a risk gate: higher-risk changes require stronger evidence, not just a larger prose summary.
- Microsoft Azure DevOps branch policy docs support protecting important branches with PRs, reviewers, build validation, and status checks.
- Swift.org contributing guidance is used for small incremental development, release branch approval, and respecting authorship/review boundaries.
- Netflix TechBlog’s PR confidence work maps to this harness through stable tests, async evidence, and developer responsibility for deciding whether a change is safe to merge.
- Tesla public Fleet Telemetry contribution docs show a conventional open-source workflow: issue/PR, branch, ready-for-review template, self-review, docs, and tests. This is public open-source evidence, not a claim about Tesla internal engineering.

## Anti-Patterns

| Anti-pattern | Risk | Correct pattern |
|---|---|---|
| Broad staging | Pulls in unrelated/user changes | Stage explicit paths only after status + diff summary |
| Staging generated images/logs | Makes repo noisy and stale | Keep `build/` and binary artifacts ignored |
| Silent commit after edits | Hides review opportunity | Ask/confirm before write Git operations |
| Destructive cleanup to get a clean tree | Can destroy user work | Preserve user changes; ask for explicit approval |
| One mega-commit for docs, build, boot, and safety | Hard to review and roll back | Split by contract/risk boundary |
| Claiming branch protections/CI exist without inspection | False safety | Inspect platform settings or label as unknown |

## Ready Checklist

Before a change is ready for user review:
- [ ] `git status --short --branch` captured.
- [ ] Diff summary captured.
- [ ] No generated build artifacts are tracked.
- [ ] Risk tier assigned.
- [ ] Required validation ran or skipped with reason.
- [ ] Handoff names files changed and evidence paths.
- [ ] No staging/commit/push performed unless explicitly requested.
